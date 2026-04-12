#include "tpsi/pcg.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

namespace tpsi {

namespace {

[[noreturn]] void openssl_fail(const std::string &where) {
    throw std::runtime_error("OpenSSL failure: " + where);
}

NTL::ZZ bytes_to_ZZ_internal(const unsigned char *buffer, std::size_t length) {
    NTL::ZZ value(0);
    for (std::size_t i = 0; i < length; ++i) {
        value = (value << 8) + NTL::ZZ(static_cast<unsigned long>(buffer[i]));
    }
    return value;
}

std::size_t prime_bytes_len(const NTL::ZZ &prime) {
    const long bits = NTL::NumBits(prime);
    return static_cast<std::size_t>((bits + 7) / 8);
}

void aes_ctr_expand(const std::vector<unsigned char> &key16,
                    const unsigned char iv16[16],
                    std::vector<unsigned char> &out,
                    std::size_t need_bytes) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) openssl_fail("EVP_CIPHER_CTX_new");
    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, key16.data(), iv16)) {
        EVP_CIPHER_CTX_free(ctx);
        openssl_fail("EVP_EncryptInit_ex");
    }
    std::vector<unsigned char> in(need_bytes, 0);
    out.resize(need_bytes + 16);
    int outlen = 0;
    if (1 != EVP_EncryptUpdate(ctx, out.data(), &outlen, in.data(), static_cast<int>(in.size()))) {
        EVP_CIPHER_CTX_free(ctx);
        openssl_fail("EVP_EncryptUpdate");
    }
    int tmplen = 0;
    if (1 != EVP_EncryptFinal_ex(ctx, out.data() + outlen, &tmplen)) {
        EVP_CIPHER_CTX_free(ctx);
        openssl_fail("EVP_EncryptFinal_ex");
    }
    EVP_CIPHER_CTX_free(ctx);
    out.resize(static_cast<std::size_t>(outlen + tmplen));
}

static std::vector<unsigned char> hkdf_expand_16(const std::vector<unsigned char> &ikm,
                                                 const std::string &info) {
    std::vector<unsigned char> okm(16);
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!pctx) openssl_fail("EVP_PKEY_CTX_new_id");
    if (EVP_PKEY_derive_init(pctx) <= 0) openssl_fail("EVP_PKEY_derive_init");
    if (EVP_PKEY_CTX_set_hkdf_md(pctx, EVP_sha256()) <= 0) openssl_fail("set_hkdf_md");

    // salt：传 nullptr, 0（避免把 "" 当作 const char* 传入）
    if (EVP_PKEY_CTX_set1_hkdf_salt(pctx, nullptr, 0) <= 0)
        openssl_fail("set1_hkdf_salt");

    if (EVP_PKEY_CTX_set1_hkdf_key(pctx, ikm.data(), static_cast<int>(ikm.size())) <= 0)
        openssl_fail("set1_hkdf_key");

    // info 需要 const unsigned char*
    const unsigned char *info_ptr =
        reinterpret_cast<const unsigned char *>(info.data());
    if (EVP_PKEY_CTX_add1_hkdf_info(pctx, info_ptr, static_cast<int>(info.size())) <= 0)
        openssl_fail("add1_hkdf_info");

    size_t outlen = okm.size();
    if (EVP_PKEY_derive(pctx, okm.data(), &outlen) <= 0) openssl_fail("HKDF_derive");
    EVP_PKEY_CTX_free(pctx);
    return okm;
}

NTL::ZZ_p byte_chunk_to_ZZp(const unsigned char *ptr, std::size_t len, const NTL::ZZ &prime) {
    NTL::ZZ value = bytes_to_ZZ_internal(ptr, len) % prime;
    return NTL::conv<NTL::ZZ_p>(value);
}

