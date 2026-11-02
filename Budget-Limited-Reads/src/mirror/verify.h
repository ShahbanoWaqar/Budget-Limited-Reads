#pragma once
#include <cstddef>
#include <cstdint>
namespace blr {
inline uint8_t expected_byte(uint64_t offset) {
  // SplitMix64-derived byte: reproducible at any offset without a manifest.
  uint64_t x = offset + 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return static_cast<uint8_t>(x ^ (x >> 31));
}
void fill_expected(uint64_t offset, size_t length, uint8_t* data);
bool verify_read(uint64_t offset, size_t length, int64_t result, const uint8_t* data);
}
