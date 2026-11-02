#include "mirror/mirror_reader.h"
#include "mirror/verify.h"
#include "engine/fake_backend.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>
namespace blr {
MirrorReader::MirrorReader(IoBackend& b, Policy& p, Counters& c, size_t cap)
    : backend_(b), policy_(p), counters_(c), cap_(cap) {
  if (!cap) throw std::invalid_argument("outstanding cap must be positive");
}
MirrorReader::~MirrorReader() {
  // Callbacks may capture objects already destroyed by their caller.
  on_result_ = {}; on_replica_ = {};
  drain();
}
bool MirrorReader::submit(uint64_t offset, size_t length, uint64_t now, uint64_t arrival) {
  if (!length || offset % 4096 || length % 4096 || length > INT32_MAX || offset > INT64_MAX - length)
    throw std::invalid_argument("reads must be aligned, nonempty, and within signed file offsets");
  if (backend_.outstanding(Drive::A) + backend_.outstanding(Drive::B) >= cap_) return false;
  auto r = std::make_unique<LogicalRead>();
  r->id = next_logical_++; r->offset = offset; r->length = length; r->arrival_ns = arrival == UINT64_MAX ? now : arrival;
  auto& p = r->primary;
  p.id = next_replica_++; p.drive = policy_.choose_primary(backend_); p.submit_ns = now; p.allocate(length);
  // Allocate ownership before exposing a buffer to asynchronous I/O.
  auto id = r->id;
  reads_.emplace(id, std::move(r));
  bool accepted = false;
  try {
    accepted = backend_.submit({p.id, p.drive, offset, length, p.buffer.get()});
  } catch (...) {
    // Backend setup exceptions occur before the buffer is exposed to I/O.
    reads_.erase(id);
    throw;
  }
  if (!accepted) { reads_.erase(id); return false; }
  ++counters_.logical_submitted; ++counters_.replica_issued;
  ++counters_.outstanding[static_cast<int>(p.drive)];
  counters_.peak_outstanding = std::max(counters_.peak_outstanding,
      counters_.outstanding[0] + counters_.outstanding[1]);
  now_ = std::max(now_, now);
  return true;
}
void MirrorReader::answer(LogicalRead& r, bool ok, uint64_t now) {
  if (r.answered) return;
  r.answered = true; r.answer_ns = now; r.state = State::Answered;
  if (ok) ++counters_.logical_succeeded; else ++counters_.logical_failed;
  if (on_result_) on_result_(r, ok);
}
void MirrorReader::reap(uint64_t now) {
  std::vector<ReadCompletion> done;
  backend_.poll(done);
  for (const auto& c : done) {
    for (auto& item : reads_) {
      auto& r = *item.second;
      ReplicaRead* p = r.primary.id == c.id ? &r.primary :
          (r.duplicate && r.duplicate->id == c.id ? &*r.duplicate : nullptr);
      if (!p || p->done) continue;
      p->done = true; p->result = c.result; p->complete_ns = now;
      ++counters_.replica_completed;
      --counters_.outstanding[static_cast<int>(p->drive)];
      if (c.result > 0) counters_.replica_bytes += c.result;
      bool valid = verify_read(r.offset, r.length, c.result, p->buffer.get());
      p->verified = valid;
      if (!valid) ++counters_.replica_failed;
      bool winner = valid && !r.answered;
      if (winner) {
        r.winner = p->id;
        if (r.duplicate && p == &*r.duplicate) ++counters_.duplicates_won;
        answer(r, true, now);
      }
      if (on_replica_) on_replica_(r.id, *p, winner);
      if (r.primary.done && (!r.duplicate || r.duplicate->done)) {
        if (!r.answered) answer(r, false, now);
        if (r.duplicate) counters_.loser_cleanup_ns_total += now - r.answer_ns;
        r.state = State::Drained;
        if (r.holds_permit) { policy_.on_pair_drained(r); r.holds_permit = false; --counters_.active_pair_permits; }
      }
      break;
    }
  }
  for (auto it = reads_.begin(); it != reads_.end();) {
    if (it->second->state == State::Drained) it = reads_.erase(it); else ++it;
  }
}
void MirrorReader::run_once(uint64_t now) {
  now_ = std::max(now_, now);
  reap(now_); // Reap before evaluating timers, including completion-at-deadline races.
  for (auto& item : reads_) {
    auto& r = *item.second;
    if (r.answered || r.primary.done || r.duplicate || now_ < r.primary.submit_ns ||
        now_ - r.primary.submit_ns < policy_.hedge_delay_ns()) continue;
    r.state = State::HedgeEligible;
    if (backend_.outstanding(Drive::A) + backend_.outstanding(Drive::B) >= cap_) continue;
    ReplicaRead candidate;
    candidate.id = next_replica_++; candidate.drive = other(r.primary.drive);
    candidate.submit_ns = now_; candidate.allocate(r.length);
    if (!policy_.try_hedge(r, backend_, now_)) {
      if (!r.hedge_denied) { ++counters_.duplicates_denied; r.hedge_denied = true; }
      continue;
    }
    r.holds_permit = policy_.uses_permits();
    r.duplicate.emplace(std::move(candidate));
    auto& d = *r.duplicate;
    bool accepted = false;
    try {
      accepted = backend_.submit({d.id, d.drive, r.offset, r.length, d.buffer.get()});
    } catch (...) {
      if (r.holds_permit) policy_.on_pair_drained(r);
      r.holds_permit = false; r.duplicate.reset();
      throw;
    }
    if (!accepted) {
      if (r.holds_permit) policy_.on_pair_drained(r);
      r.holds_permit = false; r.duplicate.reset(); continue;
    }
    ++counters_.duplicates_issued; ++counters_.replica_issued;
    ++counters_.outstanding[static_cast<int>(d.drive)];
    counters_.peak_outstanding = std::max(counters_.peak_outstanding,
        counters_.outstanding[0] + counters_.outstanding[1]);
    if (r.holds_permit) {
      ++counters_.active_pair_permits;
      counters_.peak_pair_permits = std::max(counters_.peak_pair_permits, counters_.active_pair_permits);
    }
    r.state = State::PairInflight;
  }
}
void MirrorReader::drain(std::function<uint64_t()> progress) {
  // Shutdown issues no new hedges; it only completes operations already accepted.
  auto previous = std::chrono::steady_clock::now();
  while (!reads_.empty()) {
    if (progress) now_ = std::max(now_, progress());
    else if (auto* fake = dynamic_cast<FakeBackend*>(&backend_)) { now_ += 1000; fake->set_time(now_); }
    else {
      auto current = std::chrono::steady_clock::now();
      now_ += std::chrono::duration_cast<std::chrono::nanoseconds>(current - previous).count();
      previous = current;
    }
    reap(now_);
    if (!progress) std::this_thread::yield();
  }
}
}
