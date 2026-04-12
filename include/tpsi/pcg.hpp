#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <NTL/ZZ.h>
#include <NTL/ZZ_pX.h>

namespace tpsi {

inline constexpr std::size_t PCGSeedBytes = 16;

struct PCGParams {
    long n{0};
    long t{0};
    NTL::ZZ prime{0};
    NTL::ZZ_pX modpoly;
    std::size_t prime_bytes{0};
};

struct RRSender {
    NTL::ZZ_pX a;
    NTL::ZZ_pX b;
};

struct RRReceiver {
    NTL::ZZ_pX x;
    NTL::ZZ_pX c;
};

void pcg_init_field(PCGParams &params);

std::vector<unsigned char> csprng_bytes(std::size_t n);

std::vector<unsigned char> derive_instance_seed(const std::vector<unsigned char> &master_seed,
                                                std::uint64_t index);

NTL::ZZ_pX expand_poly_from_seed(const std::vector<unsigned char> &seed,
                                 std::uint64_t index,
                                 const PCGParams &params);

NTL::ZZ_pX sample_sparse_noise(const std::vector<unsigned char> &seed,
                               std::uint64_t index,
                               const PCGParams &params);
NTL::ZZ_pX sample_bernoulli_noise(const std::vector<unsigned char> &seed,
                                  std::uint64_t index,
                                  const PCGParams &params);

NTL::ZZ_pX poly_mod(const NTL::ZZ_pX &poly, const NTL::ZZ_pX &modulus);

std::uint64_t two_party_pcg_simulate(const std::vector<unsigned char> &seed_sender,
                                     const std::vector<unsigned char> &seed_receiver,
                                     const PCGParams &params,
                                     long batch_count,
                                     std::vector<RRSender> &sender_out,
                                     std::vector<RRReceiver> &receiver_out,
                                     bool verify_outputs,
                                     double *elapsed_seconds = nullptr,
                                     double *sender_elapsed_seconds = nullptr,
                                     double *receiver_elapsed_seconds = nullptr);

NTL::ZZ bytes_to_ZZ(const unsigned char *data, std::size_t len);

} // namespace tpsi
