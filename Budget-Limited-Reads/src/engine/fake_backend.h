#pragma once

#include "engine/io_backend.h"

namespace blr {

// Simulated backend for unit tests on any platform.
// TODO: per-drive configurable latency, injectable failures, deterministic ordering.
class FakeBackend : public IoBackend {
 public:
  bool submit(const ReadSubmission& req) override;
  size_t poll(std::vector<ReadCompletion>& out) override;
  size_t outstanding(Drive d) const override;

 private:
  size_t outstanding_[2] = {0, 0};
};

}  // namespace blr
