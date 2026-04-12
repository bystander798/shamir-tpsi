#include "tpsi/polynomial.hpp"

#include <NTL/vec_ZZ_p.h>

namespace tpsi {

namespace {

NTL::vec_ZZ_p to_vec(const FiniteField &field, const std::vector<std::uint64_t> &values) {
    NTL::vec_ZZ_p vec;
    vec.SetLength(static_cast<long>(values.size()));
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto normalized = field.normalize(values[i]);
        vec[static_cast<long>(i)] = NTL::to_ZZ_p(to_ZZ(normalized));
    }
    return vec;
}

void trim_trailing_zeroes(PolyCoeffs &coeffs) {
    while (!coeffs.empty() && coeffs.back() == 0) {
        coeffs.pop_back();
    }
}

} // namespace

ZZpContextScope::ZZpContextScope(std::uint64_t modulus)
    : ctx_(NTL::conv<NTL::ZZ>(modulus)) {
    guard_.save();
    ctx_.restore();
}

ZZpContextScope::~ZZpContextScope() {
    guard_.restore();
}

NTL::ZZ to_ZZ(std::uint64_t value) {
    return NTL::conv<NTL::ZZ>(value);
}

NTL::ZZ_pX coeffs_to_poly(const FiniteField &field, const PolyCoeffs &coeffs) {
    NTL::ZZ_pX poly;
    poly.SetMaxLength(static_cast<long>(coeffs.size()));
    for (std::size_t i = 0; i < coeffs.size(); ++i) {
        const std::uint64_t normalized = field.normalize(coeffs[i]);
        NTL::SetCoeff(poly, static_cast<long>(i), NTL::to_ZZ_p(to_ZZ(normalized)));
    }
    return poly;
}

PolyCoeffs poly_to_coeffs(const FiniteField &field, const NTL::ZZ_pX &poly, std::size_t size_hint) {
    const long degree = NTL::deg(poly);
    const std::size_t size = std::max<std::size_t>(size_hint, degree >= 0 ? degree + 1 : 0);
    PolyCoeffs coeffs(size, 0);
    const NTL::ZZ modulus = to_ZZ(field.modulus());
    for (std::size_t i = 0; i < size; ++i) {
        if (static_cast<long>(i) <= degree) {
            const NTL::ZZ rep = NTL::rep(NTL::coeff(poly, static_cast<long>(i)));
            coeffs[i] = static_cast<std::uint64_t>(NTL::conv<unsigned long>(rep % modulus));
        }
    }
    trim_trailing_zeroes(coeffs);
    if (coeffs.empty()) coeffs.push_back(0);
    return coeffs;
}

PolyCoeffs poly_add(const FiniteField &field, const PolyCoeffs &a, const PolyCoeffs &b) {
    const std::size_t size = std::max(a.size(), b.size());
    PolyCoeffs result(size, 0);
    for (std::size_t i = 0; i < size; ++i) {
        const std::uint64_t lhs = i < a.size() ? a[i] : 0;
        const std::uint64_t rhs = i < b.size() ? b[i] : 0;
        result[i] = field.add(lhs, rhs);
    }
    trim_trailing_zeroes(result);
    if (result.empty()) result.push_back(0);
    return result;
}

PolyCoeffs poly_mul(const FiniteField &field, const PolyCoeffs &a, const PolyCoeffs &b) {
    if (a.empty() || b.empty()) return {0};
    ZZpContextScope scope(field.modulus());
    const NTL::ZZ_pX pa = coeffs_to_poly(field, a);
    const NTL::ZZ_pX pb = coeffs_to_poly(field, b);
    const NTL::ZZ_pX product = pa * pb;
    return poly_to_coeffs(field, product, a.size() + b.size() - 1);
}

PolyCoeffs poly_from_root(const FiniteField &field, std::uint64_t root) {
    ZZpContextScope scope(field.modulus());
    NTL::ZZ_pX poly;
    NTL::SetCoeff(poly, 1, NTL::to_ZZ_p(1));
    NTL::SetCoeff(poly, 0, NTL::to_ZZ_p(field.sub(0, root)));
    return poly_to_coeffs(field, poly, 2);
}

PolyCoeffs build_zero_poly(const FiniteField &field, const std::vector<std::uint64_t> &roots) {
    ZZpContextScope scope(field.modulus());
    if (roots.empty()) return {1};
    const auto poly = build_from_roots(field, roots);
    return poly_to_coeffs(field, poly, roots.size() + 1);
}

std::uint64_t poly_eval(const FiniteField &field, const PolyCoeffs &poly, std::uint64_t x) {
    ZZpContextScope scope(field.modulus());
    const NTL::ZZ_pX ntl_poly = coeffs_to_poly(field, poly);
    const NTL::ZZ_p value = NTL::eval(ntl_poly, NTL::to_ZZ_p(to_ZZ(x)));
    return static_cast<std::uint64_t>(NTL::conv<unsigned long>(NTL::rep(value)));
}

PolyCoeffs random_polynomial(const FiniteField &field, std::size_t degree) {
    ZZpContextScope scope(field.modulus());
    NTL::ZZ_pX poly;
    poly.SetLength(static_cast<long>(degree) + 1);
    for (std::size_t i = 0; i <= degree; ++i) {
        NTL::SetCoeff(poly, static_cast<long>(i), NTL::random_ZZ_p());
    }
    if (NTL::IsZero(NTL::LeadCoeff(poly))) {
        NTL::SetCoeff(poly, static_cast<long>(degree), NTL::to_ZZ_p(1));
    }
    return poly_to_coeffs(field, poly, degree + 1);
}

NTL::ZZ_pX build_from_roots(const FiniteField &field, const std::vector<std::uint64_t> &roots) {
    ZZpContextScope scope(field.modulus());
    const auto vec_roots = to_vec(field, roots);
    NTL::ZZ_pX poly;
    NTL::BuildFromRoots(poly, vec_roots);
    return poly;
}

NTL::ZZ_pX interpolate(const FiniteField &field,
                       const std::vector<std::uint64_t> &xs,
                       const std::vector<std::uint64_t> &ys) {
    ZZpContextScope scope(field.modulus());
    const auto vec_x = to_vec(field, xs);
    const auto vec_y = to_vec(field, ys);
    NTL::ZZ_pX result;
    NTL::interpolate(result, vec_x, vec_y);
    return result;
}

} // namespace tpsi
