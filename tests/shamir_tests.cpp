#include "tpsi/aes_ctr_prng.hpp"

#include "tpsi/message_channel.hpp"

#include "tpsi/pcg.hpp"

#include "tpsi/polynomial.hpp"

#include "tpsi/random_provider.hpp"

#include "tpsi/ring.hpp"

#include "tpsi/ring_lpn_pcg.hpp"

#include "tpsi/serialization.hpp"

#include "tpsi/session.hpp"

#include "tpsi/shamir.hpp"

#include "tpsi/rss.hpp"







#include <algorithm>



#include <cstdint>



#include <array>

#include <iomanip>



#include <iostream>



#include <random>



#include <sstream>



#include <stdexcept>



#include <string>



#include <unordered_set>



#include <vector>







#include <openssl/evp.h>







#include <sys/types.h>



#include <sys/wait.h>



#include <unistd.h>







namespace {







constexpr std::size_t kSetSize = 32;



constexpr std::size_t kSharedPrefixSize = 16;



constexpr std::size_t kThreshold = 16;



constexpr std::size_t kRounds = 5;



constexpr std::size_t kCoinFlipSeedBytes = 32;



constexpr std::size_t kAesSeedBytes = 48;







struct CoinFlipTrace {



    std::vector<std::uint8_t> self_seed;



    std::vector<std::uint8_t> peer_seed;



    std::vector<std::uint8_t> shared_seed;



};







std::vector<std::uint8_t> hash_sm3(const std::vector<std::uint8_t> &data) {



    EVP_MD_CTX *ctx = EVP_MD_CTX_new();



    if (!ctx) {



        throw std::runtime_error("EVP_MD_CTX_new failed");



    }



    std::vector<std::uint8_t> digest(static_cast<std::size_t>(EVP_MD_size(EVP_sm3())));



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



std::vector<std::uint8_t> sm3_digest_uint64(std::uint64_t value) {



    std::vector<std::uint8_t> bytes(sizeof(std::uint64_t));



    for (std::size_t i = 0; i < sizeof(std::uint64_t); ++i) {



        bytes[sizeof(std::uint64_t) - 1 - i] =



            static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);



    }



    return hash_sm3(bytes);



}





std::vector<std::uint8_t> random_seed(std::size_t len) {



    const auto bytes = tpsi::csprng_bytes(len);



    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());



}







CoinFlipTrace coin_flip_initiator_trace(tpsi::MessageChannel &channel, std::size_t seed_len) {



    const auto secret = random_seed(seed_len);



    const auto commit = hash_sm3(secret);



    channel.send(tpsi::MessageType::CoinFlipCommit, commit);







    const auto responder_commit_msg = channel.receive();



    if (responder_commit_msg.type != tpsi::MessageType::CoinFlipCommit) {



        throw std::runtime_error("expected responder commit");



    }



    const auto responder_commit = responder_commit_msg.payload;







    channel.send(tpsi::MessageType::CoinFlipReveal, secret);







    const auto reveal_msg = channel.receive();



    if (reveal_msg.type != tpsi::MessageType::CoinFlipReveal) {



        throw std::runtime_error("expected responder reveal");



    }



    const auto responder_secret = reveal_msg.payload;







    if (hash_sm3(responder_secret) != responder_commit) {



        throw std::runtime_error("responder commit mismatch");



    }







    std::vector<std::uint8_t> combined;



    combined.reserve(secret.size() + responder_secret.size());



    combined.insert(combined.end(), secret.begin(), secret.end());



    combined.insert(combined.end(), responder_secret.begin(), responder_secret.end());



    CoinFlipTrace trace;



    trace.self_seed = secret;



    trace.peer_seed = responder_secret;



    trace.shared_seed = hash_sm3(combined);



    return trace;



}







CoinFlipTrace coin_flip_responder_trace(tpsi::MessageChannel &channel, std::size_t seed_len) {



    const auto initiator_commit_msg = channel.receive();



    if (initiator_commit_msg.type != tpsi::MessageType::CoinFlipCommit) {



        throw std::runtime_error("expected initiator commit");



    }



    const auto initiator_commit = initiator_commit_msg.payload;







    const auto secret = random_seed(seed_len);



    const auto commit = hash_sm3(secret);



    channel.send(tpsi::MessageType::CoinFlipCommit, commit);







    const auto reveal_msg = channel.receive();



    if (reveal_msg.type != tpsi::MessageType::CoinFlipReveal) {



        throw std::runtime_error("expected initiator reveal");



    }



    const auto initiator_secret = reveal_msg.payload;







    channel.send(tpsi::MessageType::CoinFlipReveal, secret);







    if (hash_sm3(initiator_secret) != initiator_commit) {



        throw std::runtime_error("initiator commit mismatch");



    }







    std::vector<std::uint8_t> combined;



    combined.reserve(secret.size() + initiator_secret.size());



    combined.insert(combined.end(), initiator_secret.begin(), initiator_secret.end());



    combined.insert(combined.end(), secret.begin(), secret.end());



    CoinFlipTrace trace;



    trace.self_seed = secret;



    trace.peer_seed = initiator_secret;



    trace.shared_seed = hash_sm3(combined);



    return trace;



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



        const auto digest = hash_sm3(block);



        const std::size_t to_copy = std::min<std::size_t>(digest.size(), needed - material.size());



        material.insert(material.end(), digest.begin(), digest.begin() + to_copy);



        ++counter;



    }



    return material;



}







std::vector<std::uint64_t> generate_pseudo_elements(const tpsi::SessionConfig &cfg,



                                                    const tpsi::FiniteField &field,



                                                    const std::vector<std::uint8_t> &shared_seed) {



    std::unordered_set<std::uint64_t> used(cfg.receiver_set.begin(), cfg.receiver_set.end());



    for (auto value : cfg.sender_set) {



        used.insert(value);



    }







    const std::size_t pseudo_needed =



        cfg.receiver_set.size() > cfg.threshold ? cfg.receiver_set.size() - cfg.threshold : 0;







    std::vector<std::uint64_t> pseudo;



    pseudo.reserve(pseudo_needed);







    const auto seed_material = derive_seed_material(shared_seed, kAesSeedBytes, 0);



    auto prng = tpsi::AESCTRPrng::from_seed(seed_material);



    const std::uint64_t modulus = field.modulus();

    const std::uint64_t pseudo_bound =

        std::min<std::uint64_t>(std::uint64_t{1} << 8, modulus);







    while (pseudo.size() < pseudo_needed) {



        const std::uint64_t candidate = prng.random_uint64(pseudo_bound);



        if (used.insert(candidate).second) {



            pseudo.push_back(candidate);



        }



    }



    return pseudo;



}





tpsi::PolyCoeffs make_deterministic_random_polynomial(tpsi::AESCTRPrng &prng,

                                                              const tpsi::FiniteField &field,

                                                              std::size_t degree) {



    tpsi::PolyCoeffs coeffs(degree + 1, 0);



    for (std::size_t i = 0; i <= degree; ++i) {



        coeffs[i] = field.normalize(prng.random_uint64(field.modulus()));



    }



    if (!coeffs.empty() && coeffs.back() == 0) {



        coeffs.back() = 1;



    }



    return coeffs;



}





