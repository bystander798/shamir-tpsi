#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace tpsi {

class PolynomialBackend {
  public:
    virtual ~PolynomialBackend() = default;

    virtual std::vector<std::uint64_t> interpolate(const std::vector<std::uint64_t> &points,
                                                   const std::vector<std::uint64_t> &values) = 0;

    virtual std::vector<std::uint64_t> evaluate(const std::vector<std::uint64_t> &coeffs,
                                                const std::vector<std::uint64_t> &points) = 0;
};

PolynomialBackend &default_polynomial_backend();
void set_polynomial_backend(std::unique_ptr<PolynomialBackend> backend);
std::unique_ptr<PolynomialBackend> create_ntl_backend(std::uint64_t modulus);

#if SHAIR_TPSI_HAVE_FLINT
std::unique_ptr<PolynomialBackend> create_flint_backend(std::uint64_t modulus);
#endif

} // namespace tpsi
