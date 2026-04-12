#include "tpsi/config_io.hpp"
#include "tpsi/message_channel.hpp"
#include "tpsi/session.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct SenderOptions {
    std::string config_path;
    std::string host{"127.0.0.1"};
    std::uint16_t port{0};
    std::size_t seed_bytes{32};
};

void print_usage(const char *prog) {
    std::cerr << "Usage: " << prog
              << " --config <file> --port <port> [--host <addr>] [--seed-bytes <n>]\n";
}

std::optional<SenderOptions> parse_args(int argc, char **argv) {
    SenderOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            opts.config_path = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            opts.port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
        } else if (arg == "--host" && i + 1 < argc) {
            opts.host = argv[++i];
        } else if (arg == "--seed-bytes" && i + 1 < argc) {
            opts.seed_bytes = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else {
            print_usage(argv[0]);
            return std::nullopt;
        }
    }
    if (opts.config_path.empty() || opts.port == 0) {
        print_usage(argv[0]);
        return std::nullopt;
    }
    return opts;
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
        tpsi::MessageChannel channel = tpsi::MessageChannel::connect(opts.host, opts.port);
        tpsi::run_sender_session(cfg, std::move(channel), opts.seed_bytes);
        return EXIT_SUCCESS;
    } catch (const std::exception &ex) {
        std::cerr << "[sender] error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
}

