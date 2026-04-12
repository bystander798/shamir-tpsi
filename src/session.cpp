#include "tpsi/session.hpp"

#include "tpsi/aes_ctr_prng.hpp"
#include "tpsi/message_channel.hpp"
#include "tpsi/polynomial.hpp"
#include "tpsi/random_provider.hpp"
#include "tpsi/serialization.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

#include <openssl/evp.h>

#include <sys/resource.h>
#include <unistd.h>

namespace tpsi {

std::size_t required_ring_degree(const SessionConfig &cfg) {
    const std::size_t receiver_size = cfg.receiver_set.size();
    const std::size_t sender_size = cfg.sender_set.size();
    std::size_t pseudo_needed = 0;
    if (receiver_size > cfg.threshold) {
        pseudo_needed = receiver_size - cfg.threshold;
    }
    const std::size_t deg_u0 = receiver_size + pseudo_needed;
    const std::size_t deg_u1 = sender_size + pseudo_needed;
    const std::size_t deg_v1 = receiver_size + sender_size + pseudo_needed;
    const std::size_t threshold_deg = (cfg.threshold == 0) ? 0 : (cfg.threshold - 1);
    const std::size_t max_deg = std::max({deg_u0 + deg_u1, deg_v1, threshold_deg});
    return max_deg + 1;
}

namespace {

using Clock = std::chrono::high_resolution_clock;

[[noreturn]] void protocol_throw(const std::string &msg) {
    throw std::runtime_error("protocol error: " + msg);
}

std::vector<std::uint8_t> hash_sm3(const std::vector<std::uint8_t> &data) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }
    std::vector<std::uint8_t> digest(EVP_MD_size(EVP_sm3()));
    if (EVP_DigestInit_ex(ctx, EVP_sm3(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data.data(), data.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SM3 digest failed");
    }
    unsigned int out_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest.data(), &out_len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }
    digest.resize(out_len);
    EVP_MD_CTX_free(ctx);
    return digest;
}

std::vector<std::uint8_t> random_seed(std::size_t len) {
    auto bytes = csprng_bytes(len);
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

std::vector<std::uint8_t> coin_flip_initiator(MessageChannel &channel, std::size_t seed_len,
                                              std::size_t &bytes_sent,
                                              std::size_t &bytes_received) {
    const auto secret = random_seed(seed_len);
    const auto commit = hash_sm3(secret);
    channel.send(MessageType::CoinFlipCommit, commit);
    bytes_sent += commit.size();

    const auto responder_commit_msg = channel.receive();
    if (responder_commit_msg.type != MessageType::CoinFlipCommit) {
        protocol_throw("expected responder commit");
    }
    const auto responder_commit = responder_commit_msg.payload;
    bytes_received += responder_commit.size();

    channel.send(MessageType::CoinFlipReveal, secret);
    bytes_sent += secret.size();

    const auto reveal_msg = channel.receive();
    if (reveal_msg.type != MessageType::CoinFlipReveal) {
        protocol_throw("expected responder reveal");
    }
    const auto responder_secret = reveal_msg.payload;
    bytes_received += responder_secret.size();

    if (hash_sm3(responder_secret) != responder_commit) {
        protocol_throw("coin flip responder commit mismatch");
    }
    std::vector<std::uint8_t> combined;
    combined.reserve(secret.size() + responder_secret.size());
    combined.insert(combined.end(), secret.begin(), secret.end());
    combined.insert(combined.end(), responder_secret.begin(), responder_secret.end());
    return hash_sm3(combined);
}

std::vector<std::uint8_t> coin_flip_responder(MessageChannel &channel, std::size_t seed_len,
                                              std::size_t &bytes_sent,
                                              std::size_t &bytes_received) {
    const auto initiator_commit_msg = channel.receive();
    if (initiator_commit_msg.type != MessageType::CoinFlipCommit) {
        protocol_throw("expected initiator commit");
    }
    const auto initiator_commit = initiator_commit_msg.payload;
    bytes_received += initiator_commit.size();

    const auto secret = random_seed(seed_len);
    const auto commit = hash_sm3(secret);
    channel.send(MessageType::CoinFlipCommit, commit);
    bytes_sent += commit.size();

    const auto reveal_msg = channel.receive();
    if (reveal_msg.type != MessageType::CoinFlipReveal) {
        protocol_throw("expected initiator reveal");
    }
    const auto initiator_secret = reveal_msg.payload;
    bytes_received += initiator_secret.size();
    if (hash_sm3(initiator_secret) != initiator_commit) {
        protocol_throw("coin flip initiator commit mismatch");
    }

    channel.send(MessageType::CoinFlipReveal, secret);
    bytes_sent += secret.size();

    std::vector<std::uint8_t> combined;
    combined.reserve(secret.size() + initiator_secret.size());
    combined.insert(combined.end(), initiator_secret.begin(), initiator_secret.end());
    combined.insert(combined.end(), secret.begin(), secret.end());
    return hash_sm3(combined);
}

std::vector<std::uint8_t> sm3_digest_uint64(std::uint64_t value) {
    std::vector<std::uint8_t> bytes(sizeof(std::uint64_t));
    for (std::size_t i = 0; i < sizeof(std::uint64_t); ++i) {
        bytes[sizeof(std::uint64_t) - 1 - i] =
            static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
    }
    return hash_sm3(bytes);
}

std::vector<std::uint8_t> derive_seed_material(const std::vector<std::uint8_t> &base,
                                               std::size_t needed,
                                               std::uint32_t label) {
    std::vector<std::uint8_t> material;
    material.reserve(needed);
    std::uint32_t counter = 0;
    while (material.size() < needed) {
        std::vector<std::uint8_t> block(base);
        block.push_back(static_cast<std::uint8_t>((label >> 24) & 0xFF));
        block.push_back(static_cast<std::uint8_t>((label >> 16) & 0xFF));
        block.push_back(static_cast<std::uint8_t>((label >> 8) & 0xFF));
        block.push_back(static_cast<std::uint8_t>(label & 0xFF));
        block.push_back(static_cast<std::uint8_t>((counter >> 24) & 0xFF));
        block.push_back(static_cast<std::uint8_t>((counter >> 16) & 0xFF));
        block.push_back(static_cast<std::uint8_t>((counter >> 8) & 0xFF));
        block.push_back(static_cast<std::uint8_t>(counter & 0xFF));
        auto digest = hash_sm3(block);
        const std::size_t to_copy = std::min<std::size_t>(digest.size(), needed - material.size());
        material.insert(material.end(), digest.begin(), digest.begin() + to_copy);
        ++counter;
    }
    return material;
}

double to_ms(const Clock::duration &dur) {
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(dur).count();
}

double max_rss_megabytes() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_maxrss / 1024.0;
    }
    return -1.0;
}

