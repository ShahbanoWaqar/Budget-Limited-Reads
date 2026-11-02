#include "mirror/verify.h"

#include <cstring>

#include "mirror/pattern.h"

namespace blr {

bool verify_read(uint64_t offset, size_t expected_len, int64_t result,
                 const uint8_t* data) {
  // Size: negative (-errno) or short/long reads are failures.
  if (result < 0 || static_cast<uint64_t>(result) != expected_len) return false;
  if (expected_len == 0) return true;
  if (data == nullptr) return false;

  size_t i = 0;
  uint64_t pos = offset;

  // Unaligned head: byte-by-byte up to the next 8-byte boundary.
  while (i < expected_len && (pos & 7) != 0) {
    if (data[i] != pattern_byte(pos)) return false;
    ++i;
    ++pos;
  }

  // Aligned body: one pattern word at a time.
  uint8_t expected[8];
  while (expected_len - i >= 8) {
    pattern_word_bytes(pos >> 3, expected);
    if (std::memcmp(data + i, expected, 8) != 0) return false;
    i += 8;
    pos += 8;
  }

  // Tail: remaining bytes (< 8).
  while (i < expected_len) {
    if (data[i] != pattern_byte(pos)) return false;
    ++i;
    ++pos;
  }
  return true;
}

}  // namespace blr
