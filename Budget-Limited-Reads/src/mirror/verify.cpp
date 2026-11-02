#include "mirror/verify.h"
namespace blr {
void fill_expected(uint64_t offset, size_t length, uint8_t* data) {
  for (size_t i = 0; i < length; ++i) data[i] = expected_byte(offset + i);
}
bool verify_read(uint64_t offset, size_t length, int64_t result, const uint8_t* data) {
  if (result < 0 || static_cast<uint64_t>(result) != length || !data) return false;
  for (size_t i = 0; i < length; ++i) if (data[i] != expected_byte(offset + i)) return false;
  return true;
}
}
