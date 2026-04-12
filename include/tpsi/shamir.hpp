#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tpsi {

struct FieldConfig {
    std::uint64_t modulus;
};

// Smallest known NTL FFT-friendly prime supporting 2^22 root of unity (sufficient for n <= 2^20).
inline constexpr std::uint64_t kDefaultFieldModulus = 1'152'921'504'204'193'793;

class FiniteField {
  public:
    explicit FiniteField(FieldConfig cfg);

    std::uint64_t add(std::uint64_t a, std::uint64_t b) const;
    std::uint64_t sub(std::uint64_t a, std::uint64_t b) const;
    std::uint64_t mul(std::uint64_t a, std::uint64_t b) const;
    std::uint64_t inv(std::uint64_t a) const;
    std::uint64_t normalize(std::uint64_t a) const;

    std::uint64_t modulus() const noexcept { return cfg_.modulus; }

  private:
    FieldConfig cfg_;
};

struct Share {
    std::uint64_t x;
    std::uint64_t y;
};

std::vector<std::uint64_t> generate_threshold_polynomial(const FiniteField &field,
                                                         std::uint64_t secret,
                                                         std::size_t threshold);

} // namespace tpsi
