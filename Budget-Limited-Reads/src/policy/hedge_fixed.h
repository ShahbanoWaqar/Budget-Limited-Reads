#pragma once

#include "policy/policy.h"

namespace blr {

// Fixed-delay duplication. No pair budget, but the engine-wide total
// physical-I/O cap still applies (enforced by the backend/reader).
class HedgeFixed : public Policy {
 public:
  explicit HedgeFixed(uint64_t hedge_delay_ns) : delay_ns_(hedge_delay_ns) {}

  const char* name() const override { return "hedge-fixed"; }
  uint64_t hedge_delay_ns() const override { return delay_ns_; }
  Drive choose_primary(const IoBackend& backend) override;
  bool try_hedge(const LogicalRead& read, const IoBackend& backend,
                 uint64_t now_ns) override;
  void on_pair_drained(const LogicalRead&) override {}

  uint64_t delay_ns() const { return delay_ns_; }

 private:
  uint64_t delay_ns_;
  int rr_ = 0;
};

}  // namespace blr