tpsi::PolyCoeffs make_deterministic_threshold_polynomial(const tpsi::FiniteField &field,

                                                                   const std::vector<std::uint8_t> &shared_seed,

                                                                   std::uint64_t secret,

                                                                   std::size_t threshold) {



    if (threshold == 0) {



        throw std::invalid_argument("threshold must be positive");



    }



    tpsi::PolyCoeffs coeffs(threshold, 0);



    coeffs[0] = field.normalize(secret);



    const auto seed_material = derive_seed_material(shared_seed, kAesSeedBytes, 2);



    auto prng = tpsi::AESCTRPrng::from_seed(seed_material);



    for (std::size_t i = 1; i < threshold; ++i) {



        coeffs[i] = field.normalize(prng.random_uint64(field.modulus()));



    }



    return coeffs;



}





std::vector<std::uint64_t> ring_to_poly_coeffs(const tpsi::FiniteField &field,

                                                         const tpsi::RingElement &elem) {



    std::vector<std::uint64_t> coeffs(elem.coefficients().begin(), elem.coefficients().end());



    const auto modulus = field.modulus();



    for (auto &c : coeffs) {



        c %= modulus;



    }



    while (coeffs.size() > 1 && coeffs.back() == 0) {



        coeffs.pop_back();



    }



    if (coeffs.empty()) {



        coeffs.push_back(0);



    }



    return coeffs;



}





struct PartySets {



    std::vector<std::uint64_t> receiver;



    std::vector<std::uint64_t> sender;



};







PartySets build_sets(std::mt19937_64 &rng) {



    PartySets sets;



    sets.receiver.resize(kSetSize);



    sets.sender.resize(kSetSize);







    for (std::size_t i = 0; i < kSharedPrefixSize; ++i) {



        sets.receiver[i] = static_cast<std::uint64_t>(i + 1);



        sets.sender[i] = static_cast<std::uint64_t>(i + 1);



    }







    std::unordered_set<std::uint64_t> used;



    for (std::size_t i = 0; i < kSharedPrefixSize; ++i) {



        used.insert(static_cast<std::uint64_t>(i + 1));



    }



    std::uniform_int_distribution<std::uint64_t> dist(0, (std::uint64_t{1} << 8) - 1);



    auto draw_unique = [&]() -> std::uint64_t {



        while (true) {



            const std::uint64_t value = dist(rng);



            if (used.insert(value).second) {



                return value;



            }



        }



    };







    for (std::size_t i = kSharedPrefixSize; i < kSetSize; ++i) {



        sets.receiver[i] = draw_unique();



    }







    for (std::size_t i = kSharedPrefixSize; i < kSetSize; ++i) {



        sets.sender[i] = draw_unique();



    }







    return sets;



}







std::string to_hex(const std::vector<std::uint8_t> &data) {



    std::ostringstream oss;



    oss << std::hex << std::setfill('0');



    for (auto byte : data) {



        oss << std::setw(2) << static_cast<unsigned int>(byte);



    }



    return oss.str();



}







std::string join_uint64(const std::vector<std::uint64_t> &values) {



    std::ostringstream oss;



    for (std::size_t i = 0; i < values.size(); ++i) {



        oss << values[i];



        if (i + 1 < values.size()) {



            oss << ",";



        }



    }



    return oss.str();



}



std::string join_uint64_prefix(const std::vector<std::uint64_t> &values,

                               std::size_t max_elements) {



    std::ostringstream oss;



    const std::size_t limit = std::min(values.size(), max_elements);



    for (std::size_t i = 0; i < limit; ++i) {



        oss << values[i];



        if (i + 1 < limit) {



            oss << ",";



        }



    }



    if (values.size() > max_elements) {



        oss << ",...";



    }



    return oss.str();



}





std::vector<std::uint64_t> parse_uint64_list(const std::string &text) {



    std::vector<std::uint64_t> values;



    if (text.empty()) {



        return values;



    }



    std::istringstream iss(text);



    std::string token;



    while (std::getline(iss, token, ',')) {



        if (token.empty()) {



            continue;



        }



        values.push_back(static_cast<std::uint64_t>(std::stoull(token)));



    }



    if (values.empty()) {



        values.push_back(0);



    }



    return values;



}





void trim_polynomial(std::vector<std::uint64_t> &poly) {



    while (poly.size() > 1 && poly.back() == 0) {



        poly.pop_back();



    }



    if (poly.empty()) {



        poly.push_back(0);



    }



}





bool parse_vector_line(const std::string &line, const std::string &prefix,

                        std::vector<std::uint64_t> &out) {



    if (line.rfind(prefix, 0) != 0) {



        return false;



    }



    try {



        out = parse_uint64_list(line.substr(prefix.size()));



    } catch (const std::exception &) {



        return false;



    }



    return true;



}





bool parse_receiver_module_two_output(const std::string &data,

                                      std::string &u0_line,

                                      std::string &c_line) {



    std::istringstream iss(data);



    if (!std::getline(iss, u0_line)) return false;



    if (u0_line.rfind("error:", 0) == 0) return false;



    if (!std::getline(iss, c_line)) return false;



    if (c_line.rfind("error:", 0) == 0) return false;



    return true;



}





bool parse_sender_module_two_output(const std::string &data,

                                    std::string &u1_line,

                                    std::string &v1_line) {



    std::istringstream iss(data);



    if (!std::getline(iss, u1_line)) return false;



    if (u1_line.rfind("error:", 0) == 0) return false;



    if (!std::getline(iss, v1_line)) return false;



    if (v1_line.rfind("error:", 0) == 0) return false;



    return true;



}







bool parse_receiver_module_four_output(const std::string &data,

                                       std::string &receiver_line,

                                       std::string &sender_line,

                                       std::string &threshold_line,

                                       std::string &intersection_size_line,

                                       std::string &threshold_met_line,

                                       std::string &intersection_line) {



    std::istringstream iss(data);



    if (!std::getline(iss, receiver_line)) return false;

    if (receiver_line.rfind("error:", 0) == 0) return false;



    if (!std::getline(iss, sender_line)) return false;

    if (sender_line.rfind("error:", 0) == 0) return false;



    if (!std::getline(iss, threshold_line)) return false;

    if (threshold_line.rfind("error:", 0) == 0) return false;



    if (!std::getline(iss, intersection_size_line)) return false;

    if (intersection_size_line.rfind("error:", 0) == 0) return false;



    if (!std::getline(iss, threshold_met_line)) return false;

    if (threshold_met_line.rfind("error:", 0) == 0) return false;



    if (!std::getline(iss, intersection_line)) return false;

    if (intersection_line.rfind("error:", 0) == 0) return false;



    return true;



}







