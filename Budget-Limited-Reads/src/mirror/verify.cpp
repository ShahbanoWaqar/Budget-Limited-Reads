#include "mirror/verify.h"

namespace blr {

bool verify_read(uint64_t, size_t expected_len, int64_t result, const uint8_t*) {
  if (result < 0 || static_cast<size_t>(result) != expected_len) return false;
  // TODO: compare bytes against the expected content.
  return true;
}

}  // namespace blr
