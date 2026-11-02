#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace blr {

using ReplicaId = uint64_t;

enum class Drive : int { A = 0, B = 1 };

inline Drive other(Drive d) { return d == Drive::A ? Drive::B : Drive::A; }

struct ReadSubmission {
  ReplicaId id;
  Drive drive;
  uint64_t offset;
  size_t length;
  void* buffer;  // must stay valid until the completion is polled
};

struct ReadCompletion {
  ReplicaId id;
  Drive drive;
  int64_t result;  // bytes read, or -errno
};

class IoBackend {
 public:
  virtual ~IoBackend() = default;

  // Returns false if the request could not be queued (e.g. outstanding cap).
  // Any exception must occur before exposing the buffer to asynchronous I/O.
  virtual bool submit(const ReadSubmission& req) = 0;

  // Appends finished operations to `out`; returns how many were appended.
  virtual size_t poll(std::vector<ReadCompletion>& out) = 0;

  virtual size_t outstanding(Drive d) const = 0;
};

}  // namespace blr