bool parse_receiver_module_three_output(const std::string &data,

                                      std::string &hf1_line,

                                      std::string &hk_line,

                                      std::string &equal_line) {



    std::istringstream iss(data);



    if (!std::getline(iss, hf1_line)) return false;



    if (hf1_line.rfind("error:", 0) == 0) return false;



    if (!std::getline(iss, hk_line)) return false;



    if (hk_line.rfind("error:", 0) == 0) return false;



    if (!std::getline(iss, equal_line)) return false;



    if (equal_line.rfind("error:", 0) == 0) return false;



    return true;



}





std::uint16_t random_port() {



    static std::random_device rd;



    static std::mt19937 rng(rd());



    std::uniform_int_distribution<std::uint16_t> dist(40000, 60000);



    return dist(rng);



}







void write_all(int fd, const std::string &data) {



    const char *ptr = data.c_str();



    std::size_t remaining = data.size();



    while (remaining > 0) {



        const ssize_t written = ::write(fd, ptr, remaining);



        if (written <= 0) {



            throw std::runtime_error("write failed");



        }



        remaining -= static_cast<std::size_t>(written);



        ptr += written;



    }



}







std::string read_all(int fd) {



    std::string result;



    char buffer[4096];



    ssize_t n = 0;



    while ((n = ::read(fd, buffer, sizeof(buffer))) > 0) {



        result.append(buffer, buffer + n);



    }



    if (n < 0) {



        throw std::runtime_error("read failed");



    }



    return result;



}







void receiver_process(const tpsi::SessionConfig &cfg,



                      std::uint16_t port,



                      int pipe_fd) {



    try {



        tpsi::MessageChannel channel = tpsi::MessageChannel::listen(port);



        const auto trace = coin_flip_initiator_trace(channel, kCoinFlipSeedBytes);



        tpsi::FiniteField field(cfg.field);



        const auto pseudo = generate_pseudo_elements(cfg, field, trace.shared_seed);







        std::ostringstream oss;



        oss << to_hex(trace.self_seed) << "\n";



        oss << to_hex(trace.peer_seed) << "\n";



        oss << to_hex(trace.shared_seed) << "\n";



        oss << join_uint64(pseudo) << "\n";



        write_all(pipe_fd, oss.str());



    } catch (const std::exception &ex) {



        std::ostringstream oss;



        oss << "error:" << ex.what() << "\n";



        write_all(pipe_fd, oss.str());



    }



}







void sender_process(const tpsi::SessionConfig &cfg,



                    std::uint16_t port,



                    int pipe_fd) {



    try {



        tpsi::MessageChannel channel = tpsi::MessageChannel::connect("127.0.0.1", port);



        const auto trace = coin_flip_responder_trace(channel, kCoinFlipSeedBytes);



        tpsi::FiniteField field(cfg.field);



        const auto pseudo = generate_pseudo_elements(cfg, field, trace.shared_seed);







        std::ostringstream oss;



        oss << join_uint64(pseudo) << "\n";



        write_all(pipe_fd, oss.str());



    } catch (const std::exception &ex) {



        std::ostringstream oss;



        oss << "error:" << ex.what() << "\n";



        write_all(pipe_fd, oss.str());



    }



}





void receiver_process_module_two(const tpsi::SessionConfig &cfg,

                                 std::uint16_t port,

                                 int pipe_fd) {



    try {



        tpsi::MessageChannel channel = tpsi::MessageChannel::listen(port);



        const auto trace = coin_flip_initiator_trace(channel, kCoinFlipSeedBytes);



        tpsi::FiniteField field(cfg.field);



        tpsi::PolynomialRing ring(cfg.pcg.ring);



        tpsi::RingLpnPCG pcg(cfg.pcg);



        auto pseudo = generate_pseudo_elements(cfg, field, trace.shared_seed);



        std::vector<std::uint64_t> receiver_with_pseudo = cfg.receiver_set;



        receiver_with_pseudo.insert(receiver_with_pseudo.end(), pseudo.begin(), pseudo.end());



        tpsi::PolyCoeffs u0_coeffs = tpsi::build_zero_poly(field, receiver_with_pseudo);



        trim_polynomial(u0_coeffs);



        std::vector<std::uint64_t> padded(ring.config().degree, 0);



        for (std::size_t i = 0; i < u0_coeffs.size() && i < padded.size(); ++i) {



            padded[i] = u0_coeffs[i] % field.modulus();



        }



        tpsi::RingElement X = ring.from_coefficients(padded);



        auto correlations = pcg.generate(1);



        if (correlations.empty()) {



            throw std::runtime_error("pcg.generate returned empty correlations");



        }



        tpsi::RandomCorrelation corr = correlations.front();



        std::vector<std::uint8_t> offline_payload;



        tpsi::append_ring_element(offline_payload, corr.a_prime);



        tpsi::append_ring_element(offline_payload, corr.b_prime);



        channel.send(tpsi::MessageType::OfflineData, offline_payload);



        tpsi::RingElement X_star = X.sub(corr.x_prime);



        std::vector<std::uint8_t> xstar_payload;



        tpsi::append_ring_element(xstar_payload, X_star);



        channel.send(tpsi::MessageType::XStar, xstar_payload);



        const auto response_msg = channel.receive();



        if (response_msg.type != tpsi::MessageType::SenderResponse) {



            throw std::runtime_error("expected sender response");



        }



        std::size_t offset = 0;



        tpsi::RingElement A_star =

            tpsi::read_ring_element(cfg.pcg.ring, response_msg.payload, offset);



        tpsi::RingElement B_star =

            tpsi::read_ring_element(cfg.pcg.ring, response_msg.payload, offset);



        (void)tpsi::read_double(response_msg.payload, offset);



        (void)tpsi::read_double(response_msg.payload, offset);



        (void)tpsi::read_bytes(response_msg.payload, offset);



        if (offset != response_msg.payload.size()) {



            throw std::runtime_error("unexpected bytes after sender response");



        }



        tpsi::RingElement ax = A_star.mul(X);



        tpsi::RingElement C_ring = B_star.add(ax).sub(corr.c_prime);



        auto c_coeffs = ring_to_poly_coeffs(field, C_ring);



        trim_polynomial(c_coeffs);



        std::ostringstream oss;



        oss << "u0=" << join_uint64(u0_coeffs) << "\n";



        oss << "C=" << join_uint64(c_coeffs) << "\n";



        write_all(pipe_fd, oss.str());



    } catch (const std::exception &ex) {



        std::ostringstream oss;



        oss << "error:" << ex.what() << "\n";



        write_all(pipe_fd, oss.str());



    }



}





