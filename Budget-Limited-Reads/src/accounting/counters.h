#pragma once

#include <cstdint>

namespace blr {

// Logical results are counted separately from every replica read, including
// losing reads that finish after the answer was returned.
struct Counters {
  uint64_t logical_submitted = 0;
  uint64_t logical_succeeded = 0;
  uint64_t logical_failed = 0;

  uint64_t replica_issued = 0;
  uint64_t replica_completed = 0;
  uint64_t replica_failed = 0;
  uint64_t replica_bytes = 0;

  uint64_t duplicates_issued = 0;
  uint64_t duplicates_won = 0;
  uint64_t duplicates_denied = 0;  // eligible but no permit / threshold exceeded

  uint64_t loser_cleanup_ns_total = 0;  // answer time -> loser completion time

  double duplicate_fraction() const;
  double replica_reads_per_logical() const;
};

}  // namespace blr
