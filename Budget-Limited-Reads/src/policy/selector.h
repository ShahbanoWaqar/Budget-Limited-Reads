#pragma once

#include "policy/policy.h"

namespace blr {

// No-duplicate baseline: alternate drives.
class NoDupRoundRobin : public Policy {
 public:
  const char* name() const override { return "nodup-roundrobin"; }
  Drive choose_primary(const IoBackend& backend) override;
  bool try_hedge(const LogicalRead&, const IoBackend&, uint64_t) override { return false; }
  void on_pair_drained(const LogicalRead&) override {}

 private:
  int next_ = 0;
};

// No-duplicate baseline: fewer outstanding reads first, round-robin on ties.
class NoDupLessBusy : public Policy {
 public:
  const char* name() const override { return "nodup-lessbusy"; }
  Drive choose_primary(const IoBackend& backend) override;
  bool try_hedge(const LogicalRead&, const IoBackend&, uint64_t) override { return false; }
  void on_pair_drained(const LogicalRead&) override {}

 private:
  int next_ = 0;
};

// Single-drive reference: every read goes to one fixed drive, no duplicates.
// Used to measure each drive alone (Study A).
class NoDupSingleDrive : public Policy {
 public:
  explicit NoDupSingleDrive(Drive d) : drive_(d) {}
  const char* name() const override { return drive_ == Drive::A ? "single-A" : "single-B"; }
  Drive choose_primary(const IoBackend&) override { return drive_; }
  bool try_hedge(const LogicalRead&, const IoBackend&, uint64_t) override { return false; }
  void on_pair_drained(const LogicalRead&) override {}

 private:
  Drive drive_;
};

// Shared primary-selection rule used by the hedging policies.
Drive choose_less_busy(const IoBackend& backend, int& rr_state);

}  // namespace blr
