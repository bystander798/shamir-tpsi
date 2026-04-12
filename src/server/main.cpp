#include "httplib.h"
#include "nlohmann_json.hpp"
#include "tpsi/message_channel.hpp"
#include "tpsi/session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <cmath>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <optional>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <limits>
#include <regex>

using json = nlohmann::json;

namespace {

struct Dataset {
    std::string id;
    std::string owner;
    std::string name;
    std::vector<std::uint64_t> elements;
    std::uint64_t modulus;
    std::string version;
    std::string updated_at;
};

struct LogEntry {
    std::string time;
    std::string level;
    std::string message;
};

struct SessionRecord {
    std::string id;
    std::string receiver_dataset;
    std::string sender_dataset;
    std::string receiver_account;
    std::string sender_account;
    std::string request_id;
    std::string status;
    std::size_t receiver_size{0};
    std::size_t sender_size{0};
    std::size_t threshold{0};
    double threshold_ratio{0.95};
    std::uint64_t modulus{0};
    std::string notes;
    std::string created_at;
    std::string updated_at;
    double offline_ms{0.0};
    double online_ms{0.0};
    double finalize_ms{0.0};
    std::size_t communication_bytes{0};
    std::size_t receiver_bytes{0};
    std::size_t sender_bytes{0};
    std::size_t coin_flip_bytes{0};
    std::size_t pcg_seed_bytes{0};
    std::size_t online_comm_bytes{0};
    bool threshold_reached{false};
    double receiver_offline_ms{0.0};
    double sender_offline_ms{0.0};
    double receiver_online_ms{0.0};
    double sender_online_ms{0.0};
    double online_comm_ms{0.0};
    double coin_flip_ms{0.0};
    double total_protocol_ms{0.0};
    std::vector<std::uint64_t> intersection;
    std::vector<LogEntry> logs;
};

struct RequestRecord {
    std::string id;
    std::string applicant;
    std::string receiver_dataset_id;
    std::string counterparty;
    std::string sender_dataset_id;
    std::string status;
    std::size_t threshold{0};
    double threshold_ratio{0.95};
    std::uint64_t modulus{0};
    std::string notes;
    std::string created_at;
    std::string session_id;
    std::string error_message;
    std::string updated_at;
};

struct Notifications {
    bool approvals{true};
    bool jobs{true};
    bool alerts{true};
};

struct UserRecord {
    std::string id;
    std::string username;
    std::string password;
    std::string role;
    std::string name;
    std::string organization;
    std::string phone;
    std::string email;
    std::string avatar_url;
    bool two_factor_enabled{false};
    Notifications notifications;
    std::string created_at;
    std::string updated_at;
};

std::mutex g_mutex;
std::unordered_map<std::string, UserRecord> g_users;
std::unordered_map<std::string, std::string> g_username_index;
std::unordered_map<std::string, std::string> g_tokens;
std::unordered_map<std::string, Dataset> g_datasets;
std::unordered_map<std::string, SessionRecord> g_sessions;
std::unordered_map<std::string, RequestRecord> g_requests;

constexpr const char *kStorageDir = "data";
constexpr const char *kStoragePath = "data/state.json";
std::once_flag g_storage_dir_once;

void ensure_storage_dir() {
    std::call_once(g_storage_dir_once, []() {
        try {
            std::filesystem::create_directories(kStorageDir);
        } catch (...) {
            // best-effort; real writes will fail later if path invalid
        }
    });
}

struct AuthContext {
    std::string user_id;
    UserRecord user;
};

std::string now_iso_string();
std::string generate_id(const std::string &prefix);
std::optional<std::string> extract_bearer_token(const httplib::Request &req);

json dataset_to_storage_json(const Dataset &ds) {
    return json{
        {"id", ds.id},
        {"owner", ds.owner},
        {"name", ds.name},
        {"elements", ds.elements},
        {"modulus", ds.modulus},
        {"version", ds.version},
        {"updatedAt", ds.updated_at}
    };
}

std::string trim_copy(const std::string &value) {
    auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

bool ends_with_ignore_case(const std::string &value, const std::string &suffix) {
    if (value.size() < suffix.size()) return false;
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        char a = static_cast<char>(std::tolower(static_cast<unsigned char>(value[value.size() - suffix.size() + i])));
        char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

std::uint64_t normalize_mod(std::int64_t value, std::uint64_t modulus) {
    if (modulus == 0) {
        if (value < 0) {
            throw std::runtime_error("elements cannot be negative when modulus is zero");
        }
        return static_cast<std::uint64_t>(value);
    }
    auto mod = static_cast<std::int64_t>(modulus);
    auto rem = static_cast<std::int64_t>(value % mod);
    if (rem < 0) rem += mod;
    return static_cast<std::uint64_t>(rem);
}

std::uint64_t parse_modulus_or_default(const std::string &text, std::uint64_t fallback) {
    auto trimmed = trim_copy(text);
    if (trimmed.empty()) return fallback;
    try {
        size_t idx = 0;
        unsigned long long parsed = std::stoull(trimmed, &idx, 10);
        if (idx != trimmed.size()) {
            throw std::runtime_error("invalid modulus value");
        }
        if (parsed == 0) {
            throw std::runtime_error("modulus must be greater than zero");
        }
        if (parsed > std::numeric_limits<std::uint64_t>::max()) {
            throw std::runtime_error("modulus is too large");
        }
        return static_cast<std::uint64_t>(parsed);
    } catch (const std::invalid_argument &) {
        throw std::runtime_error("invalid modulus value");
    } catch (const std::out_of_range &) {
        throw std::runtime_error("modulus is out of range");
    }
}

std::vector<std::uint64_t> parse_elements_array(const json &arr, std::uint64_t modulus) {
    if (!arr.is_array()) {
        throw std::runtime_error("elements field must be an array");
    }
    std::vector<std::uint64_t> elements;
    elements.reserve(arr.size());
    for (const auto &item : arr) {
        try {
            if (item.is_number_integer() || item.is_number_unsigned()) {
                auto value = item.get<std::int64_t>();
                elements.push_back(normalize_mod(value, modulus));
            } else if (item.is_number_float()) {
                auto value = item.get<double>();
                auto truncated = static_cast<std::int64_t>(value >= 0 ? std::floor(value) : std::ceil(value));
                elements.push_back(normalize_mod(truncated, modulus));
            } else if (item.is_string()) {
                auto text = item.get<std::string>();
                size_t idx = 0;
                try {
                    auto parsed = std::stoll(text, &idx, 10);
                    if (idx == text.size()) {
                        elements.push_back(normalize_mod(parsed, modulus));
                        continue;
                    }
                } catch (...) {
                    // fall through
                }
                throw std::runtime_error("elements array contains non-numeric string");
            } else {
                throw std::runtime_error("elements array contains unsupported type");
            }
        } catch (const std::out_of_range &) {
            throw std::runtime_error("element value out of range");
        }
    }
    return elements;
}

std::vector<std::uint64_t> parse_numbers_from_text(const std::string &content, std::uint64_t modulus) {
    static const std::regex pattern(R"([-+]?\d+(?:\.\d+)?)");
    std::vector<std::uint64_t> elements;
    for (auto it = std::sregex_iterator(content.begin(), content.end(), pattern); it != std::sregex_iterator(); ++it) {
        auto token = it->str();
        auto dot_pos = token.find_first_of(".eE");
        if (dot_pos != std::string::npos) {
            token = token.substr(0, dot_pos);
        }
        token = trim_copy(token);
        if (token.empty() || token == "+" || token == "-") continue;
        try {
            size_t idx = 0;
            std::int64_t value = 0;
            try {
                value = std::stoll(token, &idx, 10);
            } catch (const std::out_of_range &) {
                unsigned long long uvalue = std::stoull(token, &idx, 10);
                if (idx != token.size()) continue;
                if (modulus == 0) {
                    if (uvalue > std::numeric_limits<std::uint64_t>::max()) {
                        throw std::runtime_error("element value out of range");
                    }
                    elements.push_back(static_cast<std::uint64_t>(uvalue));
                    continue;
                }
                elements.push_back(static_cast<std::uint64_t>(uvalue % modulus));
                continue;
            }
            if (idx != token.size()) continue;
            elements.push_back(normalize_mod(value, modulus));
        } catch (const std::invalid_argument &) {
            continue;
        } catch (const std::out_of_range &) {
            throw std::runtime_error("element value out of range");
        }
    }
    return elements;
}

std::vector<std::uint64_t> parse_elements_from_string(const std::string &raw, std::uint64_t modulus, bool treat_as_json_first = true) {
    auto text = trim_copy(raw);
    if (text.empty()) return {};
    if (treat_as_json_first) {
        try {
            auto value = json::parse(text);
            if (value.is_array()) {
                return parse_elements_array(value, modulus);
            }
            if (value.is_object() && value.contains("elements")) {
                return parse_elements_array(value["elements"], modulus);
            }
        } catch (...) {
            // fall back to text parsing
        }
    }
    return parse_numbers_from_text(text, modulus);
}

std::vector<std::uint64_t> parse_dataset_file_content(const std::string &filename,
                                                      const std::string &content,
                                                      std::uint64_t modulus) {
    if (content.empty()) {
        throw std::runtime_error("dataset file is empty");
    }
    if (ends_with_ignore_case(filename, ".json")) {
        try {
            auto parsed = json::parse(content);
            if (parsed.is_array()) {
                return parse_elements_array(parsed, modulus);
            }
            if (parsed.is_object() && parsed.contains("elements")) {
                return parse_elements_array(parsed["elements"], modulus);
            }
            throw std::runtime_error("JSON 文件应为数组或包含 elements 数组");
        } catch (const json::parse_error &) {
            // JSON 解析失败时尝试按纯文本解析，方便用户直接上传 .json 扩展名的纯数字文件
        }
    }
    auto elements = parse_numbers_from_text(content, modulus);
    if (elements.empty()) {
        throw std::runtime_error("未能在文件中解析出任何数字");
    }
    return elements;
}

Dataset dataset_from_json(const json &j) {
    Dataset ds;
    ds.id = j.value("id", generate_id("ds"));
    ds.owner = j.value("owner", "");
    ds.name = j.value("name", "");
    ds.modulus = j.value("modulus", 0ULL);
    ds.version = j.value("version", "");
    ds.updated_at = j.value("updatedAt", now_iso_string());
    if (j.contains("elements") && j["elements"].is_array()) {
        ds.elements.clear();
        for (const auto &item : j["elements"]) {
            ds.elements.push_back(item.get<std::uint64_t>());
        }
    }
    return ds;
}

json session_to_storage_json(const SessionRecord &rec) {
    json logs = json::array();
    for (const auto &log : rec.logs) {
        logs.push_back({{"time", log.time}, {"level", log.level}, {"message", log.message}});
    }
    return json{
        {"id", rec.id},
        {"receiverDataset", rec.receiver_dataset},
        {"senderDataset", rec.sender_dataset},
        {"receiverAccount", rec.receiver_account},
        {"senderAccount", rec.sender_account},
        {"requestId", rec.request_id},
        {"status", rec.status},
        {"receiverSize", rec.receiver_size},
        {"senderSize", rec.sender_size},
        {"threshold", rec.threshold},
        {"thresholdRatio", rec.threshold_ratio},
        {"modulus", rec.modulus},
        {"notes", rec.notes},
        {"createdAt", rec.created_at},
        {"updatedAt", rec.updated_at},
        {"offlineMs", rec.offline_ms},
        {"onlineMs", rec.online_ms},
        {"finalizeMs", rec.finalize_ms},
        {"communicationBytes", rec.communication_bytes},
        {"receiverBytes", rec.receiver_bytes},
        {"senderBytes", rec.sender_bytes},
        {"coinFlipBytes", rec.coin_flip_bytes},
        {"pcgSeedBytes", rec.pcg_seed_bytes},
        {"onlineCommBytes", rec.online_comm_bytes},
        {"receiverOfflineMs", rec.receiver_offline_ms},
        {"senderOfflineMs", rec.sender_offline_ms},
        {"receiverOnlineMs", rec.receiver_online_ms},
        {"senderOnlineMs", rec.sender_online_ms},
        {"onlineCommMs", rec.online_comm_ms},
        {"coinFlipMs", rec.coin_flip_ms},
        {"totalProtocolMs", rec.total_protocol_ms},
        {"thresholdReached", rec.threshold_reached},
        {"intersection", rec.intersection},
        {"logs", logs}
    };
}

SessionRecord session_from_json(const json &j) {
    SessionRecord rec;
    rec.id = j.value("id", generate_id("session"));
    rec.receiver_dataset = j.value("receiverDataset", "");
    rec.sender_dataset = j.value("senderDataset", "");
    rec.receiver_account = j.value("receiverAccount", "");
    rec.sender_account = j.value("senderAccount", "");
    rec.request_id = j.value("requestId", "");
    rec.status = j.value("status", "completed");
    rec.receiver_size = j.value("receiverSize", static_cast<std::size_t>(0));
    rec.sender_size = j.value("senderSize", static_cast<std::size_t>(0));
    rec.threshold = j.value("threshold", static_cast<std::size_t>(0));
    rec.threshold_ratio = j.value("thresholdRatio", 0.0);
    rec.modulus = j.value("modulus", 0ULL);
    rec.notes = j.value("notes", "");
    rec.created_at = j.value("createdAt", now_iso_string());
    rec.updated_at = j.value("updatedAt", rec.created_at);
    rec.offline_ms = j.value("offlineMs", 0.0);
    rec.online_ms = j.value("onlineMs", 0.0);
    rec.finalize_ms = j.value("finalizeMs", 0.0);
    rec.communication_bytes = j.value("communicationBytes", static_cast<std::size_t>(0));
    rec.receiver_bytes = j.value("receiverBytes", static_cast<std::size_t>(0));
    rec.sender_bytes = j.value("senderBytes", static_cast<std::size_t>(0));
    rec.coin_flip_bytes = j.value("coinFlipBytes", static_cast<std::size_t>(0));
    rec.pcg_seed_bytes = j.value("pcgSeedBytes", static_cast<std::size_t>(0));
    rec.online_comm_bytes = j.value("onlineCommBytes", static_cast<std::size_t>(0));
    rec.receiver_offline_ms = j.value("receiverOfflineMs", 0.0);
    rec.sender_offline_ms = j.value("senderOfflineMs", 0.0);
    rec.receiver_online_ms = j.value("receiverOnlineMs", 0.0);
    rec.sender_online_ms = j.value("senderOnlineMs", 0.0);
    rec.online_comm_ms = j.value("onlineCommMs", 0.0);
    rec.coin_flip_ms = j.value("coinFlipMs", 0.0);
    rec.total_protocol_ms = j.value("totalProtocolMs", 0.0);
    rec.threshold_reached = j.value("thresholdReached", false);
    if (j.contains("intersection") && j["intersection"].is_array()) {
        rec.intersection.clear();
        for (const auto &item : j["intersection"]) {
            rec.intersection.push_back(item.get<std::uint64_t>());
        }
    }
    rec.logs.clear();
    if (j.contains("logs") && j["logs"].is_array()) {
        for (const auto &log : j["logs"]) {
            rec.logs.push_back({
                log.value("time", now_iso_string()),
                log.value("level", "info"),
                log.value("message", "")
            });
        }
    }
    return rec;
}

json request_to_storage_json(const RequestRecord &req) {
    return json{
        {"id", req.id},
        {"applicant", req.applicant},
        {"receiverDatasetId", req.receiver_dataset_id},
        {"counterparty", req.counterparty},
        {"senderDatasetId", req.sender_dataset_id},
        {"status", req.status},
        {"threshold", req.threshold},
        {"thresholdRatio", req.threshold_ratio},
        {"modulus", req.modulus},
        {"notes", req.notes},
        {"createdAt", req.created_at},
        {"updatedAt", req.updated_at},
        {"sessionId", req.session_id},
        {"errorMessage", req.error_message}
    };
}

RequestRecord request_from_json(const json &j) {
    RequestRecord req;
    req.id = j.value("id", generate_id("req"));
    req.applicant = j.value("applicant", "");
    req.receiver_dataset_id = j.value("receiverDatasetId", "");
    req.counterparty = j.value("counterparty", "");
    req.sender_dataset_id = j.value("senderDatasetId", "");
    std::string status = j.value("status", "pending-sender");
    if (status == "pending" || status == "confirmed") {
        req.status = "pending-sender";
    } else if (status == "approved") {
        req.status = "completed";
    } else if (status == "rejected") {
        req.status = "sender-rejected";
    } else {
        req.status = status;
    }
    req.threshold = j.value("threshold", static_cast<std::size_t>(0));
    req.threshold_ratio = j.value("thresholdRatio", 0.0);
    req.modulus = j.value("modulus", 0ULL);
    req.notes = j.value("notes", "");
    req.created_at = j.value("createdAt", now_iso_string());
    req.updated_at = j.value("updatedAt", req.created_at);
    req.session_id = j.value("sessionId", "");
    req.error_message = j.value("errorMessage", "");
    return req;
}

json user_to_storage_json(const UserRecord &user) {
    return json{
        {"id", user.id},
        {"username", user.username},
        {"password", user.password},
        {"role", user.role},
        {"name", user.name},
        {"organization", user.organization},
        {"phone", user.phone},
        {"email", user.email},
        {"avatarUrl", user.avatar_url},
        {"twoFactorEnabled", user.two_factor_enabled},
        {"notifications",
         {{"approvals", user.notifications.approvals},
          {"jobs", user.notifications.jobs},
          {"alerts", user.notifications.alerts}}},
        {"createdAt", user.created_at},
        {"updatedAt", user.updated_at}
    };
}

UserRecord user_from_json(const json &j) {
    UserRecord user;
    user.id = j.value("id", generate_id("user"));
    user.username = j.value("username", "");
    user.password = j.value("password", "");
    user.role = j.value("role", "receiver");
    user.name = j.value("name", "");
    user.organization = j.value("organization", "");
    user.phone = j.value("phone", "");
    user.email = j.value("email", "");
    user.avatar_url = j.value("avatarUrl", "");
    user.two_factor_enabled = j.value("twoFactorEnabled", false);
    auto notifications = j.value("notifications", json::object());
    user.notifications.approvals = notifications.value("approvals", true);
    user.notifications.jobs = notifications.value("jobs", true);
    user.notifications.alerts = notifications.value("alerts", true);
    user.created_at = j.value("createdAt", now_iso_string());
    user.updated_at = j.value("updatedAt", user.created_at);
    return user;
}

void rebuild_username_index_locked() {
    g_username_index.clear();
    for (const auto &kv : g_users) {
        g_username_index[kv.second.username] = kv.second.id;
    }
}

void save_state_locked() {
    ensure_storage_dir();
    json state;
    state["users"] = json::array();
    for (const auto &kv : g_users) {
        state["users"].push_back(user_to_storage_json(kv.second));
    }
    state["datasets"] = json::array();
    for (const auto &kv : g_datasets) {
        state["datasets"].push_back(dataset_to_storage_json(kv.second));
    }
    state["requests"] = json::array();
    for (const auto &kv : g_requests) {
        state["requests"].push_back(request_to_storage_json(kv.second));
    }
    state["sessions"] = json::array();
    for (const auto &kv : g_sessions) {
        state["sessions"].push_back(session_to_storage_json(kv.second));
    }

    std::ofstream ofs(kStoragePath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        throw std::runtime_error("failed to open storage for writing");
    }
    ofs << std::setw(2) << state;
}

void save_state() {
    std::lock_guard<std::mutex> lock(g_mutex);
    save_state_locked();
}

void load_state() {
    ensure_storage_dir();
    std::ifstream ifs(kStoragePath, std::ios::binary);
    if (!ifs.is_open()) {
        return;
    }
    json state;
    try {
        ifs >> state;
    } catch (...) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    g_users.clear();
    g_username_index.clear();
    g_datasets.clear();
    g_sessions.clear();
    g_requests.clear();

    if (state.contains("users") && state["users"].is_array()) {
        for (const auto &item : state["users"]) {
            auto user = user_from_json(item);
            g_username_index[user.username] = user.id;
            g_users.emplace(user.id, std::move(user));
        }
    }

    if (state.contains("datasets") && state["datasets"].is_array()) {
        for (const auto &item : state["datasets"]) {
            auto dataset = dataset_from_json(item);
            g_datasets.emplace(dataset.id, std::move(dataset));
        }
    }

    if (state.contains("requests") && state["requests"].is_array()) {
        for (const auto &item : state["requests"]) {
            auto req = request_from_json(item);
            g_requests.emplace(req.id, std::move(req));
        }
    }

    if (state.contains("sessions") && state["sessions"].is_array()) {
        for (const auto &item : state["sessions"]) {
            auto session = session_from_json(item);
            g_sessions.emplace(session.id, std::move(session));
        }
    }
}

std::optional<AuthContext> authenticate(const httplib::Request &req) {
    auto token_opt = extract_bearer_token(req);
    if (!token_opt) return std::nullopt;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto token_it = g_tokens.find(*token_opt);
    if (token_it == g_tokens.end()) return std::nullopt;
    auto user_it = g_users.find(token_it->second);
    if (user_it == g_users.end()) return std::nullopt;
    return AuthContext{token_it->second, user_it->second};
}

std::optional<AuthContext> require_auth(const httplib::Request &req, httplib::Response &res) {
    auto auth = authenticate(req);
    if (!auth) {
        res.status = 401;
        res.set_content(json{{"error", "unauthorized"}}.dump(), "application/json");
    }
    return auth;
}

std::string now_iso_string() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm_buf;
#if defined(_WIN32)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buffer);
}

std::string generate_id(const std::string &prefix) {
    static std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> dist;
    std::ostringstream oss;
    oss << prefix << "-" << std::hex << dist(rng);
    return oss.str();
}

std::optional<std::string> extract_bearer_token(const httplib::Request &req) {
    auto header = req.get_header_value("Authorization");
    if (header.empty()) {
        return std::nullopt;
    }
    const std::string prefix = "Bearer ";
    if (header.size() < prefix.size()) {
        return std::nullopt;
    }
    bool matches = true;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(header[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) {
            matches = false;
            break;
        }
    }
    if (!matches) {
        return std::nullopt;
    }
    std::string token = header.substr(prefix.size());
    if (token.empty()) {
        return std::nullopt;
    }
    return token;
}

bool is_valid_role(const std::string &role) {
    return role == "receiver" || role == "sender" || role == "admin";
}

std::string base64_encode(const std::string &input) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 3 <= input.size()) {
        const unsigned char b0 = static_cast<unsigned char>(input[i++]);
        const unsigned char b1 = static_cast<unsigned char>(input[i++]);
        const unsigned char b2 = static_cast<unsigned char>(input[i++]);

        output.push_back(alphabet[b0 >> 2]);
        output.push_back(alphabet[((b0 & 0x03) << 4) | (b1 >> 4)]);
        output.push_back(alphabet[((b1 & 0x0F) << 2) | (b2 >> 6)]);
        output.push_back(alphabet[b2 & 0x3F]);
    }

