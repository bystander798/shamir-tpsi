//平衡场景
#include "tpsi/config_io.hpp"
#include "tpsi/session.hpp"

#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <limits>
#include <unordered_set>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

struct ReceiverReport {
    bool threshold_reached{false};
    std::uint64_t secret{0};
    std::vector<std::uint64_t> intersection;
    std::size_t sender_bytes{0};
    std::size_t receiver_bytes{0};
    double sender_time_ms{0.0};
    double receiver_time_ms{0.0};
    double coin_flip_ms{0.0};
    std::size_t coin_flip_bytes{0};
    double receiver_offline_ms{0.0};
    double sender_offline_ms{0.0};
    double receiver_online_ms{0.0};
    double sender_online_ms{0.0};
    double online_comm_ms{0.0};
    double online_total_ms{0.0};
    std::size_t online_comm_bytes{0};
    std::size_t pcg_seed_bytes{0};
    std::size_t total_comm_bytes{0};
    double rss_reconstruction_ms{0.0};
    double current_rss_mb{-1.0};
    double max_rss_mb{-1.0};
    double total_protocol_ms{0.0};
};

ReceiverReport parse_report(const fs::path &path) {
    ReceiverReport report;
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open receiver report: " + path.string());
    }
    std::string key;
    while (in >> key) {
        if (key == "threshold_reached") {
            int flag = 0;
            in >> flag;
            report.threshold_reached = (flag != 0);
        } else if (key == "secret") {
            in >> report.secret;
        } else if (key == "intersection_size") {
            std::size_t size = 0;
            in >> size;
            report.intersection.reserve(size);
        } else if (key == "intersection") {
            std::uint64_t value = 0;
            in >> value;
            report.intersection.push_back(value);
        } else if (key == "sender_bytes") {
            in >> report.sender_bytes;
        } else if (key == "receiver_bytes") {
            in >> report.receiver_bytes;
        } else if (key == "sender_time_ms") {
            in >> report.sender_time_ms;
        } else if (key == "receiver_time_ms") {
            in >> report.receiver_time_ms;
        } else if (key == "coin_flip_ms") {
            in >> report.coin_flip_ms;
        } else if (key == "coin_flip_bytes") {
            in >> report.coin_flip_bytes;
        } else if (key == "receiver_offline_ms") {
            in >> report.receiver_offline_ms;
        } else if (key == "sender_offline_ms") {
            in >> report.sender_offline_ms;
        } else if (key == "receiver_online_ms") {
            in >> report.receiver_online_ms;
        } else if (key == "sender_online_ms") {
            in >> report.sender_online_ms;
        } else if (key == "online_comm_ms") {
            in >> report.online_comm_ms;
        } else if (key == "online_total_ms") {
            in >> report.online_total_ms;
        } else if (key == "online_comm_bytes") {
            in >> report.online_comm_bytes;
        } else if (key == "pcg_seed_bytes") {
            in >> report.pcg_seed_bytes;
        } else if (key == "total_comm_bytes") {
            in >> report.total_comm_bytes;
        } else if (key == "rss_reconstruction_ms") {
            in >> report.rss_reconstruction_ms;
        } else if (key == "current_rss_mb") {
            in >> report.current_rss_mb;
        } else if (key == "max_rss_mb") {
            in >> report.max_rss_mb;
        } else if (key == "total_protocol_ms") {
            in >> report.total_protocol_ms;
        } else if (key == "communication_ms") {
            in >> report.online_comm_ms;
        } else if (key == "offline_compute_ms") {
            in >> report.receiver_offline_ms;
        } else {
            throw std::runtime_error("unexpected key in receiver report: " + key);
        }
    }
    return report;
}

pid_t spawn_process(const std::vector<std::string> &args) {
    pid_t pid = ::fork();
    if (pid < 0) {
        throw std::runtime_error("fork failed");
    }
    if (pid == 0) {
        std::vector<char *> cargs;
        cargs.reserve(args.size() + 1);
        for (const auto &arg : args) {
            cargs.push_back(const_cast<char *>(arg.c_str()));
        }
        cargs.push_back(nullptr);
        ::execv(args[0].c_str(), cargs.data());
        std::perror("execv");
        _exit(127);
    }
    return pid;
}

