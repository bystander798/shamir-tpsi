#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tpsi {

class AESCTRPrng {
  public:
    AESCTRPrng(const AESCTRPrng &) = delete;
    AESCTRPrng &operator=(const AESCTRPrng &) = delete;
    AESCTRPrng(AESCTRPrng &&) noexcept;
    AESCTRPrng &operator=(AESCTRPrng &&) noexcept;
    ~AESCTRPrng();

    static AESCTRPrng from_seed(const std::vector<std::uint8_t> &seed);
    static AESCTRPrng from_random();

    std::vector<std::uint8_t> random_bytes(std::size_t length);
    std::uint64_t random_uint64();
    std::uint64_t random_uint64(std::uint64_t modulus);

  private:
    AESCTRPrng();
    void init(const std::uint8_t *key, const std::uint8_t *iv);

    void *ctx_{nullptr};
};

} // namespace tpsi