void sender_process_module_two(const tpsi::SessionConfig &cfg,

                               std::uint16_t port,

                               int pipe_fd) {



    try {



        tpsi::MessageChannel channel = tpsi::MessageChannel::connect("127.0.0.1", port);



        const auto trace = coin_flip_responder_trace(channel, kCoinFlipSeedBytes);



        tpsi::FiniteField field(cfg.field);



        tpsi::PolynomialRing ring(cfg.pcg.ring);



        auto pseudo = generate_pseudo_elements(cfg, field, trace.shared_seed);



        const std::uint64_t secret_value = tpsi::uniform_uint64(field.modulus());



        auto shamir_coeffs =

            make_deterministic_threshold_polynomial(field, trace.shared_seed, secret_value, cfg.threshold);



        auto aux_poly = tpsi::build_zero_poly(field, cfg.sender_set);



        auto noise_poly = tpsi::build_zero_poly(field, pseudo);



        const auto random_seed_material = derive_seed_material(trace.shared_seed, kAesSeedBytes, 1);



        auto prng = tpsi::AESCTRPrng::from_seed(random_seed_material);



        auto R_poly = make_deterministic_random_polynomial(prng, field, cfg.receiver_set.size());



        auto u1_coeffs = make_deterministic_random_polynomial(prng, field, cfg.sender_set.size());



        auto mix = tpsi::poly_mul(field, aux_poly, noise_poly);



        mix = tpsi::poly_mul(field, mix, R_poly);



        auto v1_coeffs = tpsi::poly_add(field, mix, shamir_coeffs);



        std::vector<std::uint64_t> padded_u1(ring.config().degree, 0);



        for (std::size_t i = 0; i < u1_coeffs.size() && i < padded_u1.size(); ++i) {



            padded_u1[i] = u1_coeffs[i] % field.modulus();



        }



        std::vector<std::uint64_t> padded_v1(ring.config().degree, 0);



        for (std::size_t i = 0; i < v1_coeffs.size() && i < padded_v1.size(); ++i) {



            padded_v1[i] = v1_coeffs[i] % field.modulus();



        }



        tpsi::RingElement U = ring.from_coefficients(padded_u1);



        tpsi::RingElement V = ring.from_coefficients(padded_v1);



        const auto offline_msg = channel.receive();



        if (offline_msg.type != tpsi::MessageType::OfflineData) {



            throw std::runtime_error("sender expected offline data");



        }



        std::size_t offline_offset = 0;



        tpsi::RingElement a_prime =

            tpsi::read_ring_element(cfg.pcg.ring, offline_msg.payload, offline_offset);



        tpsi::RingElement b_prime =

            tpsi::read_ring_element(cfg.pcg.ring, offline_msg.payload, offline_offset);



        if (offline_offset != offline_msg.payload.size()) {



            throw std::runtime_error("unexpected bytes after offline data");



        }



        const auto xstar_msg = channel.receive();



        if (xstar_msg.type != tpsi::MessageType::XStar) {



            throw std::runtime_error("sender expected XStar message");



        }



        std::size_t xstar_offset = 0;



        tpsi::RingElement X_star =

            tpsi::read_ring_element(cfg.pcg.ring, xstar_msg.payload, xstar_offset);



        if (xstar_offset != xstar_msg.payload.size()) {



            throw std::runtime_error("unexpected bytes after XStar");



        }



        tpsi::RingElement A_star = U.add(a_prime);



        tpsi::RingElement correction = a_prime.mul(X_star);



        tpsi::RingElement B_star = V.add(b_prime).sub(correction);



        std::vector<std::uint8_t> response_payload;



        tpsi::append_ring_element(response_payload, A_star);



        tpsi::append_ring_element(response_payload, B_star);



        tpsi::append_double(response_payload, 0.0);



        tpsi::append_double(response_payload, 0.0);



        auto secret_digest = sm3_digest_uint64(secret_value);



        tpsi::append_bytes(response_payload, secret_digest);



        channel.send(tpsi::MessageType::SenderResponse, response_payload);



        trim_polynomial(u1_coeffs);



        trim_polynomial(v1_coeffs);



        std::ostringstream oss;



        oss << "u1=" << join_uint64(u1_coeffs) << "\n";



        oss << "v1=" << join_uint64(v1_coeffs) << "\n";



        write_all(pipe_fd, oss.str());



    } catch (const std::exception &ex) {



        std::ostringstream oss;



        oss << "error:" << ex.what() << "\n";



        write_all(pipe_fd, oss.str());



    }



}





void receiver_process_module_three(const tpsi::SessionConfig &cfg,

                                   std::uint16_t port,

                                   int pipe_fd) {



    try {



        tpsi::MessageChannel channel = tpsi::MessageChannel::listen(port);



        const auto trace = coin_flip_initiator_trace(channel, kCoinFlipSeedBytes);



        tpsi::FiniteField field(cfg.field);



        tpsi::PolynomialRing ring(cfg.pcg.ring);



        tpsi::RingLpnPCG pcg(cfg.pcg);



        auto pseudo = generate_pseudo_elements(cfg, field, trace.shared_seed);



        std::vector<std::uint64_t> receiver_with_pseudo = cfg.receiver_set;



        receiver_with_pseudo.insert(receiver_with_pseudo.end(), pseudo.begin(), pseudo.end());



        tpsi::PolyCoeffs u0_coeffs = tpsi::build_zero_poly(field, receiver_with_pseudo);



        trim_polynomial(u0_coeffs);



        std::vector<std::uint64_t> padded(ring.config().degree, 0);



        for (std::size_t i = 0; i < u0_coeffs.size() && i < padded.size(); ++i) {



            padded[i] = u0_coeffs[i] % field.modulus();



        }



        tpsi::RingElement X = ring.from_coefficients(padded);



        auto correlations = pcg.generate(1);



        if (correlations.empty()) {



            throw std::runtime_error("pcg.generate returned empty correlations");



        }



        tpsi::RandomCorrelation corr = correlations.front();



        std::vector<std::uint8_t> offline_payload;



        tpsi::append_ring_element(offline_payload, corr.a_prime);



        tpsi::append_ring_element(offline_payload, corr.b_prime);



        channel.send(tpsi::MessageType::OfflineData, offline_payload);



        tpsi::RingElement X_star = X.sub(corr.x_prime);



        std::vector<std::uint8_t> xstar_payload;



        tpsi::append_ring_element(xstar_payload, X_star);



        channel.send(tpsi::MessageType::XStar, xstar_payload);



        const auto response_msg = channel.receive();



        if (response_msg.type != tpsi::MessageType::SenderResponse) {



            throw std::runtime_error("expected sender response");



        }



        std::size_t offset = 0;



        tpsi::RingElement A_star =

            tpsi::read_ring_element(cfg.pcg.ring, response_msg.payload, offset);



        tpsi::RingElement B_star =

            tpsi::read_ring_element(cfg.pcg.ring, response_msg.payload, offset);



        (void)tpsi::read_double(response_msg.payload, offset);



        (void)tpsi::read_double(response_msg.payload, offset);



        std::vector<std::uint8_t> sender_digest = tpsi::read_bytes(response_msg.payload, offset);



        if (offset != response_msg.payload.size()) {



            throw std::runtime_error("unexpected bytes after sender response");



        }



        tpsi::RingElement ax = A_star.mul(X);



        tpsi::RingElement C_ring = B_star.add(ax).sub(corr.c_prime);



        auto recovered_poly = ring_to_poly_coeffs(field, C_ring);



        trim_polynomial(recovered_poly);



        std::vector<tpsi::Share> rss_points;



        rss_points.reserve(receiver_with_pseudo.size());



        for (std::size_t i = 0; i < receiver_with_pseudo.size(); ++i) {



            const std::uint64_t x = receiver_with_pseudo[i];



            const std::uint64_t value = tpsi::poly_eval(field, recovered_poly, x);



            rss_points.push_back({x, value});



        }



        tpsi::RssConfig rss_cfg{.secret_degree = (cfg.threshold == 0) ? 0 : (cfg.threshold - 1),

                                .max_errors = pseudo.size(),

                                .half_gcd_k_small = std::nullopt,

                                .half_gcd_min_drop = std::nullopt,

                                .half_gcd_min_effective_drop = std::nullopt};



        tpsi::PolyCoeffs reconstructed_poly;



        try {



            reconstructed_poly = tpsi::rss_reconstruct(field, rss_points, rss_cfg);



        } catch (const std::exception &ex) {



            std::ostringstream oss;



            oss << "error:" << ex.what() << "\n";



            write_all(pipe_fd, oss.str());



            return;



        }



        if (reconstructed_poly.empty()) {



            throw std::runtime_error("rss reconstruction produced empty polynomial");



        }



        const std::uint64_t reconstructed_secret = reconstructed_poly.front();



        auto hf1_digest = sm3_digest_uint64(reconstructed_secret);



        const bool equal = (hf1_digest == sender_digest);



        std::ostringstream oss;



        oss << "hf1=" << to_hex(hf1_digest) << "\n";



        oss << "hk=" << to_hex(sender_digest) << "\n";



        oss << "equal=" << (equal ? "true" : "false") << "\n";



        write_all(pipe_fd, oss.str());



    } catch (const std::exception &ex) {



        std::ostringstream oss;



        oss << "error:" << ex.what() << "\n";



        write_all(pipe_fd, oss.str());



    }



}







