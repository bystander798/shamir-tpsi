#pragma once

#include "tpsi/message_channel.hpp"
#include "tpsi/ring.hpp"
#include "tpsi/ring_lpn_pcg.hpp"
#include "tpsi/rss.hpp"
#include "tpsi/shamir.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tpsi {

struct SessionConfig {
    std::vector<std::uint64_t> receiver_set;
    std::vector<std::uint64_t> sender_set;
    std::size_t threshold;
    std::size_t pseudo_count = 0; // if 0 will default to max(0, receiver.size() > threshold)
    FieldConfig field{.modulus = kDefaultFieldModulus};
    PCGConfig pcg{};
};

struct CommunicationStats {
    double sender_time_ms{0.0};
    double receiver_time_ms{0.0};
    std::size_t sender_bytes{0};
    std::size_t receiver_bytes{0};
};

struct SessionResult {
    bool threshold_reached{false};
    std::vector<std::uint64_t> intersection;
    std::uint64_t secret{0};
    CommunicationStats comm;
};

struct ReceiverRunMetrics {
    double coin_flip_ms{0.0};
    double offline_compute_ms{0.0};
    double sender_offline_ms{0.0};
    double receiver_online_compute_ms{0.0};
    double sender_online_compute_ms{0.0};
    double online_comm_ms{0.0};
    double online_total_ms{0.0};
    double rss_reconstruction_ms{0.0};
    double total_protocol_ms{0.0};
    double current_rss_mb{-1.0};
    double max_rss_mb{-1.0};
    std::size_t online_comm_bytes{0};
    std::size_t coin_flip_bytes{0};
    std::size_t pcg_seed_bytes{0};
    std::size_t total_comm_bytes{0};
};

struct ReceiverOutput {
    SessionResult result;
    ReceiverRunMetrics metrics;
};

std::size_t required_ring_degree(const SessionConfig &cfg);

ReceiverOutput run_receiver_session(const SessionConfig &cfg,
                                    MessageChannel channel,
                                    std::size_t coin_flip_seed_bytes = 32);

void run_sender_session(const SessionConfig &cfg,
                        MessageChannel channel,
                        std::size_t coin_flip_seed_bytes = 32);

} // namespace tpsi

