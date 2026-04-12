#include "tpsi/coin_flip.hpp"
#include "tpsi/random_provider.hpp"

#include <openssl/evp.h>

#include <stdexcept>

namespace tpsi {
namespace {

std::vector<std::uint8_t> hash_sm3(const std::vector<std::uint8_t> &data) {
    std::vector<std::uint8_t> digest(EVP_MD_size(EVP_sm3()));
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }
    if (EVP_DigestInit_ex(ctx, EVP_sm3(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data.data(), data.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SM3 digest failed");
    }
    unsigned int len = 0;
    if (EVP_DigestFinal_ex(ctx, digest.data(), &len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }
    digest.resize(len);
    EVP_MD_CTX_free(ctx);
    return digest;
}

std::vector<std::uint8_t> random_seed(std::size_t length) {
    std::vector<std::uint8_t> seed(length);
    for (std::size_t i = 0; i < length; ++i) {
        seed[i] = static_cast<std::uint8_t>(uniform_uint64(256));
    }
    return seed;
}

} // namespace

CoinFlipCommitPhase simulate_coin_flip_commit(std::size_t seed_length) {
    CoinFlipCommitPhase phase;
    phase.initiator_secret = random_seed(seed_length);
    phase.responder_secret = random_seed(seed_length);
    phase.initiator_commit = hash_sm3(phase.initiator_secret);
    phase.responder_commit = hash_sm3(phase.responder_secret);
    return phase;
}

CoinFlipResult simulate_coin_flip_reveal(const CoinFlipCommitPhase &commit_phase) {
    auto initiator_commit = hash_sm3(commit_phase.initiator_secret);
    auto responder_commit = hash_sm3(commit_phase.responder_secret);
    if (initiator_commit != commit_phase.initiator_commit) {
        throw std::runtime_error("coin flip commit mismatch (initiator)");
    }
    if (responder_commit != commit_phase.responder_commit) {
        throw std::runtime_error("coin flip commit mismatch (responder)");
    }

    std::vector<std::uint8_t> combined;
    combined.reserve(commit_phase.initiator_secret.size() + commit_phase.responder_secret.size());
    combined.insert(combined.end(),
                    commit_phase.initiator_secret.begin(),
                    commit_phase.initiator_secret.end());
    combined.insert(combined.end(),
                    commit_phase.responder_secret.begin(),
                    commit_phase.responder_secret.end());

    CoinFlipResult result;
    result.shared_seed = hash_sm3(combined);
    result.transcript.initiator_commit = commit_phase.initiator_commit;
    result.transcript.responder_commit = commit_phase.responder_commit;
    result.transcript.initiator_reveal = commit_phase.initiator_secret;
    result.transcript.responder_reveal = commit_phase.responder_secret;
    return result;
}

CoinFlipResult simulate_coin_flip(std::size_t seed_length) {
    return simulate_coin_flip_reveal(simulate_coin_flip_commit(seed_length));
}

} // namespace tpsi

