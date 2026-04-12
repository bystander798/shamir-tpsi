#include "tpsi/shamir.hpp"
#include "tpsi/polynomial_backend.hpp"
#include "tpsi/random_provider.hpp"

#include <stdexcept>

namespace tpsi {

FiniteField::FiniteField(FieldConfig cfg) : cfg_(cfg) {
    set_polynomial_backend(create_ntl_backend(cfg_.modulus));
}

std::uint64_t FiniteField::normalize(std::uint64_t a) const {
    return a % cfg_.modulus;
}

std::uint64_t FiniteField::add(std::uint64_t a, std::uint64_t b) const {
    return normalize(a + b);
}

std::uint64_t FiniteField::sub(std::uint64_t a, std::uint64_t b) const {
    std::int64_t diff = static_cast<std::int64_t>(a) - static_cast<std::int64_t>(b);
    diff %= static_cast<std::int64_t>(cfg_.modulus);
    if (diff < 0) diff += cfg_.modulus;
    return static_cast<std::uint64_t>(diff);
}

std::uint64_t FiniteField::mul(std::uint64_t a, std::uint64_t b) const {
    unsigned __int128 prod = static_cast<unsigned __int128>(normalize(a)) *
                             static_cast<unsigned __int128>(normalize(b));
    return static_cast<std::uint64_t>(prod % cfg_.modulus);
}

std::uint64_t FiniteField::inv(std::uint64_t a) const {
    if (a == 0) throw std::runtime_error("zero has no inverse");
    std::int64_t t = 0, new_t = 1;
    std::int64_t r = static_cast<std::int64_t>(cfg_.modulus), new_r = static_cast<std::int64_t>(a);
    while (new_r != 0) {
        std::int64_t q = r / new_r;
        std::int64_t tmp_t = t - q * new_t;
        t = new_t;
        new_t = tmp_t;
        std::int64_t tmp_r = r - q * new_r;
        r = new_r;
        new_r = tmp_r;
    }
    if (r > 1) throw std::runtime_error("element not invertible");
    if (t < 0) t += cfg_.modulus;
    return static_cast<std::uint64_t>(t);
}

std::vector<std::uint64_t> generate_threshold_polynomial(const FiniteField &field,
                                                         std::uint64_t secret,
                                                         std::size_t threshold) {
    if (threshold == 0) throw std::invalid_argument("threshold must be positive");
    std::vector<std::uint64_t> coeffs(threshold);
    coeffs[0] = field.normalize(secret);
    for (std::size_t i = 1; i < threshold; ++i) {
        coeffs[i] = uniform_uint64(field.modulus());

    }
    return coeffs;
}

} // namespace tpsi
