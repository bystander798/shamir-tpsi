#include "tpsi/ring_lpn_pcg.hpp"
#include "tpsi/pcg.hpp"
#include "tpsi/random_provider.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <vector>
#include <utility>

#include <NTL/ZZ_pX.h>

namespace tpsi {
namespace {

NTL::ZZ to_ZZ(std::uint64_t value) {
    return NTL::conv<NTL::ZZ>(value);
}

// 缺省使用 X^n - 1 作为循环多项式模（论文里常见情形之一）
NTL::ZZ_pX make_cyclic_modulus(std::size_t degree) {
    NTL::ZZ_pX poly;
    NTL::SetCoeff(poly, static_cast<long>(degree), NTL::to_ZZ_p(1));
    NTL::SetCoeff(poly, 0, NTL::to_ZZ_p(-1));
    return poly;
}

std::size_t default_sparse_weight(const RingConfig &cfg) {
    // 默认噪声权重：n/8（可根据安全级别调整，保持接口不变）
    const std::size_t baseline = std::max<std::size_t>(1, cfg.degree / 8);
    return std::clamp<std::size_t>(baseline, 1, cfg.degree);
}

std::vector<std::uint64_t> poly_to_coefficients(const NTL::ZZ_pX &poly,
                                                std::size_t degree,
                                                std::uint64_t modulus) {
    std::vector<std::uint64_t> coeffs(degree, 0);
    for (long i = 0; i <= NTL::deg(poly) && i < static_cast<long>(degree); ++i) {
        const NTL::ZZ rep = NTL::rep(NTL::coeff(poly, i));
        std::uint64_t value = static_cast<std::uint64_t>(NTL::conv<unsigned long>(rep));
        value %= modulus;
        coeffs[static_cast<std::size_t>(i)] = value;
    }
    return coeffs;
}

} // namespace

RingLpnPCG::RingLpnPCG(PCGConfig cfg) : cfg_(std::move(cfg)) {
    // 1) 设定 F_p
    if (cfg_.ring.modulus < 2) throw std::runtime_error("ring modulus (prime) must be >= 2");
    NTL::ZZ_p::init(to_ZZ(cfg_.ring.modulus));

    // 2) 填充 PCGParams（与 pcg.hpp 对齐）
    params_.n = static_cast<long>(cfg_.ring.degree);

    // 稀疏噪声权重（固定汉明重）
    const std::size_t weight =
        cfg_.sparse_weight == 0 ? default_sparse_weight(cfg_.ring) : cfg_.sparse_weight;
    params_.t = static_cast<long>(std::min<std::size_t>(weight, cfg_.ring.degree));
    params_.prime = to_ZZ(cfg_.ring.modulus);

    // 初始化域参数
    pcg_init_field(params_);

    // 3) 选择模多项式（X^n - 1）
    params_.modpoly = make_cyclic_modulus(cfg_.ring.degree);
}

std::vector<RandomCorrelation> RingLpnPCG::generate(std::size_t count, PCGRunStats *stats) {
    const std::vector<unsigned char> seed_sender = csprng_bytes(PCGSeedBytes);
    const std::vector<unsigned char> seed_receiver = csprng_bytes(PCGSeedBytes);

    std::vector<RRSender> sender_out;
    std::vector<RRReceiver> receiver_out;

    double sender_seconds = 0.0;
    double receiver_seconds = 0.0;
    two_party_pcg_simulate(seed_sender,
                           seed_receiver,
                           params_,
                           static_cast<long>(count),
                           sender_out,
                           receiver_out,
                           cfg_.verify,
                           nullptr,
                           &sender_seconds,
                           &receiver_seconds);

    PolynomialRing ring(cfg_.ring);
    std::vector<RandomCorrelation> correlations;
    correlations.reserve(count);

    double sender_convert_seconds = 0.0;
    double receiver_convert_seconds = 0.0;

    for (std::size_t i = 0; i < count; ++i) {
        const auto sender_convert_start = std::chrono::high_resolution_clock::now();
        auto a_coeffs =
            poly_to_coefficients(sender_out[i].a, cfg_.ring.degree, cfg_.ring.modulus);
        auto b_coeffs =
            poly_to_coefficients(sender_out[i].b, cfg_.ring.degree, cfg_.ring.modulus);
        RingElement a_prime = ring.from_coefficients(a_coeffs);
        RingElement b_prime = ring.from_coefficients(b_coeffs);
        const auto sender_convert_end = std::chrono::high_resolution_clock::now();
        sender_convert_seconds += std::chrono::duration_cast<std::chrono::duration<double>>(
                                      sender_convert_end - sender_convert_start)
                                      .count();

        const auto receiver_convert_start = std::chrono::high_resolution_clock::now();
        auto x_coeffs =
            poly_to_coefficients(receiver_out[i].x, cfg_.ring.degree, cfg_.ring.modulus);
        auto c_coeffs =
            poly_to_coefficients(receiver_out[i].c, cfg_.ring.degree, cfg_.ring.modulus);
        RingElement x_prime = ring.from_coefficients(x_coeffs);
        RingElement c_prime = ring.from_coefficients(c_coeffs);
        const auto receiver_convert_end = std::chrono::high_resolution_clock::now();
        receiver_convert_seconds += std::chrono::duration_cast<std::chrono::duration<double>>(
                                        receiver_convert_end - receiver_convert_start)
                                        .count();

        correlations.push_back({std::move(a_prime),
                                std::move(b_prime),
                                std::move(x_prime),
                                std::move(c_prime)});
    }

    if (stats) {
        stats->sender_compute_ms = (sender_seconds + sender_convert_seconds) * 1000.0;
        stats->receiver_compute_ms = (receiver_seconds + receiver_convert_seconds) * 1000.0;
        stats->seed_bytes = seed_sender.size() + seed_receiver.size();
    }

    return correlations;
}

} // namespace tpsi