double current_rss_megabytes() {
    std::ifstream statm("/proc/self/statm");
    std::uint64_t resident_pages = 0;
    std::uint64_t total_pages = 0;
    if (!(statm >> total_pages >> resident_pages)) {
        return -1.0;
    }
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return -1.0;
    }
    return static_cast<double>(resident_pages * static_cast<std::uint64_t>(page_size)) /
           (1024.0 * 1024.0);
}

RingElement embed_poly(const PolynomialRing &ring,
                       const FiniteField &field,
                       const PolyCoeffs &coeffs) {
    const auto ring_degree = ring.config().degree;
    const auto modulus = field.modulus();
    if (ring_degree == 0) {
        throw std::invalid_argument("ring degree must be positive");
    }
    if (coeffs.size() > ring_degree) {
        throw std::invalid_argument("polynomial degree exceeds ring capacity");
    }
    std::vector<std::uint64_t> padded(ring_degree, 0);
    for (std::size_t i = 0; i < coeffs.size(); ++i) {
        padded[i] = coeffs[i] % modulus;
    }
    return ring.from_coefficients(padded);
}

PolyCoeffs ring_to_poly(const FiniteField &field, const RingElement &elem) {
    PolyCoeffs coeffs(elem.coefficients().begin(), elem.coefficients().end());
    const auto modulus = field.modulus();
    for (auto &c : coeffs) {
        c %= modulus;
    }
    while (!coeffs.empty() && coeffs.back() == 0) {
        coeffs.pop_back();
    }
    if (coeffs.empty()) {
        coeffs.push_back(0);
    }
    return coeffs;
}

