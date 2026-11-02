#pragma once

#include <cstdint>

#include "engine/io_backend.h"
#include "mirror/request.h"

namespace blr {

// One policy object per comparison mode. Everything else (engine, buffers,
// accounting, I/O caps) is shared so experiments isolate the policy.
class Policy {
 public:
  virtual ~Policy() = default;

  virtual const char* name() const = 0;

  // Initial replica: smaller outstanding count, round-robin ties
  // (or plain round-robin for the simple reference mode).
  virtual Drive choose_primary(const IoBackend& backend) = 0;

  // Called once the hedge delay has elapsed and the primary is unfinished.
  // Returns true if one duplicate may be issued. Budget-limited policies
  // acquire a permit here.
  virtual bool try_hedge(const LogicalRead& read, const IoBackend& backend,
                         uint64_t now_ns) = 0;

  virtual bool uses_permits() const { return false; }
  virtual uint64_t hedge_delay_ns() const { return UINT64_MAX; }

  // Called after both replica reads of a pair have completed and been cleaned up.
  virtual void on_pair_drained(const LogicalRead& read) = 0;
};

}  // namespace blr
