#include "tpsi/rss.hpp"

#include "tpsi/polynomial.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>
#include <NTL/ZZ_pX.h>

namespace tpsi {

namespace {

struct HalfGcdTunables {
    long k_small;
    long min_drop;
    long min_effective_drop;
};

HalfGcdTunables derive_half_gcd_tunables(const RssConfig &cfg) {
    HalfGcdTunables tunables{256, 16, 8};
    if (cfg.half_gcd_k_small.has_value()) {
        tunables.k_small = static_cast<long>(cfg.half_gcd_k_small.value());
    }
    if (cfg.half_gcd_min_drop.has_value()) {
        tunables.min_drop = static_cast<long>(cfg.half_gcd_min_drop.value());
    }
    if (cfg.half_gcd_min_effective_drop.has_value()) {
        tunables.min_effective_drop = static_cast<long>(cfg.half_gcd_min_effective_drop.value());
    }
    tunables.k_small = std::max<long>(1, tunables.k_small);
    tunables.min_drop = std::max<long>(1, tunables.min_drop);
    tunables.min_effective_drop = std::max<long>(1, tunables.min_effective_drop);
    return tunables;
}

void classical_step(NTL::ZZ_pX &r0,
                    NTL::ZZ_pX &r1,
                    NTL::ZZ_pX &t0,
                    NTL::ZZ_pX &t1) {
    if (NTL::IsZero(r1)) {
        throw std::runtime_error("RSS reconstruction failed: degenerate remainder");
    }
    NTL::ZZ_pX quotient;
    NTL::ZZ_pX remainder;
    NTL::DivRem(quotient, remainder, r0, r1);
    NTL::ZZ_pX t_next = t0 - quotient * t1;
    r0 = r1;
    r1 = remainder;
    t0 = t1;
    t1 = t_next;
}

void classical_batch(NTL::ZZ_pX &r0,
                     NTL::ZZ_pX &r1,
                     NTL::ZZ_pX &t0,
                     NTL::ZZ_pX &t1,
                     long steps) {
    for (long i = 0; i < steps; ++i) {
        if (NTL::IsZero(r1)) {
            throw std::runtime_error("RSS reconstruction failed: degenerate remainder");
        }
        classical_step(r0, r1, t0, t1);
    }
}

void classical_to_targets(NTL::ZZ_pX &r0,
                          NTL::ZZ_pX &r1,
                          NTL::ZZ_pX &t0,
                          NTL::ZZ_pX &t1,
                          long q_bound,
                          long e_bound) {
    while ((NTL::deg(r1) > q_bound || NTL::deg(t1) > e_bound) && !NTL::IsZero(r1)) {
        classical_step(r0, r1, t0, t1);
    }
}

void apply_transform(const NTL::ZZ_pXMatrix &transform,
                     NTL::ZZ_pX &t0,
                     NTL::ZZ_pX &t1,
                     NTL::ZZ_pX &tmp0,
                     NTL::ZZ_pX &tmp1,
                     NTL::ZZ_pX &tmp2) {
    const auto required_length = [](long deg_a, long deg_b) -> long {
        const long safe_a = std::max<long>(deg_a, 0);
        const long safe_b = std::max<long>(deg_b, 0);
        return safe_a + safe_b + 2;
    };

    const long deg_t0 = NTL::deg(t0);
    const long deg_t1 = NTL::deg(t1);
    const long deg_m00 = NTL::deg(transform(0, 0));
    const long deg_m01 = NTL::deg(transform(0, 1));
    const long deg_m10 = NTL::deg(transform(1, 0));
    const long deg_m11 = NTL::deg(transform(1, 1));

    const long limit0 = std::max(required_length(deg_t0, deg_m00),
                                 required_length(deg_t1, deg_m01));
    const long limit1 = std::max(required_length(deg_t1, deg_m11),
                                 required_length(deg_t0, deg_m10));

    tmp0.SetMaxLength(limit0);
    tmp1.SetMaxLength(limit1);
    tmp2.SetMaxLength(limit1);

    NTL::mul(tmp0, transform(0, 0), t0);
    NTL::mul(tmp1, transform(0, 1), t1);
    tmp0 += tmp1;

    NTL::mul(tmp1, transform(1, 1), t1);
    NTL::mul(tmp2, transform(1, 0), t0);
    tmp1 += tmp2;

    t0 = tmp0;
    t1 = tmp1;
}

void build_subproduct_tree(const std::vector<NTL::ZZ_p> &xs,
                           std::vector<std::vector<NTL::ZZ_pX>> &tree) {
    tree.clear();
    if (xs.empty()) {
        return;
    }
    tree.emplace_back();
    auto &leaves = tree.back();
    leaves.reserve(xs.size());
    for (const auto &x : xs) {
        NTL::ZZ_pX poly;
        poly.SetMaxLength(2);
        NTL::SetCoeff(poly, 1, NTL::to_ZZ_p(1));
        NTL::SetCoeff(poly, 0, -x);
        leaves.push_back(std::move(poly));
    }
    std::size_t level = 0;
    while (tree[level].size() > 1) {
        const std::size_t current_size = tree[level].size();
        const std::size_t next_size = (current_size + 1) / 2;
        tree.emplace_back();
        auto &next = tree.back();
        next.reserve(next_size);
        for (std::size_t i = 0; i < next_size; ++i) {
            const std::size_t left = 2 * i;
            const std::size_t right = left + 1;
            NTL::ZZ_pX node = tree[level][left];
            if (right < current_size) {
                node *= tree[level][right];
            }
            next.push_back(std::move(node));
        }
        ++level;
    }
}

void remainder_tree_down(const NTL::ZZ_pX &f_mod_node,
                         const std::vector<std::vector<NTL::ZZ_pX>> &tree,
                         std::size_t level,
                         std::size_t index,
                         std::vector<NTL::ZZ_p> &out_values) {
    if (level == 0) {
        out_values[index] = NTL::ConstTerm(f_mod_node);
        return;
    }
    const auto &children = tree[level - 1];
    const std::size_t left = index * 2;
    const std::size_t right = left + 1;

    NTL::ZZ_pX left_rem;
    NTL::rem(left_rem, f_mod_node, children[left]);
    remainder_tree_down(left_rem, tree, level - 1, left, out_values);

    if (right < children.size()) {
        NTL::ZZ_pX right_rem;
        NTL::rem(right_rem, f_mod_node, children[right]);
        remainder_tree_down(right_rem, tree, level - 1, right, out_values);
    }
}

std::vector<NTL::ZZ_p> multipoint_evaluate(
    const NTL::ZZ_pX &f,
    const std::vector<std::vector<NTL::ZZ_pX>> &tree) {
    if (tree.empty()) {
        return {};
    }

    std::vector<NTL::ZZ_p> values(tree.front().size());
    NTL::ZZ_pX root_remainder;
    NTL::rem(root_remainder, f, tree.back().front());

    remainder_tree_down(root_remainder, tree, tree.size() - 1, 0, values);
    return values;
}

void reduce_half_gcd(NTL::ZZ_pX &r0,
                     NTL::ZZ_pX &r1,
                     NTL::ZZ_pX &t0,
                     NTL::ZZ_pX &t1,
                     long q_bound,
                     long e_bound,
                     const HalfGcdTunables &tunables) {
    // Ensure deg(r0) >= deg(r1) initially so that the Half-GCD fast path
    // (XHalfGCD) is entered immediately rather than the O(n²) classical
    // fallback.  When the two polynomials have equal degree the algorithm
    // would otherwise iterate classical_step O(n) times before delta > 0.
    if (NTL::deg(r0) < NTL::deg(r1)) {
        std::swap(r0, r1);
        std::swap(t0, t1);
    }

    NTL::ZZ_pXMatrix transform;
    NTL::ZZ_pX tmp0;
    NTL::ZZ_pX tmp1;
    NTL::ZZ_pX tmp2;

    while (true) {
        if (NTL::deg(r1) <= q_bound && NTL::deg(t1) <= e_bound) {
            return;
        }
        if (NTL::IsZero(r1)) {
            throw std::runtime_error("RSS reconstruction failed: degenerate remainder");
        }

        if (NTL::deg(r0) <= NTL::deg(r1)) {
            classical_step(r0, r1, t0, t1);
            continue;
        }

        const long deg_r1 = NTL::deg(r1);
        if (deg_r1 < tunables.k_small) {
            classical_to_targets(r0, r1, t0, t1, q_bound, e_bound);
            continue;
        }

        const long deg_r0 = NTL::deg(r0);
        const long delta = deg_r0 - deg_r1;
        const long need_drop_r = (deg_r1 > q_bound) ? (deg_r1 - q_bound) : 0;
        const long deg_t1 = NTL::deg(t1);
        const long need_drop_t = (deg_t1 > e_bound) ? (deg_t1 - e_bound) : 0;
        const long need_drop = std::max(need_drop_r, need_drop_t);
        long want_drop = std::max(tunables.min_drop, delta / 2);
        if (want_drop <= 0) {
            want_drop = 1;
        }
        long target_drop = std::max(want_drop, need_drop);
        const long max_drop = deg_r1 + 1;
        if (target_drop > max_drop) {
            target_drop = max_drop;
        }

        NTL::XHalfGCD(transform, r0, r1, target_drop);
        apply_transform(transform, t0, t1, tmp0, tmp1, tmp2);

        const long new_deg_r1 = NTL::deg(r1);
        const long drop = deg_r1 - new_deg_r1;
        if (drop < tunables.min_effective_drop) {
            const long remaining = std::max<long>(target_drop - drop, 0);
            if (remaining > 0) {
                classical_batch(r0, r1, t0, t1, remaining);
            }
        }
    }
}

PolyCoeffs reconstruct_berlekamp_welch(const FiniteField &field,
                                          const std::vector<Share> &points,
                                          const RssConfig &cfg) {
    if (points.empty()) {
        throw std::runtime_error("RSS reconstruction failed: no points");
    }

    const std::size_t degree_f = cfg.secret_degree;
    const std::size_t max_errors = cfg.max_errors;
    const std::size_t n = points.size();
    std::vector<std::uint64_t> xs;
    std::vector<std::uint64_t> ys;
    std::vector<std::uint64_t> xs_normalized;
    xs.reserve(n);
    ys.reserve(n);
    xs_normalized.reserve(n);
    for (const auto &pt : points) {
        const auto normalized_x = field.normalize(pt.x);
        const auto normalized_y = field.normalize(pt.y);
        xs.push_back(pt.x);
        ys.push_back(normalized_y);
        xs_normalized.push_back(normalized_x);
    }

    ZZpContextScope scope(field.modulus());

    const HalfGcdTunables tunables = derive_half_gcd_tunables(cfg);
    std::vector<NTL::ZZ_p> xs_field;
    xs_field.reserve(n);
    for (const auto value : xs_normalized) {
        xs_field.push_back(NTL::to_ZZ_p(to_ZZ(value)));
    }
    const NTL::ZZ_pX vanish = build_from_roots(field, xs);
    const NTL::ZZ_pX interp = interpolate(field, xs, ys);

    const long q_bound = static_cast<long>(degree_f + max_errors);
    const long e_bound = static_cast<long>(max_errors);

    NTL::ZZ_pX r0 = vanish;
    NTL::ZZ_pX r1 = interp;
    NTL::ZZ_pX t0;
    NTL::ZZ_pX t1;
    NTL::clear(t0);
    NTL::clear(t1);
    NTL::SetCoeff(t1, 0, NTL::to_ZZ_p(1));

    reduce_half_gcd(r0, r1, t0, t1, q_bound, e_bound, tunables);

    if (NTL::IsZero(t1)) {
        throw std::runtime_error("RSS reconstruction failed: zero error locator");
    }

    const NTL::ZZ_p lc = NTL::LeadCoeff(t1);
    if (NTL::IsZero(lc)) {
        throw std::runtime_error("RSS reconstruction failed: zero leading coefficient");
    }
    const NTL::ZZ_p inv_lc = NTL::inv(lc);
    r1 *= inv_lc;
    t1 *= inv_lc;

    if (NTL::deg(r1) > q_bound || NTL::deg(t1) > e_bound) {
        throw std::runtime_error("RSS reconstruction failed: degree bounds exceeded");
    }

    NTL::ZZ_pX f_poly;
    NTL::ZZ_pX remainder;
    NTL::DivRem(f_poly, remainder, r1, t1);
    if (!NTL::IsZero(remainder)) {
        throw std::runtime_error("RSS reconstruction failed: non-zero remainder");
    }
    if (NTL::deg(f_poly) > static_cast<long>(degree_f)) {
        throw std::runtime_error("RSS reconstruction failed: recovered polynomial too large");
    }

    auto f_coeffs = poly_to_coeffs(field, f_poly, degree_f + 1);

    std::vector<std::vector<NTL::ZZ_pX>> tree;
    build_subproduct_tree(xs_field, tree);
    const auto fx = multipoint_evaluate(f_poly, tree);

    const NTL::ZZ modulus = to_ZZ(field.modulus());
    std::size_t error_count = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const NTL::ZZ value_rep = NTL::rep(fx[i]) % modulus;
        const std::uint64_t value =
            static_cast<std::uint64_t>(NTL::conv<unsigned long>(value_rep));
        if (value != ys[i]) {
            ++error_count;
        }
    }
    if (error_count > max_errors) {
        throw std::runtime_error("RSS reconstruction failed: error count exceeds bound");
    }

    return f_coeffs;
}

} // namespace

PolyCoeffs rss_reconstruct(const FiniteField &field,
                              const std::vector<Share> &points,
                              RssConfig cfg) {
    const std::size_t degree_f = cfg.secret_degree;
    const std::size_t max_errors = cfg.max_errors;
    const std::size_t degree_q = degree_f + max_errors;
    const std::size_t unknowns = degree_q + 1 + max_errors;
    if (points.size() < unknowns) {
        throw std::invalid_argument("insufficient points for RSS");
    }

    return reconstruct_berlekamp_welch(field, points, cfg);
}

} // namespace tpsi
