#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "accounting/counters.h"
#include "engine/io_backend.h"
#include "mirror/request.h"
#include "policy/policy.h"

namespace blr {

// Maps each logical offset to the same bytes in both files, drives the policy,
// returns the first successful verified result exactly once, and drains losers.
//
// Single-threaded: submit(), run_once() and drain() must be called from one
// thread. The result callback runs inside run_once()/drain(), may call
// submit(), but must not call run_once() or drain().
//
// This step implements the policy-free core (primary read, verify, answer once,
// drain, free buffers). Hedge issuing is not wired in yet: the reader never
// calls Policy::try_hedge. Completion handling already supports up to one
// duplicate per logical read so the hedge path can be added without changes here.
class MirrorReader {
 public:
  using ResultCallback = std::function<void(const LogicalRead&, bool ok)>;
  using Clock = std::function<uint64_t()>;  // monotonic nanoseconds

  MirrorReader(IoBackend& backend, Policy& policy, Counters& counters);

  // Submit a logical read. Assigns logical/replica IDs and issues the primary
  // replica read. If the backend rejects it (e.g. outstanding cap) the read is
  // queued and retried from run_once(); it is never dropped.
  void submit(uint64_t offset, size_t length, uint64_t now_ns);

  // Reap completions, verify, deliver each logical result exactly once, free
  // buffers of fully drained reads, and retry queued submits.
  void run_once(uint64_t now_ns);

  // Block until every replica operation has completed. Uses the clock set by
  // set_clock() (default: steady_clock). If the backend can never accept the
  // queued reads (nothing in flight and still rejecting), those reads are
  // failed rather than looping forever.
  void drain();

  void set_result_callback(ResultCallback cb) { on_result_ = std::move(cb); }
  void set_clock(Clock clock) { clock_ = std::move(clock); }

  // Logical reads not yet fully drained.
  size_t pending() const { return reads_.size(); }

 private:
  struct Entry {
    LogicalRead read;
    uint64_t answer_ns = 0;  // when the answer was returned (for loser cleanup time)
  };

  bool issue_primary(Entry& e, uint64_t now_ns);
  void retry_waiting(uint64_t now_ns);
  void fail_waiting(uint64_t now_ns);
  void handle_completion(const ReadCompletion& c, uint64_t now_ns);
  void answer(Entry& e, bool ok, bool from_duplicate, uint64_t now_ns);
  static bool all_issued_done(const LogicalRead& r);

  IoBackend& backend_;
  Policy& policy_;
  Counters& counters_;
  ResultCallback on_result_;
  Clock clock_;

  LogicalId next_logical_ = 1;
  ReplicaId next_replica_ = 1;  // 0 means "not submitted"

  // Entries are heap-allocated so each LogicalRead (and its replica buffers)
  // never moves while a read may still write into it.
  std::unordered_map<LogicalId, std::unique_ptr<Entry>> reads_;
  std::unordered_map<ReplicaId, LogicalId> replica_to_logical_;
  std::deque<LogicalId> waiting_;  // primary not yet accepted by the backend
};

}  // namespace blr
