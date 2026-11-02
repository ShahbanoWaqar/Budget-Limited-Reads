#pragma once

#include <cstdint>
#include <optional>
#include <new>
#include <memory>
#include <stdexcept>

#include "engine/io_backend.h"

namespace blr {

using LogicalId = uint64_t;

// Lifecycle of one logical read. The hedge permit (if any) is released only
// when the state reaches Drained.
enum class State {
  PrimaryInflight,  // first replica read issued
  HedgeEligible,    // hedge delay elapsed, primary still unfinished
  PairInflight,     // duplicate issued
  Answered,         // result returned, a losing read may still be running
  Drained           // every replica read of this logical read has completed
};

struct FreeBuffer {
  void operator()(uint8_t* p) const { ::operator delete(p, std::align_val_t{4096}); }
};
struct ReplicaRead {
  ReplicaId id = 0;
  Drive drive = Drive::A;
  std::unique_ptr<uint8_t, FreeBuffer> buffer;
  void allocate(size_t length) {
    buffer.reset(static_cast<uint8_t*>(::operator new(length, std::align_val_t{4096})));
  }
  uint64_t submit_ns = 0;
  uint64_t complete_ns = 0;
  bool done = false;
  bool verified = false;
  int64_t result = 0;  // bytes read or -errno
};

struct LogicalRead {
  LogicalId id = 0;
  uint64_t offset = 0;
  size_t length = 0;
  uint64_t arrival_ns = 0;
  State state = State::PrimaryInflight;
  ReplicaRead primary;
  std::optional<ReplicaRead> duplicate;  // never more than one
  bool answered = false;
  bool holds_permit = false;
  bool hedge_denied = false;
  uint64_t answer_ns = 0;
  ReplicaId winner = 0;
};

}  // namespace blr