// 在 F_p 上，把长度 = degree 的字节块转成 ZZ_pX（系数逐一 mod p）
NTL::ZZ_pX coefficients_to_poly_modp(const std::vector<unsigned char> &bytes,
                                     std::size_t degree,
                                     const NTL::ZZ &prime) {
    const std::size_t coeff_bytes = static_cast<std::size_t>((NTL::NumBits(prime) + 7) / 8);
    NTL::ZZ_pX poly;
    poly.SetMaxLength(static_cast<long>(degree));
    for (std::size_t i = 0; i < degree; ++i) {
        const unsigned char *ptr = bytes.data() + i * coeff_bytes;
        NTL::SetCoeff(poly, static_cast<long>(i), byte_chunk_to_ZZp(ptr, coeff_bytes, prime));
    }
    return poly;
}

} // namespace

void pcg_init_field(PCGParams &params) {
    if (params.prime == 0) throw std::runtime_error("prime not set");
    NTL::ZZ_p::init(params.prime);
    params.prime_bytes = prime_bytes_len(params.prime);
}

std::vector<unsigned char> csprng_bytes(std::size_t n) {
    std::vector<unsigned char> out(n);
    if (1 != RAND_bytes(out.data(), static_cast<int>(n))) {
        openssl_fail("RAND_bytes");
    }
    return out;
}

// 使用 HKDF 对 master_seed 和 index 做域分离得到 16 字节密钥
std::vector<unsigned char> derive_instance_seed(const std::vector<unsigned char> &master_seed,
                                                std::uint64_t index) {
    if (master_seed.size() != PCGSeedBytes) {
        throw std::runtime_error("seed size mismatch");
    }
    std::string info = "tpsi-pcg:i=" + std::to_string(index);
    return hkdf_expand_16(master_seed, info);
}

// —— “真正” ring-LPN 样本的 expand_poly_from_seed ——
// 约定：当 index 的高字节前缀是 0xB1（你代码里生成 b0 的前缀），
// 本函数返回一个 ring-LPN 样本的“右项” b = a*s + e。
// 其它前缀（如 0xA1、0xC1）仍按均匀取值生成环元素（用于 a0/x0）
NTL::ZZ_pX expand_poly_from_seed(const std::vector<unsigned char> &seed16,
                                 std::uint64_t index,
                                 const PCGParams &params) {
    // ====== 公共：一些工具 ======
    auto make_iv = [&](std::uint64_t tag) {
        unsigned char iv[16];
        std::memset(iv, 0, sizeof(iv));
        for (int i = 0; i < 8; ++i) iv[15 - i] = static_cast<unsigned char>((tag >> (8 * i)) & 0xFF);
        return std::array<unsigned char,16>{iv[0],iv[1],iv[2],iv[3],iv[4],iv[5],iv[6],iv[7],
                                            iv[8],iv[9],iv[10],iv[11],iv[12],iv[13],iv[14],iv[15]};
    };
    auto prg_poly_uniform = [&](const std::vector<unsigned char> &k16,
                                std::uint64_t iv_tag)->NTL::ZZ_pX {
        const std::size_t coeff_bytes = params.prime_bytes;
        const std::size_t total_bytes = static_cast<std::size_t>(params.n) * coeff_bytes;
        std::vector<unsigned char> stream;
        const auto iv = make_iv(iv_tag);
        aes_ctr_expand(k16, iv.data(), stream, total_bytes);
        return coefficients_to_poly_modp(stream, static_cast<std::size_t>(params.n), params.prime);
    };

    // 判定是否用于 b0（按你现有调用的域分离常量）
    const bool for_b0 = ((index & 0xFF00000000000000ull) == 0xB100000000000000ull);

    if (!for_b0) {
        // a0 / x0：均匀环元素
        // 仍使用 HKDF(info="tpsi-pcg:i=<index>") 派生独立子密钥
        const std::string info = "tpsi-pcg:i=" + std::to_string(index);
        const std::vector<unsigned char> k = hkdf_expand_16(seed16, info);
        return prg_poly_uniform(k, 0xD0D0D0D0D0D0D0D0ull ^ index);
    }

    // ====== b0：返回 ring-LPN 样本右项 b = a*s + e ======
    // 1) s：小汉明重秘密（HW_t），由 seed16+域分离派生
    const std::string info_s = "tpsi-lpn:s|i=" + std::to_string(index);
    const std::vector<unsigned char> k_s = hkdf_expand_16(seed16, info_s);
    // 用已有的 sample_sparse_noise 生成“固定汉明重 t”的 s
    NTL::ZZ_pX s = sample_bernoulli_noise(k_s, 0x5300000000000000ull ^ index, params); // 'S'

    // 2) a：均匀环元素（同样来自 seed16 的不同域分离）
    const std::string info_a = "tpsi-lpn:a|i=" + std::to_string(index);
    const std::vector<unsigned char> k_a = hkdf_expand_16(seed16, info_a);
    NTL::ZZ_pX a = prg_poly_uniform(k_a, 0x4100000000000000ull ^ index); // 'A'

    // 3) e：误差（HW_t），可直接复用你实现的稀疏/伯努利噪声生成
    const std::string info_e = "tpsi-lpn:e|i=" + std::to_string(index);
    const std::vector<unsigned char> k_e = hkdf_expand_16(seed16, info_e);
    NTL::ZZ_pX e = sample_bernoulli_noise(k_e, 0x4500000000000000ull ^ index, params); // 'E'

    // 4) 计算 b = a*s + e (mod modpoly)
    NTL::ZZ_pX b = poly_mod(a * s + e, params.modpoly);
    return b;
}

