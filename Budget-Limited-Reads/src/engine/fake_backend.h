#pragma once

#include <cstdint>
#include <vector>

#include "engine/io_backend.h"

namespace blr {

// Simulated backend for unit tests on any platform. Time is simulated: the test
// calls advance_to(), and poll() returns every read whose completion time has
// been reached, ordered by (completion time, replica id).
//
// Buffers are written only at poll() time (like a real device finishing a
// read), filled with the offset-derived pattern from mirror/pattern.h.
class FakeBackend : public IoBackend {
 public:
  bool submit(const ReadSubmission& req) override;
  size_t poll(std::vector<ReadCompletion>& out) override;
  size_t outstanding(Drive d) const override;

  // --- simulated clock ---
  void advance_to(uint64_t ns) { if (ns > now_ns_) now_ns_ = ns; }
  uint64_t now_ns() const { return now_ns_; }

  // --- scripting ---
  void set_latency(Drive d, uint64_t ns) { latency_ns_[static_cast<int>(d)] = ns; }
  // Cap on total outstanding reads across both drives; 0 = unlimited.
  void set_max_outstanding(size_t total) { max_outstanding_ = total; }
  // Reject every submit (simulates a backend that never accepts work).
  void set_reject_all(bool reject) { reject_all_ = reject; }
  // The next `count` reads submitted to `d` complete with `result` (e.g. -5 for EIO).
  void fail_next(Drive d, unsigned count, int64_t result = -5) {
    fail_count_[static_cast<int>(d)] = count;
    fail_result_ = result;
  }
  // The next `count` reads submitted to `d` complete full-size with one wrong byte.
  void corrupt_next(Drive d, unsigned count) { corrupt_count_[static_cast<int>(d)] = count; }

  size_t outstanding_total() const { return outstanding_[0] + outstanding_[1]; }
  uint64_t submitted_total() const { return submitted_total_; }

 private:
  struct Op {
    ReadSubmission req;
    uint64_t due_ns;
    bool fail;
    bool corrupt;
  };

  std::vector<Op> ops_;
  size_t outstanding_[2] = {0, 0};
  uint64_t latency_ns_[2] = {100000, 100000};
  unsigned fail_count_[2] = {0, 0};
  unsigned corrupt_count_[2] = {0, 0};
  int64_t fail_result_ = -5;
  size_t max_outstanding_ = 0;
  bool reject_all_ = false;
  uint64_t now_ns_ = 0;
  uint64_t submitted_total_ = 0;
};

}  // namespace blr
