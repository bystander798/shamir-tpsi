#include "tpsi/config_io.hpp"
#include "tpsi/message_channel.hpp"
#include "tpsi/session.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct ReceiverOptions {
    std::string config_path;
    std::string output_path;
    std::uint16_t port{0};
    std::size_t seed_bytes{32};
};

void print_usage(const char *prog) {
    std::cerr << "Usage: " << prog
              << " --config <file> --output <file> --port <port> [--seed-bytes <n>]\n";
}

std::optional<ReceiverOptions> parse_args(int argc, char **argv) {
    ReceiverOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            opts.config_path = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            opts.output_path = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            opts.port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
        } else if (arg == "--seed-bytes" && i + 1 < argc) {
            opts.seed_bytes = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else {
            print_usage(argv[0]);
            return std::nullopt;
        }
    }
    if (opts.config_path.empty() || opts.output_path.empty() || opts.port == 0) {
        print_usage(argv[0]);
        return std::nullopt;
    }
    return opts;
}

void write_output(const tpsi::ReceiverOutput &output, const std::string &path) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to write output file: " + path);
    }
    out << "threshold_reached " << (output.result.threshold_reached ? 1 : 0) << "\n";
    out << "secret " << output.result.secret << "\n";
    out << "intersection_size " << output.result.intersection.size() << "\n";
    for (auto value : output.result.intersection) {
        out << "intersection " << value << "\n";
    }
    out << "sender_bytes " << output.result.comm.sender_bytes << "\n";
    out << "receiver_bytes " << output.result.comm.receiver_bytes << "\n";
    out << "sender_time_ms " << output.result.comm.sender_time_ms << "\n";
    out << "receiver_time_ms " << output.result.comm.receiver_time_ms << "\n";
    out << "coin_flip_ms " << output.metrics.coin_flip_ms << "\n";
    out << "coin_flip_bytes " << output.metrics.coin_flip_bytes << "\n";
    out << "receiver_offline_ms " << output.metrics.offline_compute_ms << "\n";
    out << "sender_offline_ms " << output.metrics.sender_offline_ms << "\n";
    out << "receiver_online_ms " << output.metrics.receiver_online_compute_ms << "\n";
    out << "sender_online_ms " << output.metrics.sender_online_compute_ms << "\n";
    out << "online_comm_ms " << output.metrics.online_comm_ms << "\n";
    out << "online_total_ms " << output.metrics.online_total_ms << "\n";
    out << "rss_reconstruction_ms " << output.metrics.rss_reconstruction_ms << "\n";
    out << "current_rss_mb " << output.metrics.current_rss_mb << "\n";
    out << "max_rss_mb " << output.metrics.max_rss_mb << "\n";
    out << "online_comm_bytes " << output.metrics.online_comm_bytes << "\n";
    out << "pcg_seed_bytes " << output.metrics.pcg_seed_bytes << "\n";
    out << "total_comm_bytes " << output.metrics.total_comm_bytes << "\n";
    out << "total_protocol_ms " << output.metrics.total_protocol_ms << "\n";
}

} // namespace

int main(int argc, char **argv) {
    try {
        auto opts_opt = parse_args(argc, argv);
        if (!opts_opt) {
            return EXIT_FAILURE;
        }
        const auto opts = *opts_opt;

        const tpsi::SessionConfig cfg = tpsi::read_session_config(opts.config_path);
        tpsi::MessageChannel channel = tpsi::MessageChannel::listen(opts.port);
        tpsi::ReceiverOutput result =
            tpsi::run_receiver_session(cfg, std::move(channel), opts.seed_bytes);
        write_output(result, opts.output_path);
        return EXIT_SUCCESS;
    } catch (const std::exception &ex) {
        std::cerr << "[receiver] error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
}