PolyCoeffs deterministic_random_polynomial(AESCTRPrng &prng,
                                           const FiniteField &field,
                                           std::size_t degree) {
    PolyCoeffs coeffs(degree + 1, 0);
    for (std::size_t i = 0; i <= degree; ++i) {
        coeffs[i] = field.normalize(prng.random_uint64(field.modulus()));
    }
    if (!coeffs.empty() && coeffs.back() == 0) {
        coeffs.back() = 1;
    }
    return coeffs;
}

struct PseudoGenerationResult {
    std::vector<std::uint64_t> pseudo_elements;
    std::vector<std::uint64_t> receiver_with_pseudo;
};

PseudoGenerationResult generate_pseudo_elements(const SessionConfig &cfg,
                                                const FiniteField &field,
                                                const std::vector<std::uint8_t> &shared_seed) {
    constexpr std::size_t AES_SEED_BYTES = 48;
    auto seed_material = derive_seed_material(shared_seed, AES_SEED_BYTES, 0);
    auto prng = AESCTRPrng::from_seed(seed_material);

    std::unordered_set<std::uint64_t> used(cfg.receiver_set.begin(), cfg.receiver_set.end());
    for (auto val : cfg.sender_set) {
        used.insert(val);
    }

    std::vector<std::uint64_t> pseudo_elements;
    std::vector<std::uint64_t> receiver_with_pseudo = cfg.receiver_set;
    std::size_t pseudo_needed = 0;
    if (cfg.receiver_set.size() > cfg.threshold) {
        pseudo_needed = cfg.receiver_set.size() - cfg.threshold;
    }
    while (pseudo_elements.size() < pseudo_needed) {
        auto candidate = prng.random_uint64(field.modulus());
        if (used.insert(candidate).second) {
            pseudo_elements.push_back(candidate);
            receiver_with_pseudo.push_back(candidate);
        }
    }

    return {std::move(pseudo_elements), std::move(receiver_with_pseudo)};
}

PolyCoeffs deterministic_threshold_polynomial(const FiniteField &field,
                                              const std::vector<std::uint8_t> &shared_seed,
                                              std::uint64_t secret,
                                              std::size_t threshold) {
    if (threshold == 0) {
        return {secret};
    }
    PolyCoeffs coeffs(threshold, 0);
    coeffs[0] = field.normalize(secret);
    constexpr std::size_t AES_SEED_BYTES = 48;
    auto seed_material = derive_seed_material(shared_seed, AES_SEED_BYTES, 2);
    auto prng = AESCTRPrng::from_seed(seed_material);
    for (std::size_t i = 1; i < threshold; ++i) {
        coeffs[i] = field.normalize(prng.random_uint64(field.modulus()));
    }
    return coeffs;
}

struct ReceiverOfflineMetrics {
    double pcg_sender_ms{0.0};
    double pcg_receiver_ms{0.0};
    std::size_t pcg_seed_bytes{0};
};

struct ReceiverOfflineState {
    std::vector<std::uint64_t> pseudo_elements;
    std::vector<std::uint64_t> receiver_with_pseudo;
    PolyCoeffs u0_coeffs;
    RingElement X;
    std::unordered_map<std::uint64_t, std::size_t> receiver_index;
    RandomCorrelation corr;
    std::vector<std::uint8_t> shared_seed;
};