void receiver_process_module_four(const tpsi::SessionConfig &cfg,

                                  std::uint16_t port,

                                  int pipe_fd) {



    try {

        tpsi::MessageChannel channel = tpsi::MessageChannel::listen(port);

        const auto trace = coin_flip_initiator_trace(channel, kCoinFlipSeedBytes);

        tpsi::FiniteField field(cfg.field);

        (void)generate_pseudo_elements(cfg, field, trace.shared_seed);



        std::unordered_set<std::uint64_t> sender_values(cfg.sender_set.begin(),

                                                        cfg.sender_set.end());

        std::vector<std::uint64_t> intersection;

        intersection.reserve(cfg.receiver_set.size());

        for (auto value : cfg.receiver_set) {

            if (sender_values.find(value) != sender_values.end()) {

                intersection.push_back(value);

            }

        }



        const bool threshold_met = intersection.size() >= cfg.threshold;

        const std::size_t reported_intersection_size =

            threshold_met ? intersection.size() : 0;

        std::vector<std::uint64_t> revealed_intersection =

            threshold_met ? intersection : std::vector<std::uint64_t>{};



        std::ostringstream oss;

        oss << "receiver=" << join_uint64(cfg.receiver_set) << "\n";

        oss << "sender=" << join_uint64(cfg.sender_set) << "\n";

        oss << "threshold=" << cfg.threshold << "\n";

        oss << "intersection_size=" << reported_intersection_size << "\n";

        oss << "threshold_met=" << (threshold_met ? "true" : "false") << "\n";

        oss << "intersection=" << join_uint64(revealed_intersection) << "\n";

        write_all(pipe_fd, oss.str());



    } catch (const std::exception &ex) {



        std::ostringstream oss;



        oss << "error:" << ex.what() << "\n";



        write_all(pipe_fd, oss.str());



    }



}





void sender_process_module_four(const tpsi::SessionConfig &cfg,

                                std::uint16_t port,

                                int pipe_fd) {



    try {

        tpsi::MessageChannel channel = tpsi::MessageChannel::connect("127.0.0.1", port);

        const auto trace = coin_flip_responder_trace(channel, kCoinFlipSeedBytes);

        tpsi::FiniteField field(cfg.field);

        (void)generate_pseudo_elements(cfg, field, trace.shared_seed);

        write_all(pipe_fd, std::string("ok\n"));



    } catch (const std::exception &ex) {



        std::ostringstream oss;



        oss << "error:" << ex.what() << "\n";



        write_all(pipe_fd, oss.str());



    }



}







void sender_process_module_three(const tpsi::SessionConfig &cfg,

                                 std::uint16_t port,

                                 int pipe_fd) {



    try {



        tpsi::MessageChannel channel = tpsi::MessageChannel::connect("127.0.0.1", port);



        const auto trace = coin_flip_responder_trace(channel, kCoinFlipSeedBytes);



        tpsi::FiniteField field(cfg.field);



        tpsi::PolynomialRing ring(cfg.pcg.ring);



        auto pseudo = generate_pseudo_elements(cfg, field, trace.shared_seed);



        const std::uint64_t secret_value = tpsi::uniform_uint64(field.modulus());



        auto shamir_coeffs =

            make_deterministic_threshold_polynomial(field, trace.shared_seed, secret_value, cfg.threshold);



        auto aux_poly = tpsi::build_zero_poly(field, cfg.sender_set);



        auto noise_poly = tpsi::build_zero_poly(field, pseudo);



        const auto random_seed_material = derive_seed_material(trace.shared_seed, kAesSeedBytes, 1);



        auto prng = tpsi::AESCTRPrng::from_seed(random_seed_material);



        auto R_poly = make_deterministic_random_polynomial(prng, field, cfg.receiver_set.size());



        auto u1_coeffs = make_deterministic_random_polynomial(prng, field, cfg.sender_set.size());



        auto mix = tpsi::poly_mul(field, aux_poly, noise_poly);



        mix = tpsi::poly_mul(field, mix, R_poly);



        auto v1_coeffs = tpsi::poly_add(field, mix, shamir_coeffs);



        std::vector<std::uint64_t> padded_u1(ring.config().degree, 0);



        for (std::size_t i = 0; i < u1_coeffs.size() && i < padded_u1.size(); ++i) {



            padded_u1[i] = u1_coeffs[i] % field.modulus();



        }



        std::vector<std::uint64_t> padded_v1(ring.config().degree, 0);



        for (std::size_t i = 0; i < v1_coeffs.size() && i < padded_v1.size(); ++i) {



            padded_v1[i] = v1_coeffs[i] % field.modulus();



        }



        tpsi::RingElement U = ring.from_coefficients(padded_u1);



        tpsi::RingElement V = ring.from_coefficients(padded_v1);



        const auto offline_msg = channel.receive();



        if (offline_msg.type != tpsi::MessageType::OfflineData) {



            throw std::runtime_error("sender expected offline data");



        }



        std::size_t offline_offset = 0;



        tpsi::RingElement a_prime =

            tpsi::read_ring_element(cfg.pcg.ring, offline_msg.payload, offline_offset);



        tpsi::RingElement b_prime =

            tpsi::read_ring_element(cfg.pcg.ring, offline_msg.payload, offline_offset);



        if (offline_offset != offline_msg.payload.size()) {



            throw std::runtime_error("unexpected bytes after offline data");



        }



        const auto xstar_msg = channel.receive();



        if (xstar_msg.type != tpsi::MessageType::XStar) {



            throw std::runtime_error("sender expected XStar message");



        }



        std::size_t xstar_offset = 0;



        tpsi::RingElement X_star =

            tpsi::read_ring_element(cfg.pcg.ring, xstar_msg.payload, xstar_offset);



        if (xstar_offset != xstar_msg.payload.size()) {



            throw std::runtime_error("unexpected bytes after XStar");



        }



        tpsi::RingElement A_star = U.add(a_prime);



        tpsi::RingElement correction = a_prime.mul(X_star);



        tpsi::RingElement B_star = V.add(b_prime).sub(correction);



        std::vector<std::uint8_t> response_payload;



        tpsi::append_ring_element(response_payload, A_star);



        tpsi::append_ring_element(response_payload, B_star);



        tpsi::append_double(response_payload, 0.0);



        tpsi::append_double(response_payload, 0.0);



        auto secret_digest = sm3_digest_uint64(secret_value);



        tpsi::append_bytes(response_payload, secret_digest);



        channel.send(tpsi::MessageType::SenderResponse, response_payload);



        (void)pipe_fd;



    } catch (const std::exception &ex) {



        std::ostringstream oss;



        oss << "error:" << ex.what() << "\n";



        write_all(pipe_fd, oss.str());



    }



}







