#pragma once

#include "tpsi/polynomial.hpp"
#include "tpsi/shamir.hpp"
#include <optional>
#include <vector>

namespace tpsi {

struct RssConfig {
    std::size_t secret_degree;
    std::size_t max_errors;
    std::optional<std::size_t> half_gcd_k_small;
    std::optional<std::size_t> half_gcd_min_drop;
    std::optional<std::size_t> half_gcd_min_effective_drop;
};

PolyCoeffs rss_reconstruct(const FiniteField &field,
                          const std::vector<Share> &points,
                          RssConfig cfg);

} // namespace tpsi
