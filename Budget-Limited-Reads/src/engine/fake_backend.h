#pragma once
#include <array>
#include <deque>
#include "engine/io_backend.h"
namespace blr {
// Virtual clock and per-submission outcomes make races reproducible.
class FakeBackend : public IoBackend {
 public:
  struct Outcome { uint64_t latency_ns; int64_t result = 4096; bool corrupt = false; };
  explicit FakeBackend(size_t cap = 128) : cap_(cap) {}
  void set_time(uint64_t now) { now_ = now; }
  void set_latency(Drive d, uint64_t ns) { latency_[static_cast<int>(d)] = ns; }
  void enqueue_outcome(Drive d, Outcome o) { outcomes_[static_cast<int>(d)].push_back(o); }
  bool submit(const ReadSubmission& req) override;
  size_t poll(std::vector<ReadCompletion>& out) override;
  size_t outstanding(Drive d) const override { return outstanding_[static_cast<int>(d)]; }
 private:
  struct Pending { ReadSubmission req; uint64_t due; int64_t result; bool corrupt; };
  uint64_t now_ = 0;
  size_t cap_;
  std::array<size_t, 2> outstanding_{};
  std::array<uint64_t, 2> latency_{100000, 100000};
  std::array<std::deque<Outcome>, 2> outcomes_;
  std::vector<Pending> pending_;
};
}
