#include "engine/fake_backend.h"
#include "mirror/verify.h"
#include <algorithm>
namespace blr {
bool FakeBackend::submit(const ReadSubmission& req) {
  if (outstanding_[0] + outstanding_[1] >= cap_) return false;
  auto d = static_cast<int>(req.drive);
  Outcome o{latency_[d], static_cast<int64_t>(req.length), false};
  if (!outcomes_[d].empty()) { o = outcomes_[d].front(); outcomes_[d].pop_front(); }
  pending_.push_back({req, now_ + o.latency_ns, o.result, o.corrupt});
  ++outstanding_[d];
  return true;
}
size_t FakeBackend::poll(std::vector<ReadCompletion>& out) {
  size_t n = 0;
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (it->due > now_) { ++it; continue; }
    if (it->result > 0) {
      auto* data = static_cast<uint8_t*>(it->req.buffer);
      fill_expected(it->req.offset, std::min(it->req.length, static_cast<size_t>(it->result)), data);
      if (it->corrupt && it->req.length) data[0] ^= 1;
    }
    out.push_back({it->req.id, it->req.drive, it->result});
    --outstanding_[static_cast<int>(it->req.drive)];
    it = pending_.erase(it); ++n;
  }
  return n;
}
}
