#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace tpsi {

enum class MessageType : std::uint8_t {
    CoinFlipCommit = 1,
    CoinFlipReveal = 2,
    OfflineData = 3,
    XStar = 4,
    SenderResponse = 5,
    Shutdown = 6,
};

struct Message {
    MessageType type;
    std::vector<std::uint8_t> payload;
};

class MessageChannel {
  public:
    MessageChannel() = default;
    MessageChannel(const MessageChannel &) = delete;
    MessageChannel &operator=(const MessageChannel &) = delete;
    MessageChannel(MessageChannel &&other) noexcept;
    MessageChannel &operator=(MessageChannel &&other) noexcept;
    ~MessageChannel();

    static MessageChannel listen(std::uint16_t port);
    static MessageChannel connect(const std::string &host, std::uint16_t port);
    static std::pair<MessageChannel, MessageChannel> create_pair();

    void send(MessageType type, const std::vector<std::uint8_t> &payload);
    Message receive();

    bool valid() const noexcept { return fd_ >= 0; }

  private:
    explicit MessageChannel(int fd);
    void close();
    int fd_{-1};
};

class MessageError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

} // namespace tpsi
