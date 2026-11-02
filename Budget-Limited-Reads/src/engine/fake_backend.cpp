#include "engine/fake_backend.h"

#include <algorithm>
#include <cstdint>

#include "mirror/pattern.h"

namespace blr {

bool FakeBackend::submit(const ReadSubmission& req) {
  if (reject_all_) return false;
  if (max_outstanding_ != 0 && outstanding_total() >= max_outstanding_) return false;

  const int d = static_cast<int>(req.drive);
  Op op{req, now_ns_ + latency_ns_[d], false, false};
  if (fail_count_[d] > 0) {
    --fail_count_[d];
    op.fail = true;
  } else if (corrupt_count_[d] > 0) {
    --corrupt_count_[d];
    op.corrupt = true;
  }
  ops_.push_back(op);
  ++outstanding_[d];
  ++submitted_total_;
  return true;
}

size_t FakeBackend::poll(std::vector<ReadCompletion>& out) {
  // Collect finished ops in deterministic (due time, id) order.
  std::vector<Op> done;
  std::vector<Op> still;
  for (const Op& op : ops_) {
    (op.due_ns <= now_ns_ ? done : still).push_back(op);
  }
  std::sort(done.begin(), done.end(), [](const Op& a, const Op& b) {
    return a.due_ns != b.due_ns ? a.due_ns < b.due_ns : a.req.id < b.req.id;
  });
  ops_.swap(still);

  for (const Op& op : done) {
    int64_t result;
    if (op.fail) {
      result = fail_result_;
    } else {
      auto* buf = static_cast<uint8_t*>(op.req.buffer);
      if (op.req.length > 0) {
        fill_pattern(op.req.offset, buf, op.req.length);
        if (op.corrupt) buf[0] = static_cast<uint8_t>(buf[0] ^ 0xFF);
      }
      result = static_cast<int64_t>(op.req.length);
    }
    --outstanding_[static_cast<int>(op.req.drive)];
    out.push_back(ReadCompletion{op.req.id, op.req.drive, result});
  }
  return done.size();
}

size_t FakeBackend::outstanding(Drive d) const {
  return outstanding_[static_cast<int>(d)];
}

}  // namespace blr
