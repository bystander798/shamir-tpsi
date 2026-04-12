#pragma once

#include "tpsi/ring.hpp"

#include <cstdint>
#include <vector>

namespace tpsi {

std::vector<std::uint8_t> serialize_uint64_vector(const std::vector<std::uint64_t> &values);
std::vector<std::uint64_t> deserialize_uint64_vector(const std::vector<std::uint8_t> &bytes);

void append_uint64_vector(std::vector<std::uint8_t> &buffer,
                          const std::vector<std::uint64_t> &values);
std::vector<std::uint64_t> read_uint64_vector(const std::vector<std::uint8_t> &buffer,
                                              std::size_t &offset);

void append_ring_element(std::vector<std::uint8_t> &buffer, const RingElement &element);
RingElement read_ring_element(const RingConfig &cfg,
                              const std::vector<std::uint8_t> &buffer,
                              std::size_t &offset);

void append_bytes(std::vector<std::uint8_t> &buffer, const std::vector<std::uint8_t> &data);
std::vector<std::uint8_t> read_bytes(const std::vector<std::uint8_t> &buffer,
                                     std::size_t &offset);

void append_uint32(std::vector<std::uint8_t> &buffer, std::uint32_t value);
std::uint32_t read_uint32(const std::vector<std::uint8_t> &buffer, std::size_t &offset);

void append_double(std::vector<std::uint8_t> &buffer, double value);
double read_double(const std::vector<std::uint8_t> &buffer, std::size_t &offset);

} // namespace tpsi

