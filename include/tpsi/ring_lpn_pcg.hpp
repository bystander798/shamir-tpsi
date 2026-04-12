#pragma once

#include "tpsi/pcg.hpp"
#include "tpsi/ring.hpp"
#include <cstddef>
#include <vector>

namespace tpsi {

struct RandomCorrelation {
    RingElement a_prime;
    RingElement b_prime;
    RingElement x_prime;
    RingElement c_prime;
};

struct PCGConfig {
    RingConfig ring;
    std::size_t sparse_weight{0};
    bool verify{false};
};

struct PCGRunStats {
    double sender_compute_ms{0.0};
    double receiver_compute_ms{0.0};
    std::size_t seed_bytes{0};
};

class RingLpnPCG {
  public:
    explicit RingLpnPCG(PCGConfig cfg);

    std::vector<RandomCorrelation> generate(std::size_t count, PCGRunStats *stats = nullptr);

  private:
    PCGConfig cfg_;
    PCGParams params_;
};

} // namespace tpsi
