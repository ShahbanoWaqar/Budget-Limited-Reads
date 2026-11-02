#pragma once

#include <cstddef>
#include <cstdint>

namespace blr {

// Expected-content scheme for test files: the byte at file position `pos` is a
// pure function of `pos`. Every 8-byte word i holds splitmix64(i ^ seed) in
// little-endian byte order (independent of host endianness). Two files built
// from this pattern are identical by construction, and any (offset, length)
// range can be verified without a stored checksum table.
//
// Keep this the single definition: verify.cpp, the fake backend and the
// test-file generator must all use it.

constexpr uint64_t kPatternSeed = 0xA5A5C3C35A5A3C3CULL;

inline uint64_t splitmix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}

inline uint64_t pattern_word(uint64_t word_index) {
  return splitmix64(word_index ^ kPatternSeed);
}

// The 8 bytes of word `word_index`, little-endian.
inline void pattern_word_bytes(uint64_t word_index, uint8_t out[8]) {
  uint64_t w = pattern_word(word_index);
  for (int i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>(w >> (8 * i));
}

inline uint8_t pattern_byte(uint64_t pos) {
  return static_cast<uint8_t>(pattern_word(pos >> 3) >> (8 * (pos & 7)));
}

// Fills buf[0..len) with the pattern bytes for file positions [pos, pos+len).
inline void fill_pattern(uint64_t pos, uint8_t* buf, size_t len) {
  uint64_t cached_index = ~0ULL;
  uint64_t cached_word = 0;
  for (size_t i = 0; i < len; ++i) {
    uint64_t p = pos + i;
    uint64_t idx = p >> 3;
    if (idx != cached_index) {
      cached_word = pattern_word(idx);
      cached_index = idx;
    }
    buf[i] = static_cast<uint8_t>(cached_word >> (8 * (p & 7)));
  }
}

}  // namespace blr