// 稀疏噪声：严格产生“固定汉明重（t）”的一元向量（每个选中位置系数=1）
NTL::ZZ_pX sample_sparse_noise(const std::vector<unsigned char> &seed16,
                               std::uint64_t index,
                               const PCGParams &params) {
    const long n = params.n;
    const long t = std::min(params.t, n);
    if (t <= 0) {
        return NTL::ZZ_pX();
    }

    // 通过 HKDF 派生选择位置的 key，再用 AES-CTR 做流，生成下标
    const std::string base_info = "tpsi-noise:i=" + std::to_string(index);

    unsigned char iv[16];
    std::memset(iv, 0, sizeof(iv));
    for (int i = 0; i < 8; ++i) {
        iv[15 - i] = static_cast<unsigned char>((index >> (8 * i)) & 0xFF);
    }

    const std::size_t need_bytes = static_cast<std::size_t>(t) * 8;
    std::vector<unsigned char> buf;
    std::size_t buf_offset = 0;
    std::size_t chunk = 0;

    auto refill_buffer = [&]() {
        std::string info = base_info + "|chunk=" + std::to_string(chunk++);
        std::vector<unsigned char> key = hkdf_expand_16(seed16, info);
        std::vector<unsigned char> tmp;
        aes_ctr_expand(key, iv, tmp, need_bytes);
        buf.swap(tmp);
        buf_offset = 0;
    };

    refill_buffer();

    NTL::ZZ_pX noise;
    noise.SetMaxLength(n);
    std::unordered_set<long> pos;
    pos.reserve(static_cast<std::size_t>(t) * 2);

    // 抽取 64-bit 片段取模 n 做下标，直到达到 t 个独特位置
    while ((long)pos.size() < t) {
        if (buf_offset + 8 > buf.size()) {
            refill_buffer();
            continue;
        }
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v = (v << 8) | buf[buf_offset + i];
        }
        buf_offset += 8;
        long p = static_cast<long>(v % static_cast<std::uint64_t>(n));
        pos.insert(p);
    }

    const NTL::ZZ_p one = NTL::to_ZZ_p(1);
    for (long p : pos) {
        NTL::SetCoeff(noise, p, one);
    }
    return noise;
}