    const std::size_t remaining = input.size() - i;
    if (remaining == 1) {
        const unsigned char b0 = static_cast<unsigned char>(input[i]);
        output.push_back(alphabet[b0 >> 2]);
        output.push_back(alphabet[(b0 & 0x03) << 4]);
        output.push_back('=');
        output.push_back('=');
    } else if (remaining == 2) {
        const unsigned char b0 = static_cast<unsigned char>(input[i]);
        const unsigned char b1 = static_cast<unsigned char>(input[i + 1]);
        output.push_back(alphabet[b0 >> 2]);
        output.push_back(alphabet[((b0 & 0x03) << 4) | (b1 >> 4)]);
        output.push_back(alphabet[(b1 & 0x0F) << 2]);
        output.push_back('=');
    }

    return output;
}

json user_to_json(const UserRecord &user) {
    return json{
        {"id", user.id},
        {"username", user.username},
        {"name", user.name},
        {"role", user.role},
        {"organization", user.organization},
        {"phone", user.phone},
        {"email", user.email},
        {"avatarUrl", user.avatar_url},
        {"twoFactorEnabled", user.two_factor_enabled},
        {"notifications",
         {{"approvals", user.notifications.approvals},
          {"jobs", user.notifications.jobs},
          {"alerts", user.notifications.alerts}}},
        {"createdAt", user.created_at},
        {"updatedAt", user.updated_at}
    };
}

