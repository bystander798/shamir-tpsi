#include "tpsi/aes_ctr_prng.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <limits>
#include <stdexcept>

namespace tpsi {
namespace {
constexpr std::size_t KEY_SIZE = 32;   // AES-256
constexpr std::size_t IV_SIZE = 16;
} // namespace

AESCTRPrng::AESCTRPrng() = default;

AESCTRPrng::AESCTRPrng(AESCTRPrng &&other) noexcept { ctx_ = other.ctx_; other.ctx_ = nullptr; }

AESCTRPrng &AESCTRPrng::operator=(AESCTRPrng &&other) noexcept {
    if (this != &other) {
        if (ctx_) {
            EVP_CIPHER_CTX_free(reinterpret_cast<EVP_CIPHER_CTX *>(ctx_));
        }
        ctx_ = other.ctx_;
        other.ctx_ = nullptr;
    }
    return *this;
}

AESCTRPrng::~AESCTRPrng() {
    if (ctx_) {
        EVP_CIPHER_CTX_free(reinterpret_cast<EVP_CIPHER_CTX *>(ctx_));
    }
}

void AESCTRPrng::init(const std::uint8_t *key, const std::uint8_t *iv) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("failed to allocate EVP_CIPHER_CTX");
    }
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), nullptr, key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptInit_ex failed");
    }
    ctx_ = ctx;
}

AESCTRPrng AESCTRPrng::from_seed(const std::vector<std::uint8_t> &seed) {
    if (seed.size() < KEY_SIZE + IV_SIZE) {
        throw std::invalid_argument("seed too short for AES-CTR");
    }
    AESCTRPrng prng;
    prng.init(seed.data(), seed.data() + KEY_SIZE);
    return prng;
}

AESCTRPrng AESCTRPrng::from_random() {
    std::array<std::uint8_t, KEY_SIZE + IV_SIZE> seed{};
    if (RAND_bytes(seed.data(), static_cast<int>(seed.size())) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    return from_seed({seed.begin(), seed.end()});
}

std::vector<std::uint8_t> AESCTRPrng::random_bytes(std::size_t length) {
    std::vector<std::uint8_t> output(length, 0);
    std::vector<std::uint8_t> plaintext(length, 0);
    int out_len = 0;
    if (EVP_EncryptUpdate(reinterpret_cast<EVP_CIPHER_CTX *>(ctx_),
                           output.data(), &out_len,
                           plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
        throw std::runtime_error("EVP_EncryptUpdate failed");
    }
    return output;
}

std::uint64_t AESCTRPrng::random_uint64() {
    auto bytes = random_bytes(sizeof(std::uint64_t));
    std::uint64_t value = 0;
    for (auto b : bytes) {
        value = (value << 8) | static_cast<std::uint64_t>(b);
    }
    return value;
}

std::uint64_t AESCTRPrng::random_uint64(std::uint64_t modulus) {
    if (modulus == 0) {
        return random_uint64();
    }
    std::uint64_t limit = std::numeric_limits<std::uint64_t>::max() -
                          (std::numeric_limits<std::uint64_t>::max() % modulus);
    while (true) {
        auto candidate = random_uint64();
        if (candidate < limit) {
            return candidate % modulus;
        }
    }
}

} // namespace tpsi