// 伯努利噪声：每个系数独立为 1 的概率 p = min(1, t/n)
// 使用 HKDF 派生 16B 子密钥 + AES-CTR 产生均匀 64-bit 流，阈值比较生成 0/1
NTL::ZZ_pX sample_bernoulli_noise(const std::vector<unsigned char> &seed16,
                                  std::uint64_t index,
                                  const PCGParams &params) {
    const long n = params.n;
    const long t = std::max<long>(0, std::min<long>(params.t, n));
    if (n <= 0 || t <= 0) return NTL::ZZ_pX(); // 零噪声

    // p = t/n，用 64-bit 阈值 T = floor(p * 2^64)
    // 计算 T 时用 128-bit 中间变量避免溢出
    const unsigned __int128 T128 = (static_cast<unsigned __int128>(t) << 64) / static_cast<unsigned long long>(n);
    const std::uint64_t T = static_cast<std::uint64_t>(T128);

    // 域分离：不同 index + 标签，保证可复现且互不干扰
    const std::string info = "tpsi-bern:i=" + std::to_string(index);
    const std::vector<unsigned char> k = hkdf_expand_16(seed16, info);

    unsigned char iv[16]; std::memset(iv, 0, sizeof(iv));
    for (int i = 0; i < 8; ++i) iv[15 - i] = static_cast<unsigned char>((index >> (8 * i)) & 0xFF);

    // 需要的 64-bit 随机数个数 = n；按系数生成，降低内存峰值
    const std::size_t BYTES_PER_COEFF = 8;
    const std::size_t NEED = static_cast<std::size_t>(n) * BYTES_PER_COEFF;

    // 用小块循环拉流，避免一次性大分配
    const std::size_t CHUNK = 4096;

    // 我们用一个“拉流器”把 AES-CTR 流填充到 ring buffer，再逐系数取 8 字节
    std::vector<unsigned char> stream; stream.reserve(std::min<std::size_t>(NEED, 1<<20));
    auto refill = [&](std::size_t want) {
        std::vector<unsigned char> tmp;
        aes_ctr_expand(k, iv, tmp, want);
        // 直接追加
        stream.insert(stream.end(), tmp.begin(), tmp.end());
    };

    NTL::ZZ_pX noise; noise.SetMaxLength(n);
    const NTL::ZZ_p ONE = NTL::to_ZZ_p(1);

    std::size_t off = 0;
    for (long i = 0; i < n; ++i) {
        if (off + BYTES_PER_COEFF > stream.size()) {
            // 每次尽量补足一个较大的块
            const std::size_t remain = NEED - off;
            refill(std::min<std::size_t>(remain, std::max<std::size_t>(CHUNK, BYTES_PER_COEFF)));
        }
        // 取 8 字节成为 64-bit 无符号数
        std::uint64_t u = 0;
        for (int j = 0; j < 8; ++j) u = (u << 8) | stream[off + j];
        off += BYTES_PER_COEFF;

        // 伯努利判定：u < T 则置 1
        if (u < T) {
            NTL::SetCoeff(noise, i, ONE);
        }
    }
    return noise;
}

// 取模多项式
NTL::ZZ_pX poly_mod(const NTL::ZZ_pX &poly, const NTL::ZZ_pX &modulus) {
    if (NTL::deg(modulus) < 0) return poly;
    return poly % modulus;
}

