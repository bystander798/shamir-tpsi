#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tpsi {

struct CoinFlipTranscript {
    std::vector<std::uint8_t> initiator_commit;
    std::vector<std::uint8_t> responder_commit;
    std::vector<std::uint8_t> initiator_reveal;
    std::vector<std::uint8_t> responder_reveal;
};

struct CoinFlipCommitPhase {
    std::vector<std::uint8_t> initiator_commit;
    std::vector<std::uint8_t> responder_commit;
    std::vector<std::uint8_t> initiator_secret;
    std::vector<std::uint8_t> responder_secret;
};

struct CoinFlipResult {
    std::vector<std::uint8_t> shared_seed;
    CoinFlipTranscript transcript;
};

// 模拟基于 SM3 的抛币协议的承诺阶段
CoinFlipCommitPhase simulate_coin_flip_commit(std::size_t seed_length);

// 模拟揭示阶段（需要前一阶段的秘密），返回共享随机种子
CoinFlipResult simulate_coin_flip_reveal(const CoinFlipCommitPhase &commit_phase);

// 一次性执行 commit+reveal，兼容原先 API
CoinFlipResult simulate_coin_flip(std::size_t seed_length);

} // namespace tpsi
