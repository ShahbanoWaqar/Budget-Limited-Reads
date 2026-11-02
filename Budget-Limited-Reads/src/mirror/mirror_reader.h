#pragma once

#include <functional>
#include <memory>

#include "accounting/counters.h"
#include "engine/io_backend.h"
#include "mirror/request.h"
#include "policy/policy.h"

namespace blr {

// Maps each logical offset to the same bytes in both files, drives the policy,
// returns the first successful verified result exactly once, and drains losers.
class MirrorReader {
 public:
  using ResultCallback = std::function<void(const LogicalRead&, bool ok)>;

  MirrorReader(IoBackend& backend, Policy& policy, Counters& counters);

  // TODO: submit a logical read; assign logical and replica IDs.
  void submit(uint64_t offset, size_t length, uint64_t now_ns);

  // TODO: reap completions, apply hedge timers, deliver results, release permits.
  void run_once(uint64_t now_ns);

  // TODO: block until every replica operation has completed.
  void drain();

  void set_result_callback(ResultCallback cb) { on_result_ = std::move(cb); }

 private:
  IoBackend& backend_;
  Policy& policy_;
  Counters& counters_;
  ResultCallback on_result_;
};

}  // namespace blr
