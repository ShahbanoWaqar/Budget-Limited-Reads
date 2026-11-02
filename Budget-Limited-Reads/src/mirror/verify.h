#pragma once

#include <cstddef>
#include <cstdint>

namespace blr {

// A result is valid only if it has the requested size and the expected bytes.
// Expected content is the offset-derived pattern in mirror/pattern.h.
// A zero-length read with result 0 is valid (data may be null).
bool verify_read(uint64_t offset, size_t expected_len, int64_t result,
                 const uint8_t* data);

}  // namespace blr
