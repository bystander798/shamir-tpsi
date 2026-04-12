#include "tpsi/polynomial_backend.hpp"
#include "tpsi/shamir.hpp"

#include <memory>
#include <mutex>
#include <stdexcept>
#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pX.h>

namespace tpsi {

namespace {

class NTLPolynomialBackend : public PolynomialBackend {
  public:
    explicit NTLPolynomialBackend(std::uint64_t modulus) : modulus_(modulus) {
        NTL::ZZ_p::init(NTL::conv<NTL::ZZ>(modulus_));
    }

    std::vector<std::uint64_t> interpolate(const std::vector<std::uint64_t> &points,
                                           const std::vector<std::uint64_t> &values) override {
        const std::size_t n = points.size();
        if (n != values.size()) {
            throw std::invalid_argument("points/values size mismatch");
        }
        NTL::ZZ_pX poly;
        poly.SetLength(1);
        poly[0] = NTL::to_ZZ_p(0);
        for (std::size_t i = 0; i < n; ++i) {
            NTL::ZZ_pX basis;
            basis.SetLength(1);
            basis[0] = NTL::to_ZZ_p(1);
            NTL::ZZ_p denom = NTL::to_ZZ_p(1);
            for (std::size_t j = 0; j < n; ++j) {
                if (i == j) continue;
                NTL::ZZ_pX term;
                term.SetLength(2);
                term[1] = NTL::to_ZZ_p(1);
                std::uint64_t pj = points[j] % modulus_;
                std::uint64_t neg = (modulus_ + modulus_ - pj) % modulus_;
                term[0] = NTL::to_ZZ_p(NTL::conv<NTL::ZZ>(static_cast<long>(neg)));
                basis *= term;
                std::int64_t diff = static_cast<std::int64_t>(points[i] % modulus_) -
                                    static_cast<std::int64_t>(points[j] % modulus_);
                if (diff < 0) diff += static_cast<std::int64_t>(modulus_);
                denom *= NTL::to_ZZ_p(NTL::conv<NTL::ZZ>(static_cast<long>(diff)));
            }
            NTL::ZZ_p coeff =
                NTL::to_ZZ_p(NTL::conv<NTL::ZZ>(static_cast<long>(values[i] % modulus_)));
            coeff /= denom;
            poly += basis * coeff;
        }
        const long deg = NTL::deg(poly);
        std::vector<std::uint64_t> coeffs(static_cast<std::size_t>(deg + 1), 0);
        for (long i = 0; i <= deg; ++i) {
            coeffs[static_cast<std::size_t>(i)] =
                static_cast<std::uint64_t>(NTL::conv<unsigned long>(NTL::rep(poly[i])));
        }
        return coeffs;
    }

    std::vector<std::uint64_t> evaluate(const std::vector<std::uint64_t> &coeffs,
                                        const std::vector<std::uint64_t> &points) override {
        NTL::ZZ_pX poly;
        poly.SetLength(static_cast<long>(coeffs.size()));
        for (std::size_t i = 0; i < coeffs.size(); ++i) {
            poly[static_cast<long>(i)] =
                NTL::to_ZZ_p(NTL::conv<NTL::ZZ>(static_cast<long>(coeffs[i] % modulus_)));
        }
        std::vector<std::uint64_t> values(points.size(), 0);
        for (std::size_t i = 0; i < points.size(); ++i) {
            NTL::ZZ_p val = NTL::eval(poly, NTL::to_ZZ_p(NTL::conv<NTL::ZZ>(static_cast<long>(points[i]))));
            values[i] =
                static_cast<std::uint64_t>(NTL::conv<unsigned long>(NTL::rep(val)));
        }
        return values;
    }

  private:
    std::uint64_t modulus_;
};

std::unique_ptr<PolynomialBackend> &backend_singleton() {
    static std::unique_ptr<PolynomialBackend> instance;
    return instance;
}

std::mutex &backend_mutex() {
    static std::mutex mtx;
    return mtx;
}

} // namespace

PolynomialBackend &default_polynomial_backend() {
    auto &instance = backend_singleton();
    if (!instance) {
        std::lock_guard<std::mutex> lock(backend_mutex());
        if (!instance) {
            // Default modulus placeholder; actual modulus should be set by field initialization.
            instance = create_ntl_backend(kDefaultFieldModulus);
        }
    }
    return *instance;
}

void set_polynomial_backend(std::unique_ptr<PolynomialBackend> backend) {
    std::lock_guard<std::mutex> lock(backend_mutex());
    backend_singleton() = std::move(backend);
}

std::unique_ptr<PolynomialBackend> create_ntl_backend(std::uint64_t modulus) {
    return std::make_unique<NTLPolynomialBackend>(modulus);
}

#if SHAIR_TPSI_HAVE_FLINT
std::unique_ptr<PolynomialBackend> create_flint_backend(std::uint64_t) {
    throw std::runtime_error("FLINT backend not implemented yet");
}
#endif

} // namespace tpsi