// —— 关键函数：两方相关性（rrOLE-like）：Sender 出(a,b)，Receiver 出(x,c=ax+b) ——
// 这里我们保持你已有接口，但将分布/结构严格对齐为：
//  1) 在环 F_p[X]/<modpoly> 上采样 a,b,x；
//  2) a,x 叠加稀疏/伯努利噪声（LPN风格），
//  3) c = (a*x + b) mod <modpoly>，并可选验证一致性
std::uint64_t two_party_pcg_simulate(const std::vector<unsigned char> &seed_sender,
                                     const std::vector<unsigned char> &seed_receiver,
                                     const PCGParams &params,
                                     long batch_count,
                                     std::vector<RRSender> &sender_out,
                                     std::vector<RRReceiver> &receiver_out,
                                     bool verify_outputs,
                                     double *elapsed_seconds,
                                     double *sender_elapsed_seconds,
                                     double *receiver_elapsed_seconds) {
    using clock_type = std::chrono::high_resolution_clock;
    const auto start = clock_type::now();

    // Communication bytes accounted as the size of distributed seeds.
    std::uint64_t communication_bytes =
        static_cast<std::uint64_t>(seed_sender.size() + seed_receiver.size());

    sender_out.assign(static_cast<std::size_t>(batch_count), {});
    receiver_out.assign(static_cast<std::size_t>(batch_count), {});

    double sender_seconds_acc = 0.0;
    double receiver_seconds_acc = 0.0;

    for (long i = 0; i < batch_count; ++i) {
        const std::uint64_t idx = static_cast<std::uint64_t>(i);

        const auto sender_phase_start = clock_type::now();
        const auto s_key = derive_instance_seed(seed_sender, idx);

        NTL::ZZ_pX a0 = expand_poly_from_seed(s_key, 0xA100000000000000ull ^ idx, params);
        NTL::ZZ_pX b0 = expand_poly_from_seed(s_key, 0xB100000000000000ull ^ idx, params);
        NTL::ZZ_pX ea = sample_bernoulli_noise(s_key, 0xA200000000000000ull ^ idx, params);

        NTL::ZZ_pX a = poly_mod(a0 + ea, params.modpoly);
        NTL::ZZ_pX b = poly_mod(b0, params.modpoly);
        const auto sender_phase_end = clock_type::now();
        sender_seconds_acc +=
            std::chrono::duration_cast<std::chrono::duration<double>>(sender_phase_end -
                                                                      sender_phase_start)
                .count();

        const auto receiver_phase_start = clock_type::now();
        const auto r_key = derive_instance_seed(seed_receiver, idx);

        NTL::ZZ_pX x0 = expand_poly_from_seed(r_key, 0xC100000000000000ull ^ idx, params);
        NTL::ZZ_pX ex = sample_bernoulli_noise(r_key, 0xC200000000000000ull ^ idx, params);

        NTL::ZZ_pX x = poly_mod(x0 + ex, params.modpoly);
        NTL::ZZ_pX c = poly_mod(a * x + b, params.modpoly);
        const auto receiver_phase_end = clock_type::now();
        receiver_seconds_acc +=
            std::chrono::duration_cast<std::chrono::duration<double>>(receiver_phase_end -
                                                                      receiver_phase_start)
                .count();

        sender_out[static_cast<std::size_t>(i)] = {a, b};
        receiver_out[static_cast<std::size_t>(i)] = {x, c};
    }

    if (verify_outputs) {
        for (long i = 0; i < batch_count; ++i) {
            const NTL::ZZ_pX recomputed =
                poly_mod(sender_out[static_cast<std::size_t>(i)].a *
                             receiver_out[static_cast<std::size_t>(i)].x,
                         params.modpoly);
            const NTL::ZZ_pX expected =
                poly_mod(recomputed + sender_out[static_cast<std::size_t>(i)].b, params.modpoly);
            if (expected != receiver_out[static_cast<std::size_t>(i)].c) {
                throw std::runtime_error("[PCG verify] instance mismatch");
            }
        }
    }

    const auto end = clock_type::now();
    if (elapsed_seconds) {
        *elapsed_seconds =
            std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
    }
    if (sender_elapsed_seconds) {
        *sender_elapsed_seconds = sender_seconds_acc;
    }
    if (receiver_elapsed_seconds) {
        *receiver_elapsed_seconds = receiver_seconds_acc;
    }
    return communication_bytes;
}

NTL::ZZ bytes_to_ZZ(const unsigned char *data, std::size_t len) {
    return bytes_to_ZZ_internal(data, len);
}

} // namespace tpsi
