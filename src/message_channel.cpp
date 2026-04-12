#include "tpsi/message_channel.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace tpsi {
namespace {

constexpr std::size_t kHeaderSize = sizeof(std::uint8_t) + sizeof(std::uint32_t);

void throw_last_error(const char *what) {
    throw MessageError(std::string(what) + ": " + std::strerror(errno));
}

void set_reuseaddr(int fd) {
    int opt = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
        throw_last_error("setsockopt");
    }
}

void write_all(int fd, const void *buf, std::size_t len) {
    const std::uint8_t *ptr = static_cast<const std::uint8_t *>(buf);
    std::size_t remaining = len;
    while (remaining > 0) {
        const ssize_t written = ::send(fd, ptr, remaining, 0);
        if (written < 0) {
            if (errno == EINTR) continue;
            throw_last_error("send");
        }
        if (written == 0) {
            throw MessageError("socket closed while sending");
        }
        remaining -= static_cast<std::size_t>(written);
        ptr += written;
    }
}

void read_all(int fd, void *buf, std::size_t len) {
    std::uint8_t *ptr = static_cast<std::uint8_t *>(buf);
    std::size_t remaining = len;
    while (remaining > 0) {
        const ssize_t received = ::recv(fd, ptr, remaining, 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            throw_last_error("recv");
        }
        if (received == 0) {
            throw MessageError("socket closed while receiving");
        }
        remaining -= static_cast<std::size_t>(received);
        ptr += received;
    }
}

} // namespace

MessageChannel::MessageChannel(int fd) : fd_(fd) {}

MessageChannel::MessageChannel(MessageChannel &&other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

MessageChannel &MessageChannel::operator=(MessageChannel &&other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

MessageChannel::~MessageChannel() {
    close();
}

void MessageChannel::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

MessageChannel MessageChannel::listen(std::uint16_t port) {
    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        throw_last_error("socket");
    }
    try {
        set_reuseaddr(server_fd);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        if (::bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
            throw_last_error("bind");
        }
        if (::listen(server_fd, 1) != 0) {
            throw_last_error("listen");
        }
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(server_fd,
                                 reinterpret_cast<sockaddr*>(&client_addr),
                                 &client_len);
        if (client_fd < 0) {
            throw_last_error("accept");
        }
        ::close(server_fd);
        return MessageChannel(client_fd);
    } catch (...) {
        ::close(server_fd);
        throw;
    }
}

MessageChannel MessageChannel::connect(const std::string &host, std::uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw_last_error("socket");
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        throw MessageError("inet_pton failed for host " + host);
    }
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        throw_last_error("connect");
    }
    return MessageChannel(fd);
}

std::pair<MessageChannel, MessageChannel> MessageChannel::create_pair() {
#if defined(_WIN32)
    throw MessageError("socketpair is not supported on Windows builds");
#else
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        throw_last_error("socketpair");
    }
    try {
        return {MessageChannel(fds[0]), MessageChannel(fds[1])};
    } catch (...) {
        ::close(fds[0]);
        ::close(fds[1]);
        throw;
    }
#endif
}

void MessageChannel::send(MessageType type, const std::vector<std::uint8_t> &payload) {
    if (!valid()) {
        throw MessageError("attempted to send on invalid channel");
    }
    const std::uint8_t type_byte = static_cast<std::uint8_t>(type);
    const std::uint32_t payload_len = static_cast<std::uint32_t>(payload.size());
    const std::uint32_t len_net = htonl(payload_len);
    std::uint8_t header[kHeaderSize];
    header[0] = type_byte;
    std::memcpy(header + sizeof(std::uint8_t), &len_net, sizeof(len_net));
    write_all(fd_, header, kHeaderSize);
    if (!payload.empty()) {
        write_all(fd_, payload.data(), payload.size());
    }
}

Message MessageChannel::receive() {
    if (!valid()) {
        throw MessageError("attempted to receive on invalid channel");
    }
    std::uint8_t header[kHeaderSize];
    read_all(fd_, header, kHeaderSize);
    const auto type = static_cast<MessageType>(header[0]);
    std::uint32_t len_net = 0;
    std::memcpy(&len_net, header + sizeof(std::uint8_t), sizeof(len_net));
    const std::uint32_t payload_len = ntohl(len_net);
    std::vector<std::uint8_t> payload(payload_len);
    if (payload_len > 0) {
        read_all(fd_, payload.data(), payload_len);
    }
    return Message{type, std::move(payload)};
}

} // namespace tpsi
