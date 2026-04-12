#pragma once

#include "tpsi/aes_ctr_prng.hpp"
#include <cstdint>

namespace tpsi {

AESCTRPrng &global_prng();

std::uint64_t uniform_uint64(std::uint64_t modulus);
std::uint64_t random_uint64();
double uniform_double();

} // namespace tpsi