void ensure_sample_users() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_users.empty()) return;

    auto create_user = [](const std::string &username,
                          const std::string &password,
                          const std::string &role,
                          const std::string &name,
                          const std::string &organization,
                          const std::string &email) {
        UserRecord user;
        user.id = generate_id("user");
        user.username = username;
        user.password = password;
        user.role = role;
        user.name = name;
        user.organization = organization;
        user.email = email;
        user.phone = "";
        user.avatar_url = "";
        user.two_factor_enabled = false;
        user.notifications = Notifications{};
        user.created_at = now_iso_string();
        user.updated_at = user.created_at;
        g_username_index[user.username] = user.id;
        g_users.emplace(user.id, std::move(user));
    };

    create_user("receiver", "password123", "receiver", "示例接收方", "示例金融机构", "receiver@example.com");
    create_user("sender", "password123", "sender", "示例发送方", "示例保险公司", "sender@example.com");
    create_user("admin", "password123", "admin", "系统管理员", "TPSI 运维中心", "admin@example.com");

    try {
        save_state_locked();
    } catch (...) {
        // ignore storage errors for sample seeding
    }
}

json dataset_to_json(const Dataset &ds) {
    return json{{"id", ds.id},
                {"name", ds.name},
                {"size", ds.elements.size()},
                {"modulus", ds.modulus},
                {"version", ds.version},
                {"updatedAt", ds.updated_at},
                {"owner", ds.owner}};
}

void ensure_sample_datasets() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_datasets.empty()) return;
    Dataset receiver;
    receiver.id = "ds-receiver-demo";
    receiver.name = "接收方示例";
    receiver.owner = "receiver";
    receiver.modulus = 104857601ULL;
    receiver.version = "1.0";
    receiver.updated_at = now_iso_string();
    for (std::size_t i = 1; i <= 4096; ++i) receiver.elements.push_back(i);

    Dataset sender;
    sender.id = "ds-sender-demo";
    sender.name = "发送方示例";
    sender.owner = "sender";
    sender.modulus = 104857601ULL;
    sender.version = "1.0";
    sender.updated_at = now_iso_string();
    for (std::size_t i = 2048; i < 2048 + 4096; ++i) sender.elements.push_back(i);

    g_datasets.emplace(receiver.id, receiver);
    g_datasets.emplace(sender.id, sender);

    try {
        save_state_locked();
    } catch (...) {
        // ignore storage errors during seeding
    }
}