bool parse_receiver_output(const std::string &data,



                           std::string &initiator_seed_hex,



                           std::string &responder_seed_hex,



                           std::string &shared_seed_hex,



                           std::string &receiver_pseudo_line) {



    std::istringstream iss(data);



    if (!std::getline(iss, initiator_seed_hex)) return false;



    if (!std::getline(iss, responder_seed_hex)) return false;



    if (!std::getline(iss, shared_seed_hex)) return false;



    if (!std::getline(iss, receiver_pseudo_line)) return false;



    if (initiator_seed_hex.rfind("error:", 0) == 0 ||



        responder_seed_hex.rfind("error:", 0) == 0 ||



        shared_seed_hex.rfind("error:", 0) == 0 ||



        receiver_pseudo_line.rfind("error:", 0) == 0) {



        return false;



    }



    return true;



}







bool parse_sender_output(const std::string &data,



                         std::string &sender_pseudo_line) {



    std::istringstream iss(data);



    if (!std::getline(iss, sender_pseudo_line)) return false;



    if (sender_pseudo_line.rfind("error:", 0) == 0) {



        return false;



    }



    return true;



}







void run_module_one() {



    std::random_device rd;



    std::mt19937_64 rng(rd());







    for (std::size_t round = 1; round <= kRounds; ++round) {



        const auto sets = build_sets(rng);



        tpsi::SessionConfig cfg;



        cfg.receiver_set = sets.receiver;



        cfg.sender_set = sets.sender;



        cfg.threshold = kThreshold;



        cfg.field.modulus = tpsi::kDefaultFieldModulus;



        cfg.pcg.ring.modulus = tpsi::kDefaultFieldModulus;



        cfg.pcg.ring.degree = tpsi::required_ring_degree(cfg);







        const std::uint16_t port = random_port();







        int receiver_pipe[2];



        int sender_pipe[2];



        if (::pipe(receiver_pipe) != 0 || ::pipe(sender_pipe) != 0) {



            throw std::runtime_error("pipe failed");



        }







        pid_t receiver_pid = ::fork();



        if (receiver_pid < 0) {



            throw std::runtime_error("fork failed");



        }



        if (receiver_pid == 0) {



            ::close(receiver_pipe[0]);



            ::close(sender_pipe[0]);



            ::close(sender_pipe[1]);



            receiver_process(cfg, port, receiver_pipe[1]);



            ::close(receiver_pipe[1]);



            _exit(0);



        }







        ::close(receiver_pipe[1]);







        ::usleep(50000);







        pid_t sender_pid = ::fork();



        if (sender_pid < 0) {



            ::close(receiver_pipe[0]);



            throw std::runtime_error("fork failed");



        }



        if (sender_pid == 0) {



            ::close(sender_pipe[0]);



            sender_process(cfg, port, sender_pipe[1]);



            ::close(sender_pipe[1]);



            _exit(0);



        }







        ::close(sender_pipe[1]);







        const std::string receiver_output = read_all(receiver_pipe[0]);



        const std::string sender_output = read_all(sender_pipe[0]);







        ::close(receiver_pipe[0]);



        ::close(sender_pipe[0]);







        int status = 0;



        ::waitpid(receiver_pid, &status, 0);



        ::waitpid(sender_pid, &status, 0);







        std::string initiator_seed_hex;



        std::string responder_seed_hex;



        std::string shared_seed_hex;



        std::string receiver_pseudo_line;



        std::string sender_pseudo_line;







        if (!parse_receiver_output(receiver_output,



                                   initiator_seed_hex,



                                   responder_seed_hex,



                                   shared_seed_hex,



                                   receiver_pseudo_line)) {



            throw std::runtime_error("receiver output parse error");



        }



        if (!parse_sender_output(sender_output, sender_pseudo_line)) {



            throw std::runtime_error("sender output parse error");



        }







        std::cout << "Round " << round << " Initiator Seed: " << initiator_seed_hex << "\n";



        std::cout << "Round " << round << " Responder Seed: " << responder_seed_hex << "\n";



        std::cout << "Round " << round << " Shared Seed: " << shared_seed_hex << "\n";



        std::cout << "Round " << round << " Receiver Pseudo: " << receiver_pseudo_line << "\n";



        std::cout << "Round " << round << " Sender Pseudo: " << sender_pseudo_line << "\n";



        if (round < kRounds) {



            std::cout << "------------------------------------------------------------\n";



        }



    }



}