ReceiverOfflineState run_offline_receiver(const SessionConfig &cfg,
                                          const FiniteField &field,
                                          PolynomialRing &ring,
                                          RingLpnPCG &pcg,
                                          const std::vector<std::uint8_t> &shared_seed, ReceiverOfflineMetrics *metrics) {
    auto pseudo_data = generate_pseudo_elements(cfg, field, shared_seed);
    PolyCoeffs u0_coeffs = build_zero_poly(field, pseudo_data.receiver_with_pseudo);
    RingElement X = embed_poly(ring, field, u0_coeffs);

    std::unordered_map<std::uint64_t, std::size_t> receiver_index;
    receiver_index.reserve(pseudo_data.receiver_with_pseudo.size());
    for (std::size_t idx = 0; idx < pseudo_data.receiver_with_pseudo.size(); ++idx) {
        receiver_index[pseudo_data.receiver_with_pseudo[idx]] = idx;
    }

    PCGRunStats pcg_stats{};
    auto correlations = pcg.generate(1, &pcg_stats);
    if (correlations.empty()) {
        throw std::runtime_error("pcg.generate returned empty correlations");
    }
    RandomCorrelation corr = std::move(correlations.front());

    if (metrics) {
        metrics->pcg_sender_ms = pcg_stats.sender_compute_ms;
        metrics->pcg_receiver_ms = pcg_stats.receiver_compute_ms;
        metrics->pcg_seed_bytes = pcg_stats.seed_bytes;
    }

    return ReceiverOfflineState{std::move(pseudo_data.pseudo_elements),
                                std::move(pseudo_data.receiver_with_pseudo),
                                std::move(u0_coeffs),
                                std::move(X),
                                std::move(receiver_index),
                                std::move(corr),
                                shared_seed};
}

struct SenderOfflineState {
    RingElement U;
    RingElement V;
    PolyCoeffs shamir_coeffs;
    std::vector<std::uint8_t> secret_digest;
};

SenderOfflineState generate_sender_offline_state(const SessionConfig &cfg,
                                                 const FiniteField &field,
                                                 PolynomialRing &ring,
                                                 const std::vector<std::uint8_t> &shared_seed) {
    auto pseudo_data = generate_pseudo_elements(cfg, field, shared_seed);
    const std::uint64_t secret_value = uniform_uint64(field.modulus());
    auto shamir_coeffs =
        deterministic_threshold_polynomial(field, shared_seed, secret_value, cfg.threshold);
    auto secret_digest = sm3_digest_uint64(secret_value);

    auto aux_poly = build_zero_poly(field, cfg.sender_set);
    auto noise_poly = build_zero_poly(field, pseudo_data.pseudo_elements);

    constexpr std::size_t AES_SEED_BYTES = 48;
    auto random_seed = derive_seed_material(shared_seed, AES_SEED_BYTES, 1);
    auto prng = AESCTRPrng::from_seed(random_seed);

    auto R_poly = deterministic_random_polynomial(prng, field, cfg.receiver_set.size());
    auto u1_coeffs = deterministic_random_polynomial(prng, field, cfg.sender_set.size());

    auto mix = poly_mul(field, aux_poly, noise_poly);
    mix = poly_mul(field, mix, R_poly);
    auto v1_coeffs = poly_add(field, mix, shamir_coeffs);

    RingElement U = embed_poly(ring, field, u1_coeffs);
    RingElement V = embed_poly(ring, field, v1_coeffs);

    return SenderOfflineState{std::move(U),
                              std::move(V),
                              std::move(shamir_coeffs),
                              std::move(secret_digest)};
}

} // namespace