SessionRecord run_session(const Dataset &receiver,
                          const Dataset &sender,
                          std::size_t threshold,
                          double ratio,
                          std::uint64_t modulus,
                          const std::string &notes) {
    SessionRecord record;
    record.id = generate_id("session");
    record.receiver_dataset = receiver.id;
    record.sender_dataset = sender.id;
    record.receiver_size = receiver.elements.size();
    record.sender_size = sender.elements.size();
    record.threshold = threshold;
    record.threshold_ratio = ratio;
    record.modulus = modulus;
    record.notes = notes;
    record.created_at = now_iso_string();

    tpsi::SessionConfig cfg;
    cfg.receiver_set = receiver.elements;
    cfg.sender_set = sender.elements;
    const std::size_t protocol_threshold = std::max<std::size_t>(1, threshold);
    cfg.threshold = protocol_threshold;
    cfg.field.modulus = modulus;
    cfg.pseudo_count = 0;
    cfg.pcg.ring.modulus = modulus;
    cfg.pcg.ring.degree = tpsi::required_ring_degree(cfg);
    cfg.pcg.sparse_weight = 0;
    cfg.pcg.verify = false;

    auto channels = tpsi::MessageChannel::create_pair();
    auto receiver_channel = std::move(channels.first);
    auto sender_channel = std::move(channels.second);

    std::exception_ptr sender_error;
    std::thread sender_thread([cfg, ch = std::move(sender_channel), &sender_error]() mutable {
        try {
            tpsi::run_sender_session(cfg, std::move(ch));
        } catch (...) {
            sender_error = std::current_exception();
        }
    });

    tpsi::ReceiverOutput output;
    std::exception_ptr receiver_error;
    try {
        output = tpsi::run_receiver_session(cfg, std::move(receiver_channel));
    } catch (...) {
        receiver_error = std::current_exception();
    }

    if (sender_thread.joinable()) {
        sender_thread.join();
    }

    if (sender_error) {
        std::rethrow_exception(sender_error);
    }
    if (receiver_error) {
        std::rethrow_exception(receiver_error);
    }

    auto format_ms = [](double value) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << value;
        return oss.str();
    };

    record.receiver_offline_ms = output.metrics.offline_compute_ms;
    record.sender_offline_ms = output.metrics.sender_offline_ms;
    record.offline_ms = record.receiver_offline_ms + record.sender_offline_ms;
    record.receiver_online_ms = output.metrics.receiver_online_compute_ms;
    record.sender_online_ms = output.metrics.sender_online_compute_ms;
    record.online_comm_ms = output.metrics.online_comm_ms;
    record.online_ms =
        record.receiver_online_ms + record.sender_online_ms + record.online_comm_ms;
    record.finalize_ms = output.metrics.rss_reconstruction_ms;
    record.coin_flip_ms = output.metrics.coin_flip_ms;
    record.total_protocol_ms = output.metrics.total_protocol_ms;

    record.receiver_bytes = output.result.comm.receiver_bytes;
    record.sender_bytes = output.result.comm.sender_bytes;
    record.coin_flip_bytes = output.metrics.coin_flip_bytes;
    record.pcg_seed_bytes = output.metrics.pcg_seed_bytes;
    record.online_comm_bytes = output.metrics.online_comm_bytes;
    record.communication_bytes = output.metrics.total_comm_bytes;

    record.intersection = output.result.intersection;
    std::sort(record.intersection.begin(), record.intersection.end());

    const bool protocol_threshold_reached = output.result.threshold_reached;
    record.threshold_reached = (threshold == 0) ? true : protocol_threshold_reached;
    record.status = record.threshold_reached ? "completed" : "failed";

    record.logs.clear();
    auto add_log = [&](const std::string &level, const std::string &message) {
        record.logs.push_back({now_iso_string(), level, message});
    };

    add_log("info", "Coin flip 阶段耗时 " + format_ms(record.coin_flip_ms) + " ms，传输 " +
                        std::to_string(record.coin_flip_bytes) + " bytes");
    add_log("info",
            "离线阶段：接收方 " + format_ms(record.receiver_offline_ms) + " ms，发送方 " +
                format_ms(record.sender_offline_ms) + " ms");
    add_log("info",
            "在线阶段：接收方 " + format_ms(record.receiver_online_ms) + " ms，发送方 " +
                format_ms(record.sender_online_ms) + " ms，通信 " +
                format_ms(record.online_comm_ms) + " ms / " +
                std::to_string(record.online_comm_bytes) + " bytes");
    add_log("info", "RSS 重建耗时 " + format_ms(record.finalize_ms) + " ms");
    if (threshold == 0) {
        add_log("info", "阈值为 0，自动判定为达成");
    }
    add_log(record.threshold_reached ? "info" : "warn",
            record.threshold_reached ? "阈值达成" : "阈值未达");

    record.updated_at = now_iso_string();
    return record;
}

json session_summary_to_json(const SessionRecord &rec) {
    return json{{"id", rec.id},
                {"name", rec.notes.empty() ? rec.id : rec.notes},
                {"status", rec.status},
                {"size", rec.receiver_size},
                {"threshold", rec.threshold},
                {"modulus", rec.modulus},
                {"updatedAt", rec.updated_at.empty() ? rec.created_at : rec.updated_at}};
}

json session_detail_to_json(const SessionRecord &rec) {
    json metrics = {
        {"offlineMs", rec.offline_ms},
        {"receiverOfflineMs", rec.receiver_offline_ms},
        {"senderOfflineMs", rec.sender_offline_ms},
        {"onlineMs", rec.online_ms},
        {"receiverOnlineMs", rec.receiver_online_ms},
        {"senderOnlineMs", rec.sender_online_ms},
        {"onlineCommMs", rec.online_comm_ms},
        {"finalizeMs", rec.finalize_ms},
        {"coinFlipMs", rec.coin_flip_ms},
        {"totalProtocolMs", rec.total_protocol_ms},
        {"communicationBytes", rec.communication_bytes},
        {"onlineCommBytes", rec.online_comm_bytes},
        {"senderBytes", rec.sender_bytes},
        {"receiverBytes", rec.receiver_bytes},
        {"coinFlipBytes", rec.coin_flip_bytes},
        {"pcgSeedBytes", rec.pcg_seed_bytes},
        {"senderTimeMs", rec.sender_online_ms},
        {"receiverTimeMs", rec.receiver_online_ms}
    };

    json detail = {
        {"id", rec.id},
        {"name", rec.notes.empty() ? rec.id : rec.notes},
        {"status", rec.status},
        {"size", rec.receiver_size},
        {"threshold", rec.threshold},
        {"thresholdRatio", rec.threshold_ratio},
        {"modulus", rec.modulus},
        {"notes", rec.notes},
        {"createdAt", rec.created_at},
        {"updatedAt", rec.updated_at.empty() ? rec.created_at : rec.updated_at},
        {"receiverAccount", rec.receiver_account},
        {"senderAccount", rec.sender_account},
        {"requestId", rec.request_id},
        {"thresholdReached", rec.threshold_reached},
        {"intersectionSize", rec.intersection.size()},
        {"metrics", metrics}
    };

    json logs = json::array();
    for (const auto &log : rec.logs) {
        logs.push_back({{"time", log.time}, {"level", log.level}, {"message", log.message}});
    }
    detail["logs"] = logs;
    return detail;
}

json request_to_json(const RequestRecord &req) {
    json result = {
        {"id", req.id},
        {"applicant", req.applicant},
        {"receiverDatasetId", req.receiver_dataset_id},
        {"counterparty", req.counterparty},
        {"senderDatasetId", req.sender_dataset_id},
        {"status", req.status},
        {"threshold", req.threshold},
        {"thresholdRatio", req.threshold_ratio},
        {"modulus", req.modulus},
        {"notes", req.notes},
        {"createdAt", req.created_at},
        {"updatedAt", req.updated_at.empty() ? req.created_at : req.updated_at},
        {"errorMessage", req.error_message}
    };
    if (!req.session_id.empty()) {
        result["sessionId"] = req.session_id;
    }
    return result;
}

} // namespace