void wait_process(pid_t pid, const std::string &name) {
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        throw std::runtime_error("waitpid failed for " + name);
    }
    if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) != 0) {
            std::ostringstream oss;
            oss << name << " exited with status " << WEXITSTATUS(status);
            throw std::runtime_error(oss.str());
        }
    } else if (WIFSIGNALED(status)) {
        std::ostringstream oss;
        oss << name << " terminated by signal " << WTERMSIG(status);
        throw std::runtime_error(oss.str());
    } else {
        throw std::runtime_error(name + " terminated unexpectedly");
    }
}

fs::path create_temp_file(const std::string &prefix) {
    auto temp_dir = fs::temp_directory_path();
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path path = temp_dir / (prefix + std::to_string(now));
    return path;
}

std::uint16_t random_port() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint16_t> dist(40000, 55000);
    return dist(gen);
}

} // namespace

int main(int argc, char **argv) {
    std::size_t set_size_exponent = 16;
    double threshold_ratio = 0.9;

    if (argc > 1) {
        try {
            set_size_exponent = static_cast<std::size_t>(std::stoul(argv[1]));
        } catch (const std::exception &) {
            std::cerr << "Invalid set size exponent argument. Using default value "
                      << set_size_exponent << ".\n";
        }
    }
    if (argc > 2) {
        try {
            threshold_ratio = std::stod(argv[2]);
        } catch (const std::exception &) {
            std::cerr << "Invalid threshold ratio argument. Using default value "
                      << threshold_ratio << ".\n";
        }
    }

    if (set_size_exponent >= std::numeric_limits<std::size_t>::digits) {
        std::cerr << "Set size exponent is too large, clamping to "
                  << (std::numeric_limits<std::size_t>::digits - 1) << ".\n";
        set_size_exponent = std::numeric_limits<std::size_t>::digits - 1;
    }
    if (threshold_ratio <= 0.0) {
        std::cerr << "Threshold ratio must be positive. Using minimum value 1e-9.\n";
        threshold_ratio = 1e-9;
    }

    const std::size_t set_size = static_cast<std::size_t>(1) << set_size_exponent;

    std::random_device rd;
    std::mt19937_64 rng(rd());
    const std::uint64_t modulus = tpsi::kDefaultFieldModulus;
    std::uniform_int_distribution<std::uint64_t> dist(0, modulus - 1);
    std::unordered_set<std::uint64_t> used;
    used.reserve((set_size + 1) * 2);
    auto draw_unique = [&]() -> std::uint64_t {
        while (true) {
            const std::uint64_t value = dist(rng);
            if (used.insert(value).second) {
                return value;
            }
        }
    };

    std::vector<std::uint64_t> base(set_size + 1);
    for (auto &value : base) {
        value = draw_unique();
    }
    std::vector<std::uint64_t> receiver_set(base.begin(), base.begin() + set_size);
    std::vector<std::uint64_t> sender_set(base.begin() + 1, base.end());

    tpsi::SessionConfig cfg;
    cfg.receiver_set = receiver_set;
    cfg.sender_set = sender_set;
    cfg.threshold = static_cast<std::size_t>(std::floor(threshold_ratio * static_cast<double>(set_size)));
    cfg.pcg.ring.modulus = tpsi::kDefaultFieldModulus;
    cfg.pcg.ring.degree = tpsi::required_ring_degree(cfg);

    const fs::path config_path = create_temp_file("tpsi_bench_config_");
    const fs::path report_path = create_temp_file("tpsi_bench_report_");

    tpsi::write_session_config(cfg, config_path.string());

    const fs::path bench_exe = fs::absolute(argv[0]);
    const fs::path build_root = bench_exe.parent_path().parent_path();
    const fs::path receiver_exe = build_root / "tpsi_receiver";
    const fs::path sender_exe = build_root / "tpsi_sender";

    const std::uint16_t port = random_port();

    std::vector<std::string> receiver_args = {
        receiver_exe.string(),
        "--config", config_path.string(),
        "--output", report_path.string(),
        "--port", std::to_string(port)
    };
    std::vector<std::string> sender_args = {
        sender_exe.string(),
        "--config", config_path.string(),
        "--port", std::to_string(port)
    };

    pid_t receiver_pid = spawn_process(receiver_args);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    pid_t sender_pid = spawn_process(sender_args);

    try {
        wait_process(sender_pid, "sender");
        wait_process(receiver_pid, "receiver");
    } catch (...) {
        ::kill(receiver_pid, SIGTERM);
        ::kill(sender_pid, SIGTERM);
        fs::remove(config_path);
        fs::remove(report_path);
        throw;
    }

    ReceiverReport report = parse_report(report_path);

    fs::remove(config_path);
    fs::remove(report_path);

    const double receiver_online_ms =
        report.receiver_online_ms != 0.0 ? report.receiver_online_ms : report.receiver_time_ms;
    const double sender_online_ms =
        report.sender_online_ms != 0.0 ? report.sender_online_ms : report.sender_time_ms;
    const double receiver_offline_ms = report.receiver_offline_ms;
    const double sender_offline_ms = report.sender_offline_ms;
    const double online_comm_ms = report.online_comm_ms;
    const std::size_t online_comm_bytes = report.online_comm_bytes;
    const std::size_t coin_flip_bytes = report.coin_flip_bytes;
    const std::size_t pcg_seed_bytes = report.pcg_seed_bytes;
    const double online_total_ms =
        report.online_total_ms != 0.0
            ? report.online_total_ms
            : (receiver_online_ms + sender_online_ms + online_comm_ms);
    const std::size_t total_comm_bytes =
        report.total_comm_bytes != 0
            ? report.total_comm_bytes
            : (report.sender_bytes + report.receiver_bytes + coin_flip_bytes + pcg_seed_bytes);

    std::cout << "==== TPSI Two-Process Benchmark ====\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Coin flip time: " << report.coin_flip_ms << " ms\n";
    std::cout << "Receiver precomputation time: " << receiver_offline_ms << " ms\n";
    std::cout << "Sender precomputation time: " << sender_offline_ms << " ms\n";
    std::cout << "Receiver online compute time: " << receiver_online_ms << " ms\n";
    std::cout << "Sender online compute time: " << sender_online_ms << " ms\n";
    std::cout << "Online communication time: " << online_comm_ms << " ms\n";
    std::cout << "Online total time: " << online_total_ms << " ms\n";
    std::cout << "Online communication volume (bytes): " << online_comm_bytes << "\n";
    std::cout << "RSS reconstruction time: " << report.rss_reconstruction_ms << " ms\n";
    std::cout << "RSS memory usage (current/max MB): " << report.current_rss_mb << " / "
              << report.max_rss_mb << "\n";
    std::cout << "Total protocol time: " << report.total_protocol_ms << " ms\n";
    std::cout << "Total communication bytes (all phases): " << total_comm_bytes << "\n";
    std::cout << "Threshold reached: " << std::boolalpha << report.threshold_reached << "\n";
    std::cout << "Intersection size: " << report.intersection.size() << "\n";

    return 0;
}
//非平衡场景
/*#include "tpsi/config_io.hpp"
#include "tpsi/session.hpp"

#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <limits>
#include <unordered_set>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

struct ReceiverReport {
    bool threshold_reached{false};
    std::uint64_t secret{0};
    std::vector<std::uint64_t> intersection;
    std::size_t sender_bytes{0};
    std::size_t receiver_bytes{0};
    double sender_time_ms{0.0};
    double receiver_time_ms{0.0};
    double coin_flip_ms{0.0};
    std::size_t coin_flip_bytes{0};
    double receiver_offline_ms{0.0};
    double sender_offline_ms{0.0};
    double receiver_online_ms{0.0};
    double sender_online_ms{0.0};
    double online_comm_ms{0.0};
    double online_total_ms{0.0};
    std::size_t online_comm_bytes{0};
    std::size_t pcg_seed_bytes{0};
    std::size_t total_comm_bytes{0};
    double rss_reconstruction_ms{0.0};
    double current_rss_mb{-1.0};
    double max_rss_mb{-1.0};
    double total_protocol_ms{0.0};
};

ReceiverReport parse_report(const fs::path &path) {
    ReceiverReport report;
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open receiver report: " + path.string());
    }
    std::string key;
    while (in >> key) {
        if (key == "threshold_reached") {
            int flag = 0;
            in >> flag;
            report.threshold_reached = (flag != 0);
        } else if (key == "secret") {
            in >> report.secret;
        } else if (key == "intersection_size") {
            std::size_t size = 0;
            in >> size;
            report.intersection.reserve(size);
        } else if (key == "intersection") {
            std::uint64_t value = 0;
            in >> value;
            report.intersection.push_back(value);
        } else if (key == "sender_bytes") {
            in >> report.sender_bytes;
        } else if (key == "receiver_bytes") {
            in >> report.receiver_bytes;
        } else if (key == "sender_time_ms") {
            in >> report.sender_time_ms;
        } else if (key == "receiver_time_ms") {
            in >> report.receiver_time_ms;
        } else if (key == "coin_flip_ms") {
            in >> report.coin_flip_ms;
        } else if (key == "coin_flip_bytes") {
            in >> report.coin_flip_bytes;
        } else if (key == "receiver_offline_ms") {
            in >> report.receiver_offline_ms;
        } else if (key == "sender_offline_ms") {
            in >> report.sender_offline_ms;
        } else if (key == "receiver_online_ms") {
            in >> report.receiver_online_ms;
        } else if (key == "sender_online_ms") {
            in >> report.sender_online_ms;
        } else if (key == "online_comm_ms") {
            in >> report.online_comm_ms;
        } else if (key == "online_total_ms") {
            in >> report.online_total_ms;
        } else if (key == "online_comm_bytes") {
            in >> report.online_comm_bytes;
        } else if (key == "pcg_seed_bytes") {
            in >> report.pcg_seed_bytes;
        } else if (key == "total_comm_bytes") {
            in >> report.total_comm_bytes;
        } else if (key == "rss_reconstruction_ms") {
            in >> report.rss_reconstruction_ms;
        } else if (key == "current_rss_mb") {
            in >> report.current_rss_mb;
        } else if (key == "max_rss_mb") {
            in >> report.max_rss_mb;
        } else if (key == "total_protocol_ms") {
            in >> report.total_protocol_ms;
        } else if (key == "communication_ms") {
            in >> report.online_comm_ms;
        } else if (key == "offline_compute_ms") {
            in >> report.receiver_offline_ms;
        } else {
            throw std::runtime_error("unexpected key in receiver report: " + key);
        }
    }
    return report;
}

pid_t spawn_process(const std::vector<std::string> &args) {
    pid_t pid = ::fork();
    if (pid < 0) {
        throw std::runtime_error("fork failed");
    }
    if (pid == 0) {
        std::vector<char *> cargs;
        cargs.reserve(args.size() + 1);
        for (const auto &arg : args) {
            cargs.push_back(const_cast<char *>(arg.c_str()));
        }
        cargs.push_back(nullptr);
        ::execv(args[0].c_str(), cargs.data());
        std::perror("execv");
        _exit(127);
    }
    return pid;
}

void wait_process(pid_t pid, const std::string &name) {
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        throw std::runtime_error("waitpid failed for " + name);
    }
    if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) != 0) {
            std::ostringstream oss;
            oss << name << " exited with status " << WEXITSTATUS(status);
            throw std::runtime_error(oss.str());
        }
    } else if (WIFSIGNALED(status)) {
        std::ostringstream oss;
        oss << name << " terminated by signal " << WTERMSIG(status);
        throw std::runtime_error(oss.str());
    } else {
        throw std::runtime_error(name + " terminated unexpectedly");
    }
}

fs::path create_temp_file(const std::string &prefix) {
    auto temp_dir = fs::temp_directory_path();
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path path = temp_dir / (prefix + std::to_string(now));
    return path;
}

std::uint16_t random_port() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint16_t> dist(40000, 55000);
    return dist(gen);
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <receiver_size> <sender_size>\n";
        return 1;
    }

    std::size_t receiver_size = 0;
    std::size_t sender_size = 0;

    try {
        receiver_size = static_cast<std::size_t>(std::stoull(argv[1]));
    } catch (const std::exception &) {
        std::cerr << "Invalid receiver size argument.\n";
        return 1;
    }

    try {
        sender_size = static_cast<std::size_t>(std::stoull(argv[2]));
    } catch (const std::exception &) {
        std::cerr << "Invalid sender size argument.\n";
        return 1;
    }

    if (receiver_size == 0) {
        std::cerr << "Receiver size must be positive.\n";
        return 1;
    }
    if (sender_size <= receiver_size) {
        std::cerr << "Sender size must be greater than receiver size.\n";
        return 1;
    }

    std::vector<std::uint64_t> receiver_set;
    receiver_set.reserve(receiver_size);
    for (std::size_t value = 1; value <= receiver_size; ++value) {
        receiver_set.push_back(static_cast<std::uint64_t>(value));
    }

    std::vector<std::uint64_t> sender_set;
    sender_set.reserve(sender_size);
    sender_set.insert(sender_set.end(), receiver_set.begin(), receiver_set.end());

    std::unordered_set<std::uint64_t> used(receiver_set.begin(), receiver_set.end());
    used.reserve(sender_size * 2);

    std::random_device rd;
    std::mt19937_64 rng(rd());
    std::uniform_int_distribution<std::uint64_t> dist(1, tpsi::kDefaultFieldModulus - 1);

    while (sender_set.size() < sender_size) {
        const std::uint64_t candidate = dist(rng);
        if (used.insert(candidate).second) {
            sender_set.push_back(candidate);
        }
    }

    tpsi::SessionConfig cfg;
    cfg.receiver_set = receiver_set;
    cfg.sender_set = sender_set;
    cfg.threshold = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::floor(0.3 * static_cast<double>(receiver_size))));
    cfg.pcg.ring.modulus = tpsi::kDefaultFieldModulus;
    cfg.pcg.ring.degree = tpsi::required_ring_degree(cfg);

    const fs::path config_path = create_temp_file("tpsi_bench_config_");
    const fs::path report_path = create_temp_file("tpsi_bench_report_");

    tpsi::write_session_config(cfg, config_path.string());

    const fs::path bench_exe = fs::absolute(argv[0]);
    const fs::path build_root = bench_exe.parent_path().parent_path();
    const fs::path receiver_exe = build_root / "tpsi_receiver";
    const fs::path sender_exe = build_root / "tpsi_sender";

    const std::uint16_t port = random_port();

    std::vector<std::string> receiver_args = {
        receiver_exe.string(),
        "--config", config_path.string(),
        "--output", report_path.string(),
        "--port", std::to_string(port)
    };
    std::vector<std::string> sender_args = {
        sender_exe.string(),
        "--config", config_path.string(),
        "--port", std::to_string(port)
    };

    pid_t receiver_pid = spawn_process(receiver_args);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    pid_t sender_pid = spawn_process(sender_args);

    try {
        wait_process(sender_pid, "sender");
        wait_process(receiver_pid, "receiver");
    } catch (...) {
        ::kill(receiver_pid, SIGTERM);
        ::kill(sender_pid, SIGTERM);
        fs::remove(config_path);
        fs::remove(report_path);
        throw;
    }

    ReceiverReport report = parse_report(report_path);

    fs::remove(config_path);
    fs::remove(report_path);

    const double receiver_online_ms =
        report.receiver_online_ms != 0.0 ? report.receiver_online_ms : report.receiver_time_ms;
    const double sender_online_ms =
        report.sender_online_ms != 0.0 ? report.sender_online_ms : report.sender_time_ms;
    const double receiver_offline_ms = report.receiver_offline_ms;
    const double sender_offline_ms = report.sender_offline_ms;
    const double online_comm_ms = report.online_comm_ms;
    const std::size_t online_comm_bytes = report.online_comm_bytes;
    const std::size_t coin_flip_bytes = report.coin_flip_bytes;
    const std::size_t pcg_seed_bytes = report.pcg_seed_bytes;
    const double online_total_ms =
        report.online_total_ms != 0.0
            ? report.online_total_ms
            : (receiver_online_ms + sender_online_ms + online_comm_ms);
    const std::size_t total_comm_bytes =
        report.total_comm_bytes != 0
            ? report.total_comm_bytes
            : (report.sender_bytes + report.receiver_bytes + coin_flip_bytes + pcg_seed_bytes);

    std::cout << "==== TPSI Two-Process Benchmark ====\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Coin flip time: " << report.coin_flip_ms << " ms\n";
    std::cout << "Receiver precomputation time: " << receiver_offline_ms << " ms\n";
    std::cout << "Sender precomputation time: " << sender_offline_ms << " ms\n";
    std::cout << "Receiver online compute time: " << receiver_online_ms << " ms\n";
    std::cout << "Sender online compute time: " << sender_online_ms << " ms\n";
    std::cout << "Online communication time: " << online_comm_ms << " ms\n";
    std::cout << "Online total time: " << online_total_ms << " ms\n";
    std::cout << "Online communication volume (bytes): " << online_comm_bytes << "\n";
    std::cout << "RSS reconstruction time: " << report.rss_reconstruction_ms << " ms\n";
    std::cout << "RSS memory usage (current/max MB): " << report.current_rss_mb << " / "
              << report.max_rss_mb << "\n";
    std::cout << "Total protocol time: " << report.total_protocol_ms << " ms\n";
    std::cout << "Total communication bytes (all phases): " << total_comm_bytes << "\n";
    std::cout << "Threshold reached: " << std::boolalpha << report.threshold_reached << "\n";
    std::cout << "Intersection size: " << report.intersection.size() << "\n";

    return 0;
}*/