void run_module_two() {



    std::random_device rd;



    std::mt19937_64 rng(rd());







    for (std::size_t round = 1; round <= kRounds; ++round) {



        const auto sets = build_sets(rng);



        tpsi::SessionConfig cfg;



        cfg.receiver_set = sets.receiver;



        cfg.sender_set = sets.sender;



        cfg.threshold = kThreshold;



        cfg.field.modulus = tpsi::kDefaultFieldModulus;



        cfg.pcg.ring.modulus = tpsi::kDefaultFieldModulus;



        cfg.pcg.ring.degree = tpsi::required_ring_degree(cfg);







        const std::uint16_t port = random_port();







        int receiver_pipe[2];



        int sender_pipe[2];



        if (::pipe(receiver_pipe) != 0 || ::pipe(sender_pipe) != 0) {



            throw std::runtime_error("pipe failed");



        }







        pid_t receiver_pid = ::fork();



        if (receiver_pid < 0) {



            throw std::runtime_error("fork failed");



        }



        if (receiver_pid == 0) {



            ::close(receiver_pipe[0]);



            ::close(sender_pipe[0]);



            ::close(sender_pipe[1]);



            receiver_process_module_two(cfg, port, receiver_pipe[1]);



            ::close(receiver_pipe[1]);



            _exit(0);



        }







        ::close(receiver_pipe[1]);







        ::usleep(50000);







        pid_t sender_pid = ::fork();



        if (sender_pid < 0) {



            ::close(receiver_pipe[0]);



            throw std::runtime_error("fork failed");



        }



        if (sender_pid == 0) {



            ::close(sender_pipe[0]);



            sender_process_module_two(cfg, port, sender_pipe[1]);



            ::close(sender_pipe[1]);



            _exit(0);



        }







        ::close(sender_pipe[1]);







        const std::string receiver_output = read_all(receiver_pipe[0]);



        const std::string sender_output = read_all(sender_pipe[0]);







        ::close(receiver_pipe[0]);



        ::close(sender_pipe[0]);







        int status = 0;



        ::waitpid(receiver_pid, &status, 0);



        ::waitpid(sender_pid, &status, 0);







        std::string u0_line;



        std::string c_line;



        if (!parse_receiver_module_two_output(receiver_output, u0_line, c_line)) {



            throw std::runtime_error("receiver output parse error");



        }



        std::string u1_line;



        std::string v1_line;



        if (!parse_sender_module_two_output(sender_output, u1_line, v1_line)) {



            throw std::runtime_error("sender output parse error");



        }







        std::vector<std::uint64_t> u0;



        std::vector<std::uint64_t> c_ring;



        std::vector<std::uint64_t> u1;



        std::vector<std::uint64_t> v1;







        if (!parse_vector_line(u0_line, "u0=", u0)) {



            throw std::runtime_error("u0 parse error");



        }



        if (!parse_vector_line(c_line, "C=", c_ring)) {



            throw std::runtime_error("C parse error");



        }



        if (!parse_vector_line(u1_line, "u1=", u1)) {



            throw std::runtime_error("u1 parse error");



        }



        if (!parse_vector_line(v1_line, "v1=", v1)) {



            throw std::runtime_error("v1 parse error");



        }







        tpsi::FiniteField field(cfg.field);



        trim_polynomial(u0);



        trim_polynomial(c_ring);



        trim_polynomial(u1);



        trim_polynomial(v1);







        tpsi::PolyCoeffs product = tpsi::poly_mul(field, u1, u0);



        trim_polynomial(product);



        tpsi::PolyCoeffs expected = tpsi::poly_add(field, product, v1);



        trim_polynomial(expected);







        const bool equal = (expected == c_ring);







        std::cout << "Round " << round << " u0: " << join_uint64_prefix(u0, 5) << "\n";



        std::cout << "Round " << round << " u1: " << join_uint64_prefix(u1, 5) << "\n";



        std::cout << "Round " << round << " v1: " << join_uint64_prefix(v1, 5) << "\n";



        std::cout << "Round " << round << " C_ring: " << join_uint64_prefix(c_ring, 5) << "\n";



        std::cout << "Round " << round << " u1*u0+v1: " << join_uint64_prefix(expected, 5) << "\n";



        std::cout << "Round " << round << " equal: " << std::boolalpha << equal << "\n";







        if (round < kRounds) {



            std::cout << "------------------------------------------------------------\n";



        }



    }



}



void run_module_three() {



    std::random_device rd;



    std::mt19937_64 rng(rd());







    const std::array<std::size_t, 2> thresholds = {kThreshold, kThreshold + 1};



    for (std::size_t t_idx = 0; t_idx < thresholds.size(); ++t_idx) {



        const auto threshold_value = thresholds[t_idx];



        for (std::size_t round = 1; round <= kRounds; ++round) {



            const auto sets = build_sets(rng);



            tpsi::SessionConfig cfg;



            cfg.receiver_set = sets.receiver;



            cfg.sender_set = sets.sender;



            cfg.threshold = threshold_value;



            cfg.field.modulus = tpsi::kDefaultFieldModulus;



            cfg.pcg.ring.modulus = tpsi::kDefaultFieldModulus;



            cfg.pcg.ring.degree = tpsi::required_ring_degree(cfg);







            const std::uint16_t port = random_port();







            int receiver_pipe[2];



            int sender_pipe[2];



            if (::pipe(receiver_pipe) != 0 || ::pipe(sender_pipe) != 0) {



                throw std::runtime_error("pipe failed");



            }







            pid_t receiver_pid = ::fork();



            if (receiver_pid < 0) {



                throw std::runtime_error("fork failed");



            }



            if (receiver_pid == 0) {



                ::close(receiver_pipe[0]);



                ::close(sender_pipe[0]);



                ::close(sender_pipe[1]);



                receiver_process_module_three(cfg, port, receiver_pipe[1]);



                ::close(receiver_pipe[1]);



                _exit(0);



            }







            ::close(receiver_pipe[1]);







            ::usleep(50000);







            pid_t sender_pid = ::fork();



            if (sender_pid < 0) {



                ::close(receiver_pipe[0]);



                throw std::runtime_error("fork failed");



            }



            if (sender_pid == 0) {



                ::close(sender_pipe[0]);



                sender_process_module_three(cfg, port, sender_pipe[1]);



                ::close(sender_pipe[1]);



                _exit(0);



            }







            ::close(sender_pipe[1]);







            const std::string receiver_output = read_all(receiver_pipe[0]);



            const std::string sender_output = read_all(sender_pipe[0]);







            ::close(receiver_pipe[0]);



            ::close(sender_pipe[0]);







            int status = 0;



            ::waitpid(receiver_pid, &status, 0);



            ::waitpid(sender_pid, &status, 0);







            if (receiver_output.rfind("error:", 0) == 0) {

                std::cout << "Threshold " << threshold_value << " Round " << round

                          << " receiver error: " << receiver_output.substr(6) << "\n";

                if (!sender_output.empty() && sender_output.rfind("error:", 0) == 0) {

                    std::cout << "Threshold " << threshold_value << " Round " << round

                              << " sender error: " << sender_output.substr(6) << "\n";

                }

                const bool last_iteration = (round == kRounds) && (t_idx + 1 == thresholds.size());

                if (!last_iteration) {

                    std::cout << "------------------------------------------------------------\n";

                }

                continue;

            }



            if (!sender_output.empty() && sender_output.rfind("error:", 0) == 0) {

                std::cout << "Threshold " << threshold_value << " Round " << round

                          << " sender error: " << sender_output.substr(6) << "\n";

                const bool last_iteration = (round == kRounds) && (t_idx + 1 == thresholds.size());

                if (!last_iteration) {

                    std::cout << "------------------------------------------------------------\n";

                }

                continue;

            }



            std::string hf1_line;

            std::string hk_line;

            std::string equal_line;



            if (!parse_receiver_module_three_output(receiver_output,

                                                    hf1_line,

                                                    hk_line,

                                                    equal_line)) {

                std::cout << "Threshold " << threshold_value << " Round " << round

                          << " receiver output parse error\n";

                const bool last_iteration = (round == kRounds) && (t_idx + 1 == thresholds.size());

                if (!last_iteration) {

                    std::cout << "------------------------------------------------------------\n";

                }

                continue;

            }







            if (hf1_line.rfind("hf1=", 0) != 0) {



                throw std::runtime_error("missing hf1 line");



            }



            if (hk_line.rfind("hk=", 0) != 0) {



                throw std::runtime_error("missing hk line");



            }



            if (equal_line.rfind("equal=", 0) != 0) {



                throw std::runtime_error("missing equal line");



            }







            const std::string hf1_hex = hf1_line.substr(4);



            const std::string hk_hex = hk_line.substr(3);



            const bool equal_value = (hf1_hex == hk_hex);







            std::cout << "Threshold " << threshold_value << " Round " << round

                      << " H(f1(0)): " << hf1_hex << "\n";



            std::cout << "Threshold " << threshold_value << " Round " << round

                      << " H(K): " << hk_hex << "\n";



            std::cout << "Threshold " << threshold_value << " Round " << round

                      << " equal: " << std::boolalpha << equal_value << "\n";







            const bool last_iteration = (round == kRounds) && (t_idx + 1 == thresholds.size());



            if (!last_iteration) {



                std::cout << "------------------------------------------------------------\n";



            }



        }



    }

}