int main() {
    load_state();
    ensure_sample_users();
    ensure_sample_datasets();

    httplib::Server server;
    server.set_default_headers({{"Access-Control-Allow-Origin", "*"},
                                {"Access-Control-Allow-Headers", "Content-Type, Authorization"},
                                {"Access-Control-Allow-Methods", "GET,POST,OPTIONS"}});

    server.Options(".*", [](const httplib::Request &, httplib::Response &res) {
        res.status = 204;
    });

    server.Get("/health", [](const httplib::Request &, httplib::Response &res) {
        res.set_content(json{{"status", "ok"}}.dump(), "application/json");
    });

    server.Post("/api/auth/login", [](const httplib::Request &req, httplib::Response &res) {
        try {
            const auto body = json::parse(req.body);
            const std::string username = body.value("username", "");
            const std::string password = body.value("password", "");
            if (username.empty() || password.empty()) {
                res.status = 400;
                res.set_content(json{{"error", "username and password required"}}.dump(), "application/json");
                return;
            }
            std::lock_guard<std::mutex> lock(g_mutex);
            auto index_it = g_username_index.find(username);
            if (index_it == g_username_index.end()) {
                res.status = 401;
                res.set_content(json{{"error", "invalid credentials"}}.dump(), "application/json");
                return;
            }
            auto user_it = g_users.find(index_it->second);
            if (user_it == g_users.end() || user_it->second.password != password) {
                res.status = 401;
                res.set_content(json{{"error", "invalid credentials"}}.dump(), "application/json");
                return;
            }
            const std::string requested_role = body.value("role", "");
            if (!requested_role.empty() && requested_role != user_it->second.role) {
                res.status = 403;
                res.set_content(json{{"error", "role mismatch"}}.dump(), "application/json");
                return;
            }
            std::string token = generate_id("token");
            g_tokens[token] = user_it->second.id;
            user_it->second.updated_at = now_iso_string();
            res.set_content(json{{"token", token}, {"user", user_to_json(user_it->second)}}.dump(), "application/json");
        } catch (const std::exception &ex) {
            res.status = 400;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/auth/register", [](const httplib::Request &req, httplib::Response &res) {
        try {
            const auto body = json::parse(req.body);
            const std::string username = body.value("username", "");
            const std::string password = body.value("password", "");
            const std::string role = body.value("role", "receiver");
            const std::string organization = body.value("organization", "");
            if (username.empty() || password.empty()) {
                res.status = 400;
                res.set_content(json{{"error", "username and password required"}}.dump(), "application/json");
                return;
            }
            if (!is_valid_role(role) || role == "admin") {
                res.status = 400;
                res.set_content(json{{"error", "invalid role"}}.dump(), "application/json");
                return;
            }
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_username_index.count(username)) {
                res.status = 409;
                res.set_content(json{{"error", "username already exists"}}.dump(), "application/json");
                return;
            }
            UserRecord user;
            user.id = generate_id("user");
            user.username = username;
            user.password = password;
            user.role = role;
            user.name = body.value("name", username);
            user.organization = organization;
            user.phone = body.value("phone", "");
            user.email = body.value("email", "");
            user.avatar_url = "";
            user.two_factor_enabled = false;
            user.notifications = Notifications{};
            user.created_at = now_iso_string();
            user.updated_at = user.created_at;

            g_username_index[user.username] = user.id;
            auto emplace_result = g_users.emplace(user.id, std::move(user));
            UserRecord &stored_user = emplace_result.first->second;

            std::string token = generate_id("token");
            g_tokens[token] = stored_user.id;
            try {
                save_state_locked();
            } catch (const std::exception &ex) {
                res.status = 500;
                res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
                return;
            }
            res.set_content(json{{"token", token}, {"user", user_to_json(stored_user)}}.dump(), "application/json");
        } catch (const std::exception &ex) {
            res.status = 400;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
        }
    });

    server.Get("/api/auth/me", [](const httplib::Request &req, httplib::Response &res) {
        auto token_opt = extract_bearer_token(req);
        if (!token_opt) {
            res.status = 401;
            res.set_content(json{{"error", "missing bearer token"}}.dump(), "application/json");
            return;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        auto token_it = g_tokens.find(*token_opt);
        if (token_it == g_tokens.end()) {
            res.status = 401;
            res.set_content(json{{"error", "invalid token"}}.dump(), "application/json");
            return;
        }
        auto user_it = g_users.find(token_it->second);
        if (user_it == g_users.end()) {
            res.status = 401;
            res.set_content(json{{"error", "invalid token"}}.dump(), "application/json");
            return;
        }
        res.set_content(json{{"user", user_to_json(user_it->second)}}.dump(), "application/json");
    });

    server.Patch("/api/users/me", [](const httplib::Request &req, httplib::Response &res) {
        auto token_opt = extract_bearer_token(req);
        if (!token_opt) {
            res.status = 401;
            res.set_content(json{{"error", "missing bearer token"}}.dump(), "application/json");
            return;
        }
        try {
            const auto body = json::parse(req.body);
            std::lock_guard<std::mutex> lock(g_mutex);
            auto token_it = g_tokens.find(*token_opt);
            if (token_it == g_tokens.end()) {
                res.status = 401;
                res.set_content(json{{"error", "invalid token"}}.dump(), "application/json");
                return;
            }
            auto user_it = g_users.find(token_it->second);
            if (user_it == g_users.end()) {
                res.status = 401;
                res.set_content(json{{"error", "invalid token"}}.dump(), "application/json");
                return;
            }
            if (body.contains("name")) user_it->second.name = body.value("name", user_it->second.name);
            if (body.contains("organization")) user_it->second.organization = body.value("organization", user_it->second.organization);
            if (body.contains("phone")) user_it->second.phone = body.value("phone", user_it->second.phone);
            if (body.contains("email")) user_it->second.email = body.value("email", user_it->second.email);
            user_it->second.updated_at = now_iso_string();
            try {
                save_state_locked();
            } catch (const std::exception &ex) {
                res.status = 500;
                res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
                return;
            }
            res.set_content(json{{"user", user_to_json(user_it->second)}}.dump(), "application/json");
        } catch (const std::exception &ex) {
            res.status = 400;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/users/me/avatar", [](const httplib::Request &req, httplib::Response &res) {
        auto token_opt = extract_bearer_token(req);
        if (!token_opt) {
            res.status = 401;
            res.set_content(json{{"error", "missing bearer token"}}.dump(), "application/json");
            return;
        }
        if (!req.is_multipart_form_data() || !req.has_file("file")) {
            res.status = 400;
            res.set_content(json{{"error", "multipart form with file required"}}.dump(), "application/json");
            return;
        }
        const auto &file = req.get_file_value("file");
        std::lock_guard<std::mutex> lock(g_mutex);
        auto token_it = g_tokens.find(*token_opt);
        if (token_it == g_tokens.end()) {
            res.status = 401;
            res.set_content(json{{"error", "invalid token"}}.dump(), "application/json");
            return;
        }
        auto user_it = g_users.find(token_it->second);
        if (user_it == g_users.end()) {
            res.status = 401;
            res.set_content(json{{"error", "invalid token"}}.dump(), "application/json");
            return;
        }
        const std::string content_type = file.content_type.empty() ? "application/octet-stream" : file.content_type;
        const std::string encoded = base64_encode(file.content);
        user_it->second.avatar_url = "data:" + content_type + ";base64," + encoded;
        user_it->second.updated_at = now_iso_string();
        try {
            save_state_locked();
        } catch (const std::exception &ex) {
            res.status = 500;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
            return;
        }
        res.set_content(json{{"avatarUrl", user_it->second.avatar_url}, {"user", user_to_json(user_it->second)}}.dump(), "application/json");
    });

    server.Put("/api/users/me/notifications", [](const httplib::Request &req, httplib::Response &res) {
        auto token_opt = extract_bearer_token(req);
        if (!token_opt) {
            res.status = 401;
            res.set_content(json{{"error", "missing bearer token"}}.dump(), "application/json");
            return;
        }
        try {
            const auto body = json::parse(req.body);
            std::lock_guard<std::mutex> lock(g_mutex);
            auto token_it = g_tokens.find(*token_opt);
            if (token_it == g_tokens.end()) {
                res.status = 401;
                res.set_content(json{{"error", "invalid token"}}.dump(), "application/json");
                return;
            }
            auto user_it = g_users.find(token_it->second);
            if (user_it == g_users.end()) {
                res.status = 401;
                res.set_content(json{{"error", "invalid token"}}.dump(), "application/json");
                return;
            }
            user_it->second.notifications.approvals = body.value("approvals", user_it->second.notifications.approvals);
            user_it->second.notifications.jobs = body.value("jobs", user_it->second.notifications.jobs);
            user_it->second.notifications.alerts = body.value("alerts", user_it->second.notifications.alerts);
            user_it->second.updated_at = now_iso_string();
            try {
                save_state_locked();
            } catch (const std::exception &ex) {
                res.status = 500;
                res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
                return;
            }
            res.set_content(json{{"user", user_to_json(user_it->second)}}.dump(), "application/json");
        } catch (const std::exception &ex) {
            res.status = 400;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/auth/password", [](const httplib::Request &req, httplib::Response &res) {
        auto token_opt = extract_bearer_token(req);
        if (!token_opt) {
            res.status = 401;
            res.set_content(json{{"error", "missing bearer token"}}.dump(), "application/json");
            return;
        }
        try {
            const auto body = json::parse(req.body);
            const std::string old_password = body.value("old_password", "");
            const std::string new_password = body.value("new_password", "");
            if (old_password.empty() || new_password.empty()) {
                res.status = 400;
                res.set_content(json{{"error", "old_password and new_password required"}}.dump(), "application/json");
                return;
            }
            std::lock_guard<std::mutex> lock(g_mutex);
            auto token_it = g_tokens.find(*token_opt);
            if (token_it == g_tokens.end()) {
                res.status = 401;
                res.set_content(json{{"error", "invalid token"}}.dump(), "application/json");
                return;
            }
            auto user_it = g_users.find(token_it->second);
            if (user_it == g_users.end() || user_it->second.password != old_password) {
                res.status = 400;
                res.set_content(json{{"error", "old password incorrect"}}.dump(), "application/json");
                return;
            }
            user_it->second.password = new_password;
            user_it->second.updated_at = now_iso_string();
            try {
                save_state_locked();
            } catch (const std::exception &ex) {
                res.status = 500;
                res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
                return;
            }
            res.set_content(json{{"status", "ok"}}.dump(), "application/json");
        } catch (const std::exception &ex) {
            res.status = 400;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
        }
    });

    server.Post("/api/auth/2fa/enable", [](const httplib::Request &req, httplib::Response &res) {
        auto token_opt = extract_bearer_token(req);
        if (!token_opt) {
            res.status = 401;
            res.set_content(json{{"error", "missing bearer token"}}.dump(), "application/json");
            return;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        auto token_it = g_tokens.find(*token_opt);
        if (token_it == g_tokens.end()) {
            res.status = 401;
            res.set_content(json{{"error", "invalid token"}}.dump(), "application/json");
            return;
        }
        auto user_it = g_users.find(token_it->second);
        if (user_it == g_users.end()) {
            res.status = 401;
            res.set_content(json{{"error", "invalid token"}}.dump(), "application/json");
            return;
        }
        user_it->second.two_factor_enabled = true;
        user_it->second.updated_at = now_iso_string();
        std::string secret = generate_id("otp");
        try {
            save_state_locked();
        } catch (const std::exception &ex) {
            res.status = 500;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
            return;
        }
        res.set_content(
            json{{"user", user_to_json(user_it->second)}, {"secret", secret}}.dump(), "application/json");
    });

    server.Post("/api/auth/2fa/disable", [](const httplib::Request &req, httplib::Response &res) {
        auto token_opt = extract_bearer_token(req);
        if (!token_opt) {
            res.status = 401;
            res.set_content(json{{"error", "missing bearer token"}}.dump(), "application/json");
            return;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        auto token_it = g_tokens.find(*token_opt);
        if (token_it == g_tokens.end()) {
            res.status = 401;
            res.set_content(json{{"error", "invalid token"}}.dump(), "application/json");
            return;
        }
        auto user_it = g_users.find(token_it->second);
        if (user_it == g_users.end()) {
            res.status = 401;
            res.set_content(json{{"error", "invalid token"}}.dump(), "application/json");
            return;
        }
        user_it->second.two_factor_enabled = false;
        user_it->second.updated_at = now_iso_string();
        try {
            save_state_locked();
        } catch (const std::exception &ex) {
            res.status = 500;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
            return;
        }
        res.set_content(json{{"user", user_to_json(user_it->second)}}.dump(), "application/json");
    });

    server.Get("/api/datasets", [](const httplib::Request &req, httplib::Response &res) {
        auto auth_opt = require_auth(req, res);
        if (!auth_opt) return;
        const auto &auth = *auth_opt;
        std::lock_guard<std::mutex> lock(g_mutex);
        json arr = json::array();
        for (const auto &kv : g_datasets) {
            const auto &ds = kv.second;
            if (auth.user.role != "admin" && ds.owner != auth.user.username) continue;
            arr.push_back(dataset_to_json(ds));
        }
        res.set_content(arr.dump(), "application/json");
    });

    server.Get(R"(/api/datasets/(.+))", [](const httplib::Request &req, httplib::Response &res) {
        auto auth_opt = require_auth(req, res);
        if (!auth_opt) return;
        const auto &auth = *auth_opt;
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_datasets.find(req.matches[1]);
        if (it == g_datasets.end()) {
            res.status = 404;
            res.set_content(json{{"error", "dataset not found"}}.dump(), "application/json");
            return;
        }
        if (auth.user.role != "admin" && it->second.owner != auth.user.username) {
            res.status = 403;
            res.set_content(json{{"error", "forbidden"}}.dump(), "application/json");
            return;
        }
        json detail = dataset_to_json(it->second);
        if (!it->second.elements.empty()) {
            auto minmax = std::minmax_element(it->second.elements.begin(), it->second.elements.end());
            std::unordered_set<std::uint64_t> dedup(it->second.elements.begin(), it->second.elements.end());
            detail["stats"] = {
                {"min", *minmax.first},
                {"max", *minmax.second},
                {"duplicates", it->second.elements.size() - dedup.size()}
            };
            json sample = json::array();
            for (std::size_t i = 0; i < std::min<std::size_t>(it->second.elements.size(), 16); ++i)
                sample.push_back(it->second.elements[i]);
            detail["sample"] = sample;
        }
        res.set_content(detail.dump(), "application/json");
    });

    server.Post("/api/datasets", [](const httplib::Request &req, httplib::Response &res) {
        auto auth_opt = require_auth(req, res);
        if (!auth_opt) return;
        const auto &auth = *auth_opt;
        if (auth.user.role != "receiver" && auth.user.role != "sender" && auth.user.role != "admin") {
            res.status = 403;
            res.set_content(json{{"error", "insufficient role"}}.dump(), "application/json");
            return;
        }
        try {
            std::string name;
            std::string version = "上传";
            std::uint64_t modulus = 104857601ULL;
            std::vector<std::uint64_t> elements;

            if (req.is_multipart_form_data()) {
                auto name_param = trim_copy(req.get_param_value("name"));
                if (!name_param.empty()) {
                    name = std::move(name_param);
                }
                auto version_param = trim_copy(req.get_param_value("version"));
                if (!version_param.empty()) {
                    version = std::move(version_param);
                }
                auto modulus_param = trim_copy(req.get_param_value("modulus"));
                if (!modulus_param.empty()) {
                    modulus = parse_modulus_or_default(modulus_param, modulus);
                }

                if (req.has_file("file")) {
                    const auto &file = req.get_file_value("file");
                    elements = parse_dataset_file_content(file.filename, file.content, modulus);
                    if (name.empty()) {
                        name = trim_copy(file.filename);
                    }
                }
                if (elements.empty()) {
                    auto elements_param = req.get_param_value("elements");
                    if (!elements_param.empty()) {
                        elements = parse_elements_from_string(elements_param, modulus);
                    }
                }
                name = trim_copy(name);
                if (name.empty()) {
                    res.status = 400;
                    res.set_content(json{{"error", "name is required"}}.dump(), "application/json");
                    return;
                }
                if (elements.empty()) {
                    res.status = 400;
                    res.set_content(json{{"error", "dataset file required"}}.dump(), "application/json");
                    return;
                }
            } else {
                auto body = req.body.empty() ? json::object() : json::parse(req.body);
                name = trim_copy(body.value("name", ""));
                if (name.empty()) {
                    res.status = 400;
                    res.set_content(json{{"error", "name is required"}}.dump(), "application/json");
                    return;
                }
                if (body.contains("version")) {
                    auto candidate = trim_copy(body.value("version", ""));
                    if (!candidate.empty()) version = candidate;
                }
                if (body.contains("modulus")) {
                    const auto &node = body["modulus"];
                    if (node.is_string()) {
                        modulus = parse_modulus_or_default(node.get<std::string>(), modulus);
                    } else if (node.is_number_unsigned()) {
                        modulus = node.get<std::uint64_t>();
                    } else if (node.is_number_integer()) {
                        auto value = node.get<std::int64_t>();
                        if (value <= 0) throw std::runtime_error("modulus must be greater than zero");
                        modulus = static_cast<std::uint64_t>(value);
                    } else if (node.is_number_float()) {
                        auto value = node.get<double>();
                        if (value <= 0.0) throw std::runtime_error("modulus must be greater than zero");
                        modulus = static_cast<std::uint64_t>(std::llround(value));
                        if (modulus == 0) throw std::runtime_error("modulus must be greater than zero");
                    } else {
                        throw std::runtime_error("invalid modulus value");
                    }
                }
                if (modulus == 0) {
                    throw std::runtime_error("modulus must be greater than zero");
                }
                if (body.contains("elements")) {
                    elements = parse_elements_array(body["elements"], modulus);
                } else if (body.contains("elementsText")) {
                    elements = parse_elements_from_string(body["elementsText"].get<std::string>(), modulus);
                } else if (body.contains("size")) {
                    const std::size_t sz = body["size"].get<std::size_t>();
                    elements.reserve(sz);
                    for (std::size_t i = 0; i < sz; ++i) {
                        elements.push_back(static_cast<std::uint64_t>(i + 1));
                    }
                }
                if (elements.empty()) {
                    res.status = 400;
                    res.set_content(json{{"error", "elements array required"}}.dump(), "application/json");
                    return;
                }
            }

            Dataset ds;
            ds.id = generate_id("ds");
            ds.owner = auth.user.username;
            ds.name = name;
            ds.modulus = modulus;
            ds.version = version.empty() ? "上传" : version;
            ds.updated_at = now_iso_string();
            ds.elements = std::move(elements);

            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_datasets.emplace(ds.id, ds);
                try {
                    save_state_locked();
                } catch (const std::exception &ex) {
                    res.status = 500;
                    res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
                    return;
                }
            }
            res.set_content(dataset_to_json(ds).dump(), "application/json");
        } catch (const std::exception &ex) {
            res.status = 400;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
        }
    });

    server.Get("/api/requests", [](const httplib::Request &req, httplib::Response &res) {
        auto auth_opt = require_auth(req, res);
        if (!auth_opt) return;
        const auto &auth = *auth_opt;
        std::lock_guard<std::mutex> lock(g_mutex);
        json arr = json::array();
        for (const auto &kv : g_requests) {
            const auto &request = kv.second;
            bool visible = false;
            if (auth.user.role == "admin") {
                visible = true;
            } else if (auth.user.role == "receiver") {
                visible = (request.applicant == auth.user.username);
            } else if (auth.user.role == "sender") {
                visible = (request.counterparty == auth.user.username);
            }
            if (!visible) continue;
            arr.push_back(request_to_json(request));
        }
        res.set_content(arr.dump(), "application/json");
    });

    server.Post("/api/requests", [](const httplib::Request &req, httplib::Response &res) {
        auto auth_opt = require_auth(req, res);
        if (!auth_opt) return;
        const auto &auth = *auth_opt;
        if (auth.user.role != "receiver") {
            res.status = 403;
            res.set_content(json{{"error", "only receivers can submit requests"}}.dump(), "application/json");
            return;
        }
        try {
            auto body = json::parse(req.body);
            const std::string receiver_id = body.value("receiverDatasetId", "");
            const std::string counterparty = body.value("counterparty", "");
            const std::size_t threshold = body.contains("threshold") ? body["threshold"].get<std::size_t>() : 0;
            const std::string notes = body.value("notes", "");
            if (receiver_id.empty() || counterparty.empty()) {
                res.status = 400;
                res.set_content(json{{"error", "receiverDatasetId and counterparty required"}}.dump(), "application/json");
                return;
            }

            Dataset receiver_dataset;
            std::size_t receiver_size = 0;
            std::uint64_t modulus = 104857601ULL;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                auto ds_it = g_datasets.find(receiver_id);
                if (ds_it == g_datasets.end()) {
                    res.status = 404;
                    res.set_content(json{{"error", "receiver dataset not found"}}.dump(), "application/json");
                    return;
                }
                if (ds_it->second.owner != auth.user.username) {
                    res.status = 403;
                    res.set_content(json{{"error", "dataset does not belong to receiver"}}.dump(), "application/json");
                    return;
                }
                receiver_dataset = ds_it->second;
                receiver_size = receiver_dataset.elements.size();
                modulus = receiver_dataset.modulus;

                auto counterparty_it = g_username_index.find(counterparty);
                if (counterparty_it == g_username_index.end()) {
                    res.status = 404;
                    res.set_content(json{{"error", "counterparty not found"}}.dump(), "application/json");
                    return;
                }
                auto sender_user_it = g_users.find(counterparty_it->second);
                if (sender_user_it == g_users.end() || sender_user_it->second.role != "sender") {
                    res.status = 400;
                    res.set_content(json{{"error", "counterparty must be a sender"}}.dump(), "application/json");
                    return;
                }
            }

            RequestRecord req_record;
            req_record.id = generate_id("req");
            req_record.applicant = auth.user.username;
            req_record.receiver_dataset_id = receiver_id;
            req_record.counterparty = counterparty;
            req_record.sender_dataset_id = "";
            req_record.status = "pending-sender";
            req_record.threshold = threshold;
            req_record.threshold_ratio = (receiver_size > 0 && threshold > 0)
                                             ? static_cast<double>(threshold) / static_cast<double>(receiver_size)
                                             : 0.0;
            req_record.modulus = modulus;
            req_record.notes = notes;
            req_record.created_at = now_iso_string();
            req_record.updated_at = req_record.created_at;
            req_record.error_message.clear();

            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_requests.emplace(req_record.id, req_record);
                try {
                    save_state_locked();
                } catch (const std::exception &ex) {
                    res.status = 500;
                    res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
                    return;
                }
            }
            res.set_content(json{{"request", request_to_json(req_record)}}.dump(), "application/json");
        } catch (const std::exception &ex) {
            res.status = 400;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
        }
    });

    server.Get("/api/sessions", [](const httplib::Request &req, httplib::Response &res) {
        auto auth_opt = require_auth(req, res);
        if (!auth_opt) return;
        const auto &auth = *auth_opt;
        std::lock_guard<std::mutex> lock(g_mutex);
        json arr = json::array();
        for (const auto &kv : g_sessions) {
            const auto &session = kv.second;
            bool visible = false;
            if (auth.user.role == "admin") {
                visible = true;
            } else if (auth.user.role == "receiver") {
                visible = (session.receiver_account == auth.user.username);
            } else if (auth.user.role == "sender") {
                visible = (session.sender_account == auth.user.username);
            }
            if (!visible) continue;
            arr.push_back(session_summary_to_json(session));
        }
        res.set_content(arr.dump(), "application/json");
    });

    server.Get(R"(/api/sessions/(.+))", [](const httplib::Request &req, httplib::Response &res) {
        auto auth_opt = require_auth(req, res);
        if (!auth_opt) return;
        const auto &auth = *auth_opt;
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_sessions.find(req.matches[1]);
        if (it == g_sessions.end()) {
            res.status = 404;
            res.set_content(json{{"error", "session not found"}}.dump(), "application/json");
            return;
        }
        const auto &session = it->second;
        bool visible = false;
        if (auth.user.role == "admin") {
            visible = true;
        } else if (auth.user.role == "receiver") {
            visible = (session.receiver_account == auth.user.username);
        } else if (auth.user.role == "sender") {
            visible = (session.sender_account == auth.user.username);
        }
        if (!visible) {
            res.status = 403;
            res.set_content(json{{"error", "forbidden"}}.dump(), "application/json");
            return;
        }
        json detail = session_detail_to_json(session);
        if (auth.user.role == "admin" || (auth.user.role == "receiver" && session.receiver_account == auth.user.username)) {
            detail["intersection"] = session.intersection;
        }
        res.set_content(detail.dump(), "application/json");
    });

    server.Post("/api/sessions", [](const httplib::Request &req, httplib::Response &res) {
        auto auth_opt = require_auth(req, res);
        if (!auth_opt) return;
        if (auth_opt->user.role != "admin") {
            res.status = 403;
            res.set_content(json{{"error", "admin required"}}.dump(), "application/json");
            return;
        }
        try {
            auto body = json::parse(req.body);
            std::string receiver_id = body.value("receiverDatasetId", "");
            std::string sender_id = body.value("senderDatasetId", "");
            if (receiver_id.empty() || sender_id.empty()) {
                res.status = 400;
                res.set_content(json{{"error", "receiverDatasetId and senderDatasetId required"}}.dump(), "application/json");
                return;
            }

            Dataset receiver;
            Dataset sender;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                auto it_r = g_datasets.find(receiver_id);
                auto it_s = g_datasets.find(sender_id);
                if (it_r == g_datasets.end() || it_s == g_datasets.end()) {
                    res.status = 404;
                    res.set_content(json{{"error", "dataset not found"}}.dump(), "application/json");
                    return;
                }
                receiver = it_r->second;
                sender = it_s->second;
            }

            std::uint64_t modulus = body.value("modulus", receiver.modulus);
            double ratio = body.value("thresholdRatio", 0.95);
            std::size_t threshold = body.contains("threshold") ? body["threshold"].get<std::size_t>()
                                                                : static_cast<std::size_t>(receiver.elements.size() * ratio);
            threshold = std::min(threshold, receiver.elements.size());
            std::string notes = body.value("notes", "");

            SessionRecord record = run_session(receiver, sender, threshold, ratio, modulus, notes);
            record.receiver_account = receiver.owner;
            record.sender_account = sender.owner;
            record.request_id = body.value("requestId", "");
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_sessions.emplace(record.id, record);
                try {
                    save_state_locked();
                } catch (const std::exception &ex) {
                    res.status = 500;
                    res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
                    return;
                }
            }

            res.set_content(session_detail_to_json(record).dump(), "application/json");
        } catch (const std::exception &ex) {
            res.status = 500;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
        }
    });

    server.Post(R"(/api/sessions/(.+)/rerun)", [](const httplib::Request &req, httplib::Response &res) {
        auto auth_opt = require_auth(req, res);
        if (!auth_opt) return;
        if (auth_opt->user.role != "admin") {
            res.status = 403;
            res.set_content(json{{"error", "admin required"}}.dump(), "application/json");
            return;
        }
        const std::string session_id = req.matches[1];
        Dataset receiver;
        Dataset sender;
        SessionRecord previous;

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_sessions.find(session_id);
            if (it == g_sessions.end()) {
                res.status = 404;
                res.set_content(json{{"error", "session not found"}}.dump(), "application/json");
                return;
            }
            auto it_r = g_datasets.find(it->second.receiver_dataset);
            auto it_s = g_datasets.find(it->second.sender_dataset);
            if (it_r == g_datasets.end() || it_s == g_datasets.end()) {
                res.status = 404;
                res.set_content(json{{"error", "dataset not found"}}.dump(), "application/json");
                return;
            }
            receiver = it_r->second;
            sender = it_s->second;
            previous = it->second;
        }

        std::size_t rerun_threshold = std::min(previous.threshold, receiver.elements.size());
        SessionRecord rerun;
        try {
            rerun = run_session(receiver,
                                sender,
                                rerun_threshold,
                                previous.threshold_ratio,
                                previous.modulus,
                                previous.notes);
        } catch (const std::exception &ex) {
            res.status = 500;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
            return;
        }

        rerun.id = previous.id;
        rerun.created_at = previous.created_at;
        rerun.updated_at = now_iso_string();
        rerun.receiver_account = receiver.owner;
        rerun.sender_account = sender.owner;
        rerun.request_id = previous.request_id;

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_sessions[rerun.id] = rerun;
            try {
                save_state_locked();
            } catch (const std::exception &ex) {
                res.status = 500;
                res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
                return;
            }
        }
        res.set_content(session_detail_to_json(rerun).dump(), "application/json");
    });

    server.Get(R"(/api/sessions/(.+)/intersection\.csv)", [](const httplib::Request &req, httplib::Response &res) {
        auto auth_opt = require_auth(req, res);
        if (!auth_opt) return;
        const auto &auth = *auth_opt;
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_sessions.find(req.matches[1]);
        if (it == g_sessions.end()) {
            res.status = 404;
            return;
        }
        bool allowed = false;
        if (auth.user.role == "admin") {
            allowed = true;
        } else if (auth.user.role == "receiver") {
            allowed = (it->second.receiver_account == auth.user.username);
        }
        if (!allowed) {
            res.status = 403;
            res.set_content(json{{"error", "forbidden"}}.dump(), "application/json");
            return;
        }
        std::ostringstream oss;
        oss << "element\n";
        for (auto v : it->second.intersection) oss << v << "\n";
        res.set_content(oss.str(), "text/csv");
    });

    server.Post(R"(/api/requests/(.+)/confirm)", [](const httplib::Request &req, httplib::Response &res) {
        auto auth_opt = require_auth(req, res);
        if (!auth_opt) return;
        const auto &auth = *auth_opt;
        if (auth.user.role != "sender") {
            res.status = 403;
            res.set_content(json{{"error", "only senders can confirm requests"}}.dump(), "application/json");
            return;
        }
        std::string request_id = req.matches[1];
        std::string sender_dataset_id;
        try {
            auto body = json::parse(req.body.empty() ? "{}" : req.body);
            sender_dataset_id = body.value("senderDatasetId", "");
        } catch (const std::exception &ex) {
            res.status = 400;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
            return;
        }
        if (sender_dataset_id.empty()) {
            res.status = 400;
            res.set_content(json{{"error", "senderDatasetId required"}}.dump(), "application/json");
            return;
        }

        Dataset receiver_dataset;
        Dataset sender_dataset;
        RequestRecord request_snapshot;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_requests.find(request_id);
            if (it == g_requests.end()) {
                res.status = 404;
                res.set_content(json{{"error", "request not found"}}.dump(), "application/json");
                return;
            }
            if (it->second.counterparty != auth.user.username) {
                res.status = 403;
                res.set_content(json{{"error", "request not assigned to this sender"}}.dump(), "application/json");
                return;
            }
            if (it->second.status == "cancelled") {
                res.status = 400;
                res.set_content(json{{"error", "request cancelled"}}.dump(), "application/json");
                return;
            }
            if (it->second.status == "running") {
                res.status = 409;
                res.set_content(json{{"error", "request already running"}}.dump(), "application/json");
                return;
            }
            if (!it->second.sender_dataset_id.empty() && it->second.sender_dataset_id != sender_dataset_id) {
                // allow reusing previous dataset id
                sender_dataset_id = it->second.sender_dataset_id;
            }
            auto receiver_it = g_datasets.find(it->second.receiver_dataset_id);
            if (receiver_it == g_datasets.end()) {
                res.status = 404;
                res.set_content(json{{"error", "receiver dataset not found"}}.dump(), "application/json");
                return;
            }
            auto sender_it = g_datasets.find(sender_dataset_id);
            if (sender_it == g_datasets.end()) {
                res.status = 404;
                res.set_content(json{{"error", "sender dataset not found"}}.dump(), "application/json");
                return;
            }
            if (sender_it->second.owner != auth.user.username) {
                res.status = 403;
                res.set_content(json{{"error", "dataset does not belong to sender"}}.dump(), "application/json");
                return;
            }

            Dataset receiver_copy = receiver_it->second;
            Dataset sender_copy = sender_it->second;

            RequestRecord before = it->second;
            it->second.sender_dataset_id = sender_dataset_id;
            it->second.status = "running";
            it->second.updated_at = now_iso_string();
            it->second.error_message.clear();

            try {
                save_state_locked();
            } catch (const std::exception &ex) {
                it->second = before;
                res.status = 500;
                res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
                return;
            }

            receiver_dataset = std::move(receiver_copy);
            sender_dataset = std::move(sender_copy);
            request_snapshot = it->second;
        }

        SessionRecord session_result;
        std::string error_message;
        bool success = false;
        try {
            std::size_t receiver_size = receiver_dataset.elements.size();
            std::size_t threshold = request_snapshot.threshold;
            if (threshold == 0 && receiver_size > 0) {
                double ratio = request_snapshot.threshold_ratio > 0.0 ? request_snapshot.threshold_ratio : 0.95;
                threshold = static_cast<std::size_t>(std::round(receiver_size * ratio));
                if (threshold == 0) threshold = 1;
            }
            threshold = std::min(threshold, receiver_size > 0 ? receiver_size : threshold);
            double ratio = request_snapshot.threshold_ratio;
            if (ratio <= 0.0 && receiver_size > 0) {
                ratio = threshold > 0 ? static_cast<double>(threshold) / static_cast<double>(receiver_size) : 0.0;
            }
            std::uint64_t modulus = request_snapshot.modulus ? request_snapshot.modulus : receiver_dataset.modulus;
            session_result = run_session(receiver_dataset,
                                         sender_dataset,
                                         threshold,
                                         ratio,
                                         modulus,
                                         request_snapshot.notes);
            session_result.receiver_account = receiver_dataset.owner;
            session_result.sender_account = sender_dataset.owner;
            session_result.request_id = request_snapshot.id;
            session_result.updated_at = now_iso_string();
            success = true;
        } catch (const std::exception &ex) {
            error_message = ex.what();
        }

        json response;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_requests.find(request_id);
            if (it == g_requests.end()) {
                res.status = 404;
                res.set_content(json{{"error", "request not found"}}.dump(), "application/json");
                return;
            }
            if (success) {
                if (session_result.threshold_reached) {
                    it->second.status = "completed";
                    it->second.error_message.clear();
                } else {
                    it->second.status = "threshold-not-met";
                    it->second.error_message = "threshold not met";
                }
                it->second.session_id = session_result.id;
                it->second.updated_at = session_result.updated_at;
                g_sessions[session_result.id] = session_result;
                response["session"] = session_detail_to_json(session_result);
            } else {
                it->second.status = "failed";
                it->second.error_message = error_message.empty() ? "unknown error" : error_message;
                it->second.updated_at = now_iso_string();
            }
            response["request"] = request_to_json(it->second);
            try {
                save_state_locked();
            } catch (const std::exception &ex) {
                res.status = 500;
                res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
                return;
            }
        }

        if (!success) {
            if (!response.contains("error")) {
                response["error"] = error_message.empty() ? "execution failed" : error_message;
            }
        }
        res.set_content(response.dump(), "application/json");
    });

    server.Post(R"(/api/requests/(.+)/reject)", [](const httplib::Request &req, httplib::Response &res) {
        auto auth_opt = require_auth(req, res);
        if (!auth_opt) return;
        const auto &auth = *auth_opt;
        if (auth.user.role != "sender") {
            res.status = 403;
            res.set_content(json{{"error", "only senders can reject requests"}}.dump(), "application/json");
            return;
        }
        std::string request_id = req.matches[1];
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_requests.find(request_id);
        if (it == g_requests.end()) {
            res.status = 404;
            res.set_content(json{{"error", "request not found"}}.dump(), "application/json");
            return;
        }
        if (it->second.counterparty != auth.user.username) {
            res.status = 403;
            res.set_content(json{{"error", "request not assigned to this sender"}}.dump(), "application/json");
            return;
        }
        it->second.status = "sender-rejected";
        it->second.updated_at = now_iso_string();
        it->second.error_message.clear();
        try {
            save_state_locked();
        } catch (const std::exception &ex) {
            res.status = 500;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
            return;
        }
        res.set_content(json{{"request", request_to_json(it->second)}}.dump(), "application/json");
    });

    server.Post(R"(/api/requests/(.+)/override)", [](const httplib::Request &req, httplib::Response &res) {
        auto auth_opt = require_auth(req, res);
        if (!auth_opt) return;
        const auto &auth = *auth_opt;
        if (auth.user.role != "admin") {
            res.status = 403;
            res.set_content(json{{"error", "admin required"}}.dump(), "application/json");
            return;
        }
        std::string request_id = req.matches[1];
        std::optional<std::uint64_t> new_modulus;
        std::optional<std::size_t> new_threshold;
        std::optional<std::string> new_notes;
        try {
            if (!req.body.empty()) {
                auto body = json::parse(req.body);
                if (body.contains("modulus")) new_modulus = body["modulus"].get<std::uint64_t>();
                if (body.contains("threshold")) new_threshold = body["threshold"].get<std::size_t>();
                if (body.contains("notes")) new_notes = body["notes"].get<std::string>();
            }
        } catch (const std::exception &ex) {
            res.status = 400;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
            return;
        }
        if (!new_modulus && !new_threshold && !new_notes) {
            res.status = 400;
            res.set_content(json{{"error", "no parameters provided"}}.dump(), "application/json");
            return;
        }

        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_requests.find(request_id);
        if (it == g_requests.end()) {
            res.status = 404;
            res.set_content(json{{"error", "request not found"}}.dump(), "application/json");
            return;
        }
        if (it->second.status == "running") {
            res.status = 409;
            res.set_content(json{{"error", "cannot override while running"}}.dump(), "application/json");
            return;
        }
        if (new_modulus) {
            if (*new_modulus == 0) {
                res.status = 400;
                res.set_content(json{{"error", "modulus must be positive"}}.dump(), "application/json");
                return;
            }
            it->second.modulus = *new_modulus;
        }
        if (new_threshold) {
            it->second.threshold = *new_threshold;
            auto ds_it = g_datasets.find(it->second.receiver_dataset_id);
            if (ds_it != g_datasets.end() && !ds_it->second.elements.empty()) {
                it->second.threshold_ratio = static_cast<double>(*new_threshold) /
                                             static_cast<double>(ds_it->second.elements.size());
            }
        }
        if (new_notes) {
            it->second.notes = *new_notes;
        }
        it->second.updated_at = now_iso_string();
        try {
            save_state_locked();
        } catch (const std::exception &ex) {
            res.status = 500;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
            return;
        }
        res.set_content(json{{"request", request_to_json(it->second)}}.dump(), "application/json");
    });

    server.Post(R"(/api/requests/(.+)/abort)", [](const httplib::Request &req, httplib::Response &res) {
        auto auth_opt = require_auth(req, res);
        if (!auth_opt) return;
        const auto &auth = *auth_opt;
        if (auth.user.role != "admin") {
            res.status = 403;
            res.set_content(json{{"error", "admin required"}}.dump(), "application/json");
            return;
        }
        std::string request_id = req.matches[1];
        std::string reason;
        try {
            if (!req.body.empty()) {
                auto body = json::parse(req.body);
                reason = body.value("reason", "");
            }
        } catch (const std::exception &ex) {
            res.status = 400;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
            return;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_requests.find(request_id);
        if (it == g_requests.end()) {
            res.status = 404;
            res.set_content(json{{"error", "request not found"}}.dump(), "application/json");
            return;
        }
        if (it->second.status == "completed" || it->second.status == "failed" || it->second.status == "cancelled") {
            res.status = 409;
            res.set_content(json{{"error", "request already finalized"}}.dump(), "application/json");
            return;
        }
        if (it->second.status == "running") {
            res.status = 409;
            res.set_content(json{{"error", "request currently running"}}.dump(), "application/json");
            return;
        }
        it->second.status = "cancelled";
        it->second.error_message = reason;
        it->second.updated_at = now_iso_string();
        try {
            save_state_locked();
        } catch (const std::exception &ex) {
            res.status = 500;
            res.set_content(json{{"error", ex.what()}}.dump(), "application/json");
            return;
        }
        res.set_content(json{{"request", request_to_json(it->second)}}.dump(), "application/json");
    });

    const char *port_env = std::getenv("TPSI_SERVER_PORT");
    int port = port_env ? std::stoi(port_env) : 8080;
    const char *host_env = std::getenv("TPSI_SERVER_HOST");
    std::string host = host_env ? host_env : "0.0.0.0";

    std::printf("[tpsi_server] listening on %s:%d\n", host.c_str(), port);
    server.listen(host.c_str(), port);
    return 0;
}
