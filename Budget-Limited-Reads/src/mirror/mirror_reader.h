#pragma once
#include <functional>
#include <map>
#include <memory>
#include "accounting/counters.h"
#include "engine/io_backend.h"
#include "mirror/request.h"
#include "policy/policy.h"
namespace blr {
class MirrorReader {
 public:
  using ResultCallback = std::function<void(const LogicalRead&, bool)>;
  using ReplicaCallback = std::function<void(LogicalId, const ReplicaRead&, bool)>;
  MirrorReader(IoBackend&, Policy&, Counters&, size_t max_outstanding = 128);
  ~MirrorReader();
  // False means backpressure; caller retains the arrival and retries it.
  bool submit(uint64_t offset, size_t length, uint64_t now_ns, uint64_t arrival_ns = UINT64_MAX);
  void run_once(uint64_t now_ns);
  // Fake backends require a progress callback to advance virtual time.
  void drain(std::function<uint64_t()> progress = {});
  size_t pending() const { return reads_.size(); }
  void set_result_callback(ResultCallback cb) { on_result_ = std::move(cb); }
  void set_replica_callback(ReplicaCallback cb) { on_replica_ = std::move(cb); }
 private:
  void reap(uint64_t now);
  void answer(LogicalRead&, bool, uint64_t);
  IoBackend& backend_;
  Policy& policy_;
  Counters& counters_;
  size_t cap_;
  uint64_t next_logical_ = 1, next_replica_ = 1, now_ = 0;
  std::map<LogicalId, std::unique_ptr<LogicalRead>> reads_;
  ResultCallback on_result_;
  ReplicaCallback on_replica_;
};
}
