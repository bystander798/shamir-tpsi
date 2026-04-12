#include "tpsi/random_provider.hpp"

#include <openssl/rand.h>

#include <array>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace tpsi {
namespace {
std::once_flag init_flag;
AESCTRPrng *global_instance = nullptr;

void initialize_prng() {
    std::array<std::uint8_t, 48> seed{}; // KEY(32) + IV(16)
    if (RAND_bytes(seed.data(), static_cast<int>(seed.size())) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    auto prng = AESCTRPrng::from_seed({seed.begin(), seed.end()});
    global_instance = new AESCTRPrng(std::move(prng));
}
} // namespace

AESCTRPrng &global_prng() {
    std::call_once(init_flag, initialize_prng);
    return *global_instance;
}

std::uint64_t random_uint64() {
    return global_prng().random_uint64();
}

std::uint64_t uniform_uint64(std::uint64_t modulus) {
    return global_prng().random_uint64(modulus);
}

double uniform_double() {
    constexpr auto denom = static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
    auto value = static_cast<long double>(random_uint64());
    return static_cast<double>(value / denom);
}

} // namespace tpsi
