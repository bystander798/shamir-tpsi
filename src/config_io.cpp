#include "tpsi/config_io.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace tpsi {

namespace {

void expect_key(const std::string &line, const std::string &expected) {
    if (!line.starts_with(expected + " ")) {
        throw std::runtime_error("expected key '" + expected + "' but got '" + line + "'");
    }
}

std::uint64_t parse_u64(const std::string &line, const std::string &key) {
    expect_key(line, key);
    return std::stoull(line.substr(key.size() + 1));
}

std::size_t parse_size(const std::string &line, const std::string &key) {
    return static_cast<std::size_t>(parse_u64(line, key));
}

bool parse_bool(const std::string &line, const std::string &key) {
    const auto value = parse_u64(line, key);
    if (value > 1) {
        throw std::runtime_error("boolean key '" + key + "' must be 0 or 1");
    }
    return value == 1;
}

} // namespace

SessionConfig read_session_config(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open config file: " + path);
    }
    std::string line;
    auto getline = [&]() -> std::string {
        if (!std::getline(in, line)) {
            throw std::runtime_error("unexpected end of config file");
        }
        return line;
    };

    SessionConfig cfg;
    cfg.field.modulus = parse_u64(getline(), "field_modulus");
    cfg.pcg.ring.degree = parse_size(getline(), "ring_degree");
    cfg.pcg.ring.modulus = parse_u64(getline(), "ring_modulus");
    cfg.pcg.sparse_weight = parse_size(getline(), "pcg_sparse_weight");
    cfg.pcg.verify = parse_bool(getline(), "pcg_verify");
    cfg.threshold = parse_size(getline(), "threshold");
    cfg.pseudo_count = parse_size(getline(), "pseudo_count");

    const auto receiver_count = parse_size(getline(), "receiver_count");
    cfg.receiver_set.resize(receiver_count);
    for (std::size_t i = 0; i < receiver_count; ++i) {
        cfg.receiver_set[i] = parse_u64(getline(), "receiver");
    }

    const auto sender_count = parse_size(getline(), "sender_count");
    cfg.sender_set.resize(sender_count);
    for (std::size_t i = 0; i < sender_count; ++i) {
        cfg.sender_set[i] = parse_u64(getline(), "sender");
    }
    return cfg;
}

void write_session_config(const SessionConfig &cfg, const std::string &path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to write config file: " + path);
    }
    out << "field_modulus " << cfg.field.modulus << "\n";
    out << "ring_degree " << cfg.pcg.ring.degree << "\n";
    out << "ring_modulus " << cfg.pcg.ring.modulus << "\n";
    out << "pcg_sparse_weight " << cfg.pcg.sparse_weight << "\n";
    out << "pcg_verify " << (cfg.pcg.verify ? 1 : 0) << "\n";
    out << "threshold " << cfg.threshold << "\n";
    out << "pseudo_count " << cfg.pseudo_count << "\n";
    out << "receiver_count " << cfg.receiver_set.size() << "\n";
    for (auto value : cfg.receiver_set) {
        out << "receiver " << value << "\n";
    }
    out << "sender_count " << cfg.sender_set.size() << "\n";
    for (auto value : cfg.sender_set) {
        out << "sender " << value << "\n";
    }
}

} // namespace tpsi

