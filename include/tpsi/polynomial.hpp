#pragma once

#include "tpsi/shamir.hpp"

#include <NTL/ZZ_pX.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tpsi {

using PolyCoeffs = std::vector<std::uint64_t>;

class ZZpContextScope {
  public:
    explicit ZZpContextScope(std::uint64_t modulus);
    ~ZZpContextScope();

    ZZpContextScope(const ZZpContextScope &) = delete;
    ZZpContextScope &operator=(const ZZpContextScope &) = delete;

  private:
    NTL::ZZ_pBak guard_;
    NTL::ZZ_pContext ctx_;
};

NTL::ZZ to_ZZ(std::uint64_t value);

NTL::ZZ_pX coeffs_to_poly(const FiniteField &field, const PolyCoeffs &coeffs);
PolyCoeffs poly_to_coeffs(const FiniteField &field, const NTL::ZZ_pX &poly, std::size_t size_hint);

PolyCoeffs poly_add(const FiniteField &field, const PolyCoeffs &a, const PolyCoeffs &b);
PolyCoeffs poly_mul(const FiniteField &field, const PolyCoeffs &a, const PolyCoeffs &b);
PolyCoeffs poly_from_root(const FiniteField &field, std::uint64_t root);
PolyCoeffs build_zero_poly(const FiniteField &field, const std::vector<std::uint64_t> &roots);
std::uint64_t poly_eval(const FiniteField &field, const PolyCoeffs &poly, std::uint64_t x);
PolyCoeffs random_polynomial(const FiniteField &field, std::size_t degree);

NTL::ZZ_pX build_from_roots(const FiniteField &field, const std::vector<std::uint64_t> &roots);
NTL::ZZ_pX interpolate(const FiniteField &field,
                       const std::vector<std::uint64_t> &xs,
                       const std::vector<std::uint64_t> &ys);

} // namespace tpsi

