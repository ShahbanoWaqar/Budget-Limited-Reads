#include "engine/fake_backend.h"

namespace blr {

bool FakeBackend::submit(const ReadSubmission&) {
  // TODO: queue the request with a simulated completion time.
  return false;
}

size_t FakeBackend::poll(std::vector<ReadCompletion>&) {
  // TODO: return requests whose simulated completion time has passed.
  return 0;
}

size_t FakeBackend::outstanding(Drive d) const {
  return outstanding_[static_cast<int>(d)];
}

}  // namespace blr
