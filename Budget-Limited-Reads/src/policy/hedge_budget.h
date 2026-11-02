#pragma once

#include "policy/policy.h"

namespace blr {

// Budget-limited duplication: a small pool of permits for active hedged pairs,
// plus an alternate-drive outstanding-read threshold. A permit is held until
// both replica reads of the pair have finished.
class HedgeBudget : public Policy {
 public:
  // use_threshold = false disables the alternate-drive check (Study C ablation).
  HedgeBudget(uint64_t hedge_delay_ns, unsigned permits,
              size_t alt_drive_threshold, bool use_threshold = true)
      : delay_ns_(hedge_delay_ns),
        permits_(permits),
        threshold_(alt_drive_threshold),
        use_threshold_(use_threshold) {}

  const char* name() const override { return "hedge-budget"; }
  uint64_t hedge_delay_ns() const override { return delay_ns_; }
  Drive choose_primary(const IoBackend& backend) override;
  bool try_hedge(const LogicalRead& read, const IoBackend& backend,
                 uint64_t now_ns) override;
  void on_pair_drained(const LogicalRead& read) override;

  bool uses_permits() const override { return true; }
  unsigned active_permits() const { return active_; }

 private:
  uint64_t delay_ns_;
  unsigned permits_;
  size_t threshold_;
  bool use_threshold_;
  unsigned active_ = 0;
  int rr_ = 0;
};

}  // namespace blr
