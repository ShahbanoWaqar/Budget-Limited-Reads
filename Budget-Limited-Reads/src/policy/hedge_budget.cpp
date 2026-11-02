#include "policy/hedge_budget.h"

#include "policy/selector.h"

namespace blr {

Drive HedgeBudget::choose_primary(const IoBackend& backend) {
  return choose_less_busy(backend, rr_);
}

bool HedgeBudget::try_hedge(const LogicalRead& read, const IoBackend& backend,
                            uint64_t now_ns) {
  if (read.answered || read.primary.done || read.duplicate.has_value()) return false;
  if (now_ns < read.primary.submit_ns || now_ns - read.primary.submit_ns < delay_ns_) return false;
  if (active_ >= permits_) return false;
  if (use_threshold_ &&
      backend.outstanding(other(read.primary.drive)) >= threshold_) {
    return false;
  }

  ++active_;
  return true;
}

void HedgeBudget::on_pair_drained(const LogicalRead& read) {
  if (read.holds_permit && active_ > 0) --active_;
}

}  // namespace blr
