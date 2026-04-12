#include "tpsi/serialization.hpp"

#include <cstring>
#include <stdexcept>

namespace tpsi {
namespace {

template <typename T>
void append_raw(std::vector<std::uint8_t> &buffer, const T &value) {
    const std::uint8_t *ptr = reinterpret_cast<const std::uint8_t *>(&value);
    buffer.insert(buffer.end(), ptr, ptr + sizeof(T));
}

template <typename T>
T read_raw(const std::vector<std::uint8_t> &buffer, std::size_t &offset) {
    if (offset + sizeof(T) > buffer.size()) {
        throw std::runtime_error("deserialize: not enough bytes");
    }
    T value{};
    std::memcpy(&value, buffer.data() + offset, sizeof(T));
    offset += sizeof(T);
    return value;
}

} // namespace

std::vector<std::uint8_t> serialize_uint64_vector(const std::vector<std::uint64_t> &values) {
    std::vector<std::uint8_t> buffer;
    buffer.reserve(sizeof(std::uint32_t) + values.size() * sizeof(std::uint64_t));
    append_uint32(buffer, static_cast<std::uint32_t>(values.size()));
    append_uint64_vector(buffer, values);
    return buffer;
}

std::vector<std::uint64_t> deserialize_uint64_vector(const std::vector<std::uint8_t> &bytes) {
    std::size_t offset = 0;
    const auto size = read_uint32(bytes, offset);
    std::vector<std::uint64_t> values;
    values.reserve(size);
    for (std::uint32_t i = 0; i < size; ++i) {
        values.push_back(read_raw<std::uint64_t>(bytes, offset));
    }
    return values;
}

void append_uint64_vector(std::vector<std::uint8_t> &buffer,
                          const std::vector<std::uint64_t> &values) {
    for (auto v : values) {
        append_raw(buffer, v);
    }
}

std::vector<std::uint64_t> read_uint64_vector(const std::vector<std::uint8_t> &buffer,
                                              std::size_t &offset) {
    const auto size = read_uint32(buffer, offset);
    std::vector<std::uint64_t> values;
    values.reserve(size);
    for (std::uint32_t i = 0; i < size; ++i) {
        values.push_back(read_raw<std::uint64_t>(buffer, offset));
    }
    return values;
}

void append_ring_element(std::vector<std::uint8_t> &buffer, const RingElement &element) {
    const auto &coeffs = element.coefficients();
    append_uint32(buffer, static_cast<std::uint32_t>(coeffs.size()));
    append_uint64_vector(buffer, coeffs);
}

RingElement read_ring_element(const RingConfig &cfg,
                              const std::vector<std::uint8_t> &buffer,
                              std::size_t &offset) {
    const std::uint32_t degree = read_uint32(buffer, offset);
    std::vector<std::uint64_t> coeffs;
    coeffs.reserve(degree);
    for (std::uint32_t i = 0; i < degree; ++i) {
        coeffs.push_back(read_raw<std::uint64_t>(buffer, offset));
    }
    return RingElement(cfg, coeffs);
}

void append_bytes(std::vector<std::uint8_t> &buffer, const std::vector<std::uint8_t> &data) {
    append_uint32(buffer, static_cast<std::uint32_t>(data.size()));
    buffer.insert(buffer.end(), data.begin(), data.end());
}

std::vector<std::uint8_t> read_bytes(const std::vector<std::uint8_t> &buffer,
                                     std::size_t &offset) {
    const auto size = read_uint32(buffer, offset);
    if (offset + size > buffer.size()) {
        throw std::runtime_error("deserialize bytes: not enough data");
    }
    std::vector<std::uint8_t> out(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                                  buffer.begin() + static_cast<std::ptrdiff_t>(offset + size));
    offset += size;
    return out;
}

void append_uint32(std::vector<std::uint8_t> &buffer, std::uint32_t value) {
    append_raw(buffer, value);
}

std::uint32_t read_uint32(const std::vector<std::uint8_t> &buffer, std::size_t &offset) {
    return read_raw<std::uint32_t>(buffer, offset);
}

void append_double(std::vector<std::uint8_t> &buffer, double value) {
    append_raw(buffer, value);
}

double read_double(const std::vector<std::uint8_t> &buffer, std::size_t &offset) {
    return read_raw<double>(buffer, offset);
}

} // namespace tpsi