ReceiverOutput run_receiver_session(const SessionConfig &cfg,
                                    MessageChannel channel,
                                    std::size_t coin_flip_seed_bytes) {
    if (!channel.valid()) {
        throw std::invalid_argument("receiver session requires valid channel");
    }
    if (cfg.pcg.ring.degree == 0) {
        throw std::invalid_argument("ring degree must be positive");
    }

    ReceiverOutput output;
    output.result.comm.sender_bytes = 0;
    output.result.comm.receiver_bytes = 0;

    const Clock::time_point protocol_start = Clock::now();

    const Clock::time_point coin_start = Clock::now();
    auto coin_bytes_sent = std::size_t{0};
    auto coin_bytes_received = std::size_t{0};
    std::vector<std::uint8_t> shared_seed =
        coin_flip_initiator(channel, coin_flip_seed_bytes, coin_bytes_sent, coin_bytes_received);
    const Clock::time_point coin_end = Clock::now();
    const double coin_flip_ms = to_ms(coin_end - coin_start);

    std::size_t online_bytes_sent = 0;
    std::size_t online_bytes_received = 0;
    double online_send_ms = 0.0;
    double online_recv_ms = 0.0;

    FiniteField field(cfg.field);
    PolynomialRing ring(cfg.pcg.ring);
    RingLpnPCG pcg(cfg.pcg);

    ReceiverOfflineMetrics offline_metrics{};
    const Clock::time_point offline_compute_start = Clock::now();
    auto offline_state =
        run_offline_receiver(cfg, field, ring, pcg, shared_seed, &offline_metrics);
    const Clock::time_point offline_compute_end = Clock::now();
    const double receiver_offline_total_ms = to_ms(offline_compute_end - offline_compute_start);
    const double receiver_offline_ms =
        std::max(0.0, receiver_offline_total_ms - offline_metrics.pcg_sender_ms);
    const std::size_t coin_flip_bytes = coin_bytes_sent + coin_bytes_received;
    const std::size_t pcg_seed_bytes = offline_metrics.pcg_seed_bytes;
    const double pcg_sender_ms = offline_metrics.pcg_sender_ms;

    // Send offline data to sender
    std::vector<std::uint8_t> offline_payload;
    append_ring_element(offline_payload, offline_state.corr.a_prime);
    append_ring_element(offline_payload, offline_state.corr.b_prime);
    channel.send(MessageType::OfflineData, offline_payload);

    // Phase 1 receiver computation (X and X*)
    const Clock::time_point recv_phase1_start = Clock::now();
    const RingElement &X = offline_state.X;
    RingElement X_star = X.sub(offline_state.corr.x_prime);
    const Clock::time_point recv_phase1_end = Clock::now();

    std::vector<std::uint8_t> xstar_payload;
    append_ring_element(xstar_payload, X_star);
    const auto online_send_start = Clock::now();
    channel.send(MessageType::XStar, xstar_payload);
    const auto online_send_end = Clock::now();
    online_send_ms += to_ms(online_send_end - online_send_start);
    online_bytes_sent += xstar_payload.size();

    const auto online_recv_start = Clock::now();
    const auto response_msg = channel.receive();
    const auto online_recv_end = Clock::now();
    online_recv_ms += to_ms(online_recv_end - online_recv_start);
    if (response_msg.type != MessageType::SenderResponse) {
        protocol_throw("expected sender response");
    }
    online_bytes_received += response_msg.payload.size();
    std::size_t response_offset = 0;
    RingElement A_star = read_ring_element(cfg.pcg.ring, response_msg.payload, response_offset);
    RingElement B_star = read_ring_element(cfg.pcg.ring, response_msg.payload, response_offset);
    const double sender_time_ms = read_double(response_msg.payload, response_offset);
    const double sender_offline_ms =
        read_double(response_msg.payload, response_offset) + pcg_sender_ms;
    std::vector<std::uint8_t> sender_digest = read_bytes(response_msg.payload, response_offset);
    if (response_offset != response_msg.payload.size()) {
        protocol_throw("unexpected bytes after sender response");
    }
    output.result.comm.sender_time_ms = sender_time_ms;
    output.metrics.sender_offline_ms = sender_offline_ms;
    output.result.comm.sender_bytes = online_bytes_received;
    output.result.comm.receiver_bytes = online_bytes_sent;

    const Clock::time_point recv_phase2_start = Clock::now();
    RingElement ax = A_star.mul(X);
    RingElement C_ring = B_star.add(ax).sub(offline_state.corr.c_prime);
    const Clock::time_point recv_phase2_end = Clock::now();
    const double receiver_online_ms =
        to_ms((recv_phase1_end - recv_phase1_start) + (recv_phase2_end - recv_phase2_start));
    const double online_comm_ms = online_send_ms + online_recv_ms;
    const double online_total_ms = receiver_online_ms + sender_time_ms + online_comm_ms;
    const std::size_t online_comm_bytes = online_bytes_sent + online_bytes_received;
    const std::size_t total_comm_bytes =
        online_comm_bytes + coin_flip_bytes + pcg_seed_bytes;

    const Clock::time_point finalize_start = Clock::now();
    auto recovered_poly = ring_to_poly(field, C_ring);

    std::vector<Share> rss_points;
    rss_points.reserve(offline_state.receiver_with_pseudo.size());
    std::vector<std::uint64_t> evaluations;
    evaluations.reserve(offline_state.receiver_with_pseudo.size());
    for (std::size_t i = 0; i < offline_state.receiver_with_pseudo.size(); ++i) {
        const std::uint64_t x = offline_state.receiver_with_pseudo[i];
        const std::uint64_t value = poly_eval(field, recovered_poly, x);
        evaluations.push_back(value);
        rss_points.push_back({x, value});
    }

    RssConfig rss_cfg{.secret_degree = cfg.threshold - 1,
                      .max_errors = offline_state.pseudo_elements.size(),
                      .half_gcd_k_small = std::nullopt,
                      .half_gcd_min_drop = std::nullopt,
                      .half_gcd_min_effective_drop = std::nullopt};

    auto finalize_failure = [&](double rss_ms) -> ReceiverOutput {
        output.result.threshold_reached = false;
        output.result.secret = 0;
        output.result.comm.receiver_time_ms = receiver_online_ms;
        output.metrics.coin_flip_ms = coin_flip_ms;
        output.metrics.offline_compute_ms = receiver_offline_ms;
        output.metrics.sender_offline_ms = sender_offline_ms;
        output.metrics.receiver_online_compute_ms = receiver_online_ms;
        output.metrics.sender_online_compute_ms = sender_time_ms;
        output.metrics.online_comm_ms = online_comm_ms;
        output.metrics.online_comm_bytes = online_comm_bytes;
        output.metrics.online_total_ms = online_total_ms;
        output.metrics.coin_flip_bytes = coin_flip_bytes;
        output.metrics.pcg_seed_bytes = pcg_seed_bytes;
        output.metrics.total_comm_bytes = total_comm_bytes;
        output.metrics.rss_reconstruction_ms = rss_ms;
        output.metrics.current_rss_mb = current_rss_megabytes();
        output.metrics.max_rss_mb = max_rss_megabytes();
        output.metrics.total_protocol_ms = to_ms(Clock::now() - protocol_start);
        return output;
    };

    PolyCoeffs reconstructed_poly;
    bool success = false;
    try {
        reconstructed_poly = rss_reconstruct(field, rss_points, rss_cfg);
        success = true;
    } catch (...) {
        success = false;
    }

    if (!success) {
        return finalize_failure(to_ms(Clock::now() - finalize_start));
    }
    if (reconstructed_poly.empty()) {
        return finalize_failure(to_ms(Clock::now() - finalize_start));
    }

    const std::uint64_t reconstructed_secret = reconstructed_poly.front();
    auto recomputed_digest = sm3_digest_uint64(reconstructed_secret);
    if (recomputed_digest != sender_digest) {
        return finalize_failure(to_ms(Clock::now() - finalize_start));
    }

    output.result.threshold_reached = true;
    output.result.secret = reconstructed_secret;

    std::vector<std::uint64_t> intersection;
    intersection.reserve(cfg.receiver_set.size());
    for (auto x : cfg.receiver_set) {
        auto it = offline_state.receiver_index.find(x);
        if (it == offline_state.receiver_index.end()) continue;
        auto f_val = poly_eval(field, reconstructed_poly, x);
        if (f_val == evaluations[it->second]) {
            intersection.push_back(x);
        }
    }
    output.result.intersection = std::move(intersection);

    const Clock::time_point finalize_end = Clock::now();

    output.result.comm.receiver_time_ms = receiver_online_ms;
    output.metrics.coin_flip_ms = coin_flip_ms;
    output.metrics.offline_compute_ms = receiver_offline_ms;
    output.metrics.sender_offline_ms = sender_offline_ms;
    output.metrics.receiver_online_compute_ms = receiver_online_ms;
    output.metrics.sender_online_compute_ms = sender_time_ms;
    output.metrics.online_comm_ms = online_comm_ms;
    output.metrics.online_comm_bytes = online_comm_bytes;
    output.metrics.online_total_ms = online_total_ms;
    output.metrics.coin_flip_bytes = coin_flip_bytes;
    output.metrics.pcg_seed_bytes = pcg_seed_bytes;
    output.metrics.total_comm_bytes = total_comm_bytes;
    output.metrics.rss_reconstruction_ms = to_ms(finalize_end - finalize_start);
    output.metrics.total_protocol_ms = to_ms(finalize_end - protocol_start);
    output.metrics.current_rss_mb = current_rss_megabytes();
    output.metrics.max_rss_mb = max_rss_megabytes();
    return output;
}

