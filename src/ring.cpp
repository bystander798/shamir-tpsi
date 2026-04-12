#include "tpsi/ring.hpp"
#include "tpsi/random_provider.hpp"

#include <algorithm>
#include <stdexcept>

#include <NTL/lzz_pX.h>
#include <NTL/lzz_p.h>

namespace tpsi {

namespace {

class ZzpContextScope {
  public:
    explicit ZzpContextScope(std::uint64_t modulus)
        : ctx_(static_cast<long>(modulus)) {
        NTL::zz_p::UserFFTInit(static_cast<long>(modulus));
        guard_.save();
        ctx_.restore();
    }
    ~ZzpContextScope() { guard_.restore(); }

  private:
    NTL::zz_pBak guard_;
    NTL::zz_pContext ctx_;
};

NTL::zz_pX vector_to_poly(const std::vector<std::uint64_t> &coeffs,
                          std::size_t degree,
                          std::uint64_t modulus) {
    NTL::zz_pX poly;
    poly.SetMaxLength(static_cast<long>(degree));
    for (std::size_t i = 0; i < coeffs.size() && i < degree; ++i) {
        const long normalized = static_cast<long>(coeffs[i] % modulus);
        NTL::SetCoeff(poly, static_cast<long>(i), NTL::to_zz_p(normalized));
    }
    return poly;
}

std::vector<std::uint64_t> poly_to_vector(const NTL::zz_pX &poly,
                                          std::size_t degree,
                                          std::uint64_t modulus) {
    std::vector<std::uint64_t> coeffs(degree, 0);
    for (long i = 0; i <= NTL::deg(poly) && i < static_cast<long>(degree); ++i) {
        const long rep = NTL::rep(NTL::coeff(poly, i));
        std::uint64_t value = static_cast<std::uint64_t>(rep);
        value %= modulus;
        coeffs[static_cast<std::size_t>(i)] = value;
    }
    return coeffs;
}

NTL::zz_pX make_cyclic_modulus(std::size_t degree) {
    NTL::zz_pX modulus;
    NTL::SetCoeff(modulus, static_cast<long>(degree), NTL::to_zz_p(1));
    NTL::SetCoeff(modulus, 0, NTL::to_zz_p(-1));
    return modulus;
}

} // namespace

RingElement::RingElement(const RingConfig &cfg, std::vector<std::uint64_t> coeffs)
    : cfg_(cfg), coeffs_(std::move(coeffs)) {}

RingElement RingElement::add(const RingElement &other) const {
    auto result = coeffs_;
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = (result[i] + other.coeffs_[i]) % cfg_.modulus;
    }
    return RingElement(cfg_, std::move(result));
}

RingElement RingElement::sub(const RingElement &other) const {
    auto result = coeffs_;
    for (std::size_t i = 0; i < result.size(); ++i) {
        std::int64_t diff =
            static_cast<std::int64_t>(result[i]) - static_cast<std::int64_t>(other.coeffs_[i]);
        diff %= static_cast<std::int64_t>(cfg_.modulus);
        if (diff < 0) diff += cfg_.modulus;
        result[i] = static_cast<std::uint64_t>(diff);
    }
    return RingElement(cfg_, std::move(result));
}

RingElement RingElement::neg() const {
    auto result = coeffs_;
    for (auto &coeff : result) {
        coeff = (cfg_.modulus - coeff) % cfg_.modulus;
    }
    return RingElement(cfg_, std::move(result));
}

RingElement RingElement::mul(const RingElement &other) const {
    ZzpContextScope scope(cfg_.modulus);
    const NTL::zz_pX lhs = vector_to_poly(coeffs_, cfg_.degree, cfg_.modulus);
    const NTL::zz_pX rhs = vector_to_poly(other.coeffs_, cfg_.degree, cfg_.modulus);
    const NTL::zz_pX modulus_poly = make_cyclic_modulus(cfg_.degree);
    const NTL::zz_pX product = NTL::MulMod(lhs, rhs, modulus_poly);
    auto coeffs = poly_to_vector(product, cfg_.degree, cfg_.modulus);
    return RingElement(cfg_, std::move(coeffs));
}

RingElement RingElement::mul_scalar(std::uint64_t scalar) const {
    std::vector<std::uint64_t> result(coeffs_.size(), 0);
    for (std::size_t i = 0; i < coeffs_.size(); ++i) {
        unsigned __int128 prod = static_cast<unsigned __int128>(coeffs_[i]) * scalar;
        result[i] = static_cast<std::uint64_t>(prod % cfg_.modulus);
    }
    return RingElement(cfg_, std::move(result));
}

std::uint64_t RingElement::coefficient_at(std::size_t idx) const {
    if (idx >= coeffs_.size()) return 0;
    return coeffs_[idx];
}

PolynomialRing::PolynomialRing(RingConfig cfg) : cfg_(cfg) {}

RingElement PolynomialRing::zero() const {
    return RingElement(cfg_, std::vector<std::uint64_t>(cfg_.degree, 0));
}

RingElement PolynomialRing::random_uniform() {
    std::vector<std::uint64_t> coeffs(cfg_.degree, 0);
    for (auto &coeff : coeffs) {
        coeff = uniform_uint64(cfg_.modulus);
    }
    return RingElement(cfg_, std::move(coeffs));
}

RingElement PolynomialRing::random_noise(double probability) {
    std::vector<std::uint64_t> coeffs(cfg_.degree, 0);
    for (auto &coeff : coeffs) {
        const double sample = uniform_double();
        if (sample < probability) {
            coeff = (uniform_double() < 0.5) ? 1 : (cfg_.modulus - 1);
        }
    }
    return RingElement(cfg_, std::move(coeffs));
}

RingElement PolynomialRing::from_coefficients(const std::vector<std::uint64_t> &coeffs) const {
    if (coeffs.size() != cfg_.degree) {
        throw std::invalid_argument("coefficient vector size does not match ring degree");
    }
    std::vector<std::uint64_t> normalized(coeffs.size());
    for (std::size_t i = 0; i < coeffs.size(); ++i) {
        normalized[i] = coeffs[i] % cfg_.modulus;
    }
    return RingElement(cfg_, std::move(normalized));
}

RingElement PolynomialRing::constant(std::uint64_t value) const {
    std::vector<std::uint64_t> coeffs(cfg_.degree, 0);
    coeffs[0] = value % cfg_.modulus;
    return RingElement(cfg_, std::move(coeffs));
}

RingElement PolynomialRing::evaluate(const std::vector<std::uint64_t> &poly_coeffs,
                                     const RingElement &point) const {
    if (point.config().degree != cfg_.degree || point.config().modulus != cfg_.modulus) {
        throw std::invalid_argument("point ring configuration mismatch");
    }
    RingElement result = zero();
    for (std::size_t idx = poly_coeffs.size(); idx-- > 0;) {
        result = result.mul(point);
        result = result.add(constant(poly_coeffs[idx]));
    }
    return result;
}

} // namespace tpsi
