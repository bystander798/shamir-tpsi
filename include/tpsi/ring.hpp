#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tpsi {

struct RingConfig {
    std::size_t degree;
    std::uint64_t modulus;
};

class RingElement {
  public:
    RingElement(const RingConfig &cfg, std::vector<std::uint64_t> coeffs);

    const RingConfig &config() const noexcept { return cfg_; }
    const std::vector<std::uint64_t> &coefficients() const noexcept { return coeffs_; }

    RingElement add(const RingElement &other) const;
    RingElement sub(const RingElement &other) const;
    RingElement mul(const RingElement &other) const;
    RingElement neg() const;
    RingElement mul_scalar(std::uint64_t scalar) const;
    std::uint64_t coefficient_at(std::size_t idx) const;

  private:
    RingConfig cfg_;
    std::vector<std::uint64_t> coeffs_;
};

class PolynomialRing {
  public:
    explicit PolynomialRing(RingConfig cfg);

    RingElement zero() const;
    RingElement random_uniform();
    RingElement random_noise(double probability);
    RingElement from_coefficients(const std::vector<std::uint64_t> &coeffs) const;
    RingElement constant(std::uint64_t value) const;
    RingElement evaluate(const std::vector<std::uint64_t> &poly_coeffs,
                         const RingElement &point) const;

    const RingConfig &config() const noexcept { return cfg_; }

  private:
    RingConfig cfg_;
};

} // namespace tpsi