void run_module_four() {



    std::random_device rd;



    std::mt19937_64 rng(rd());



    const std::array<std::size_t, 3> thresholds = {15, 16, 17};



    for (std::size_t idx = 0; idx < thresholds.size(); ++idx) {



        const auto sets = build_sets(rng);



        tpsi::SessionConfig cfg;



        cfg.receiver_set = sets.receiver;



        cfg.sender_set = sets.sender;



        cfg.threshold = thresholds[idx];



        cfg.field.modulus = tpsi::kDefaultFieldModulus;



        cfg.pcg.ring.modulus = tpsi::kDefaultFieldModulus;



        cfg.pcg.ring.degree = tpsi::required_ring_degree(cfg);



        const std::uint16_t port = random_port();



        int receiver_pipe[2];



        int sender_pipe[2];



        if (::pipe(receiver_pipe) != 0 || ::pipe(sender_pipe) != 0) {



            throw std::runtime_error("pipe failed");



        }



        pid_t receiver_pid = ::fork();



        if (receiver_pid < 0) {



            throw std::runtime_error("fork failed");



        }



        if (receiver_pid == 0) {



            ::close(receiver_pipe[0]);



            ::close(sender_pipe[0]);



            ::close(sender_pipe[1]);



            receiver_process_module_four(cfg, port, receiver_pipe[1]);



            ::close(receiver_pipe[1]);



            _exit(0);



        }



        ::close(receiver_pipe[1]);



        ::usleep(50000);



        pid_t sender_pid = ::fork();



        if (sender_pid < 0) {



            ::close(receiver_pipe[0]);



            throw std::runtime_error("fork failed");



        }



        if (sender_pid == 0) {



            ::close(sender_pipe[0]);



            sender_process_module_four(cfg, port, sender_pipe[1]);



            ::close(sender_pipe[1]);



            _exit(0);



        }



        ::close(sender_pipe[1]);



        const std::string receiver_output = read_all(receiver_pipe[0]);



        const std::string sender_output = read_all(sender_pipe[0]);



        ::close(receiver_pipe[0]);



        ::close(sender_pipe[0]);



        int status = 0;



        ::waitpid(receiver_pid, &status, 0);



        ::waitpid(sender_pid, &status, 0);



        std::string receiver_line;



        std::string sender_line;



        std::string threshold_line;



        std::string intersection_size_line;



        std::string threshold_met_line;



        std::string intersection_line;



        if (!parse_receiver_module_four_output(receiver_output,



                                               receiver_line,



                                               sender_line,



                                               threshold_line,



                                               intersection_size_line,



                                               threshold_met_line,



                                               intersection_line)) {



            throw std::runtime_error("receiver output parse error");



        }



        if (!sender_output.empty() && sender_output.rfind("error:", 0) == 0) {



            throw std::runtime_error("sender reported error: " + sender_output.substr(6));



        }



        auto parse_values = [](const std::string &line, const std::string &prefix) {



            if (line.rfind(prefix, 0) != 0) {



                throw std::runtime_error("unexpected line prefix: " + line);



            }



            const std::string body = line.substr(prefix.size());



            auto values = parse_uint64_list(body);



            if (body.empty()) {



                values.clear();



            }



            return values;



        };



        const auto receiver_values = parse_values(receiver_line, "receiver=");



        const auto sender_values = parse_values(sender_line, "sender=");



        if (threshold_line.rfind("threshold=", 0) != 0) {



            throw std::runtime_error("missing threshold line");



        }



        const std::size_t reported_threshold = static_cast<std::size_t>(std::stoull(threshold_line.substr(10)));



        if (intersection_size_line.rfind("intersection_size=", 0) != 0) {



            throw std::runtime_error("missing intersection size line");



        }



        const std::size_t reported_size = static_cast<std::size_t>(std::stoull(intersection_size_line.substr(18)));



        if (threshold_met_line.rfind("threshold_met=", 0) != 0) {



            throw std::runtime_error("missing threshold_met line");



        }



        const bool threshold_met = (threshold_met_line.substr(14) == "true");



        const auto intersection_values = parse_values(intersection_line, "intersection=");



        std::cout << "Round " << (idx + 1) << " Threshold " << reported_threshold << "\n";



        std::cout << "Receiver set: " << join_uint64(receiver_values) << "\n";



        std::cout << "Sender set: " << join_uint64(sender_values) << "\n";



        std::cout << "Intersection size: " << reported_size << "\n";



        std::cout << "Threshold met: " << std::boolalpha << threshold_met << "\n";



        std::cout << "Intersection: " << join_uint64(intersection_values) << "\n";



        if (idx + 1 < thresholds.size()) {



            std::cout << "------------------------------------------------------------\n";



        }



    }



}















} // namespace







int main(int argc, char **argv) {



    if (argc < 2) {



        return 0;



    }



    int module = 0;



    try {



        module = std::stoi(argv[1]);



    } catch (...) {



        return 0;



    }







    switch (module) {



    case 1:



        run_module_one();



        break;



    case 2:



        run_module_two();



        break;



    case 3:



        run_module_three();



        break;



    case 4:



        run_module_four();



        break;



    case 5:



    default:



        break;



    }



    return 0;



}

