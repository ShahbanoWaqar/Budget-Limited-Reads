#pragma once

#include <cstddef>
#include <cstdint>

namespace blr {

// A result is valid only if it has the requested size and the expected bytes.
// TODO: decide on the expected-content scheme (e.g. offset-derived pattern or checksum table).
bool verify_read(uint64_t offset, size_t expected_len, int64_t result,
                 const uint8_t* data);

}  // namespace blr
