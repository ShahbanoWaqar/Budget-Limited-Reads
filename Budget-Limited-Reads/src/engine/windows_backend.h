#pragma once
#include <memory>
#include <string>
#include "engine/io_backend.h"

namespace blr {
// Native asynchronous Windows file reads. No WSL, liburing, or worker threads.
// Definitions are hidden so Windows.h never leaks into shared engine headers.
class WindowsBackend : public IoBackend {
 public:
  WindowsBackend(const std::string& path_a, const std::string& path_b, unsigned queue_depth);
  ~WindowsBackend() override;
  bool submit(const ReadSubmission& req) override;
  size_t poll(std::vector<ReadCompletion>& out) override;
  size_t outstanding(Drive d) const override;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