void run_sender_session(const SessionConfig &cfg,
                        MessageChannel channel,
                        std::size_t coin_flip_seed_bytes) {
    if (!channel.valid()) {
        throw std::invalid_argument("sender session requires valid channel");
    }
    auto bytes_sent = std::size_t{0};
    auto bytes_received = std::size_t{0};
    std::vector<std::uint8_t> shared_seed =
        coin_flip_responder(channel, coin_flip_seed_bytes, bytes_sent, bytes_received);

    FiniteField field(cfg.field);
    PolynomialRing ring(cfg.pcg.ring);

    const auto sender_offline_start = Clock::now();
    auto offline_state = generate_sender_offline_state(cfg, field, ring, shared_seed);
    const auto sender_offline_end = Clock::now();
    const double sender_offline_ms = to_ms(sender_offline_end - sender_offline_start);

    const auto offline_msg = channel.receive();
    if (offline_msg.type != MessageType::OfflineData) {
        protocol_throw("sender expected offline data");
    }
    std::size_t offline_offset = 0;
    RingElement a_prime = read_ring_element(cfg.pcg.ring, offline_msg.payload, offline_offset);
    RingElement b_prime = read_ring_element(cfg.pcg.ring, offline_msg.payload, offline_offset);
    if (offline_offset != offline_msg.payload.size()) {
        protocol_throw("unexpected bytes after offline data");
    }

    const auto xstar_msg = channel.receive();
    if (xstar_msg.type != MessageType::XStar) {
        protocol_throw("sender expected XStar message");
    }
    bytes_received += xstar_msg.payload.size();
    std::size_t offset = 0;
    RingElement X_star = read_ring_element(cfg.pcg.ring, xstar_msg.payload, offset);
    if (offset != xstar_msg.payload.size()) {
        protocol_throw("unexpected bytes after XStar");
    }

    const auto sender_compute_start = Clock::now();
    RingElement A_star = offline_state.U.add(a_prime);
    RingElement correction = a_prime.mul(X_star);
    RingElement B_star = offline_state.V.add(b_prime).sub(correction);
    const auto sender_compute_end = Clock::now();
    const double sender_time_ms = to_ms(sender_compute_end - sender_compute_start);

    std::vector<std::uint8_t> response_payload;
    append_ring_element(response_payload, A_star);
    append_ring_element(response_payload, B_star);
    append_double(response_payload, sender_time_ms);
    append_double(response_payload, sender_offline_ms);
    append_bytes(response_payload, offline_state.secret_digest);
    channel.send(MessageType::SenderResponse, response_payload);
    bytes_sent += response_payload.size();
}

} // namespace tpsi







