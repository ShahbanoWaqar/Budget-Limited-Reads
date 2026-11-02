#include "policy/hedge_fixed.h"

#include "policy/selector.h"

namespace blr {

Drive HedgeFixed::choose_primary(const IoBackend& backend) {
  return choose_less_busy(backend, rr_);
}

bool HedgeFixed::try_hedge(const LogicalRead& read, const IoBackend&, uint64_t now_ns) {
  return !read.answered && !read.primary.done && !read.duplicate.has_value() &&
         (now_ns >= read.primary.submit_ns && now_ns - read.primary.submit_ns >= delay_ns_);
}

}  // namespace blr
