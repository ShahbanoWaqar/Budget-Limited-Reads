#include "mirror/mirror_reader.h"

#include <chrono>

#include "mirror/verify.h"

namespace blr {

namespace {

uint64_t steady_now_ns() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

}  // namespace

MirrorReader::MirrorReader(IoBackend& backend, Policy& policy, Counters& counters)
    : backend_(backend), policy_(policy), counters_(counters) {}

bool MirrorReader::all_issued_done(const LogicalRead& r) {
  // id == 0 means the primary was never accepted by the backend.
  if (r.primary.id == 0 || !r.primary.done) return false;
  return !r.duplicate.has_value() || r.duplicate->done;
}

bool MirrorReader::issue_primary(Entry& e, uint64_t now_ns) {
  LogicalRead& r = e.read;
  const Drive d = policy_.choose_primary(backend_);
  r.primary.drive = d;
  // Not yet submitted, so resizing the buffer here is safe. After a successful
  // submit the buffer is never resized or moved until the read is drained.
  r.primary.buffer.assign(r.length, 0);

  const ReplicaId id = next_replica_;
  const ReadSubmission sub{id, d, r.offset, r.length, r.primary.buffer.data()};
  if (!backend_.submit(sub)) return false;

  ++next_replica_;
  r.primary.id = id;
  r.primary.submit_ns = now_ns;
  r.state = State::PrimaryInflight;
  replica_to_logical_[id] = r.id;
  ++counters_.replica_issued;
  return true;
}

void MirrorReader::submit(uint64_t offset, size_t length, uint64_t now_ns) {
  auto entry = std::make_unique<Entry>();
  entry->read.id = next_logical_++;
  entry->read.offset = offset;
  entry->read.length = length;
  entry->read.arrival_ns = now_ns;

  Entry& ref = *entry;
  const LogicalId id = ref.read.id;
  reads_[id] = std::move(entry);
  ++counters_.logical_submitted;

  // Keep FIFO order: if anything is already waiting, queue behind it.
  if (!waiting_.empty() || !issue_primary(ref, now_ns)) waiting_.push_back(id);
}

void MirrorReader::answer(Entry& e, bool ok, bool from_duplicate, uint64_t now_ns) {
  LogicalRead& r = e.read;
  r.answered = true;
  r.state = State::Answered;
  e.answer_ns = now_ns;
  if (ok) {
    ++counters_.logical_succeeded;
    if (from_duplicate) ++counters_.duplicates_won;
  } else {
    ++counters_.logical_failed;
  }
  if (on_result_) on_result_(r, ok);
}

void MirrorReader::handle_completion(const ReadCompletion& c, uint64_t now_ns) {
  auto rit = replica_to_logical_.find(c.id);
  if (rit == replica_to_logical_.end()) return;  // unknown id; ignore
  const LogicalId lid = rit->second;
  replica_to_logical_.erase(rit);

  auto eit = reads_.find(lid);
  if (eit == reads_.end()) return;
  Entry& e = *eit->second;
  LogicalRead& r = e.read;

  ReplicaRead* rr = nullptr;
  bool is_duplicate = false;
  if (r.primary.id == c.id) {
    rr = &r.primary;
  } else if (r.duplicate.has_value() && r.duplicate->id == c.id) {
    rr = &*r.duplicate;
    is_duplicate = true;
  }
  if (rr == nullptr) return;

  rr->done = true;
  rr->complete_ns = now_ns;
  rr->result = c.result;
  ++counters_.replica_completed;
  if (c.result > 0) counters_.replica_bytes += static_cast<uint64_t>(c.result);

  // A replica read succeeds only if it is full-size and the bytes verify.
  const bool full_size = c.result >= 0 && static_cast<uint64_t>(c.result) == r.length;
  bool ok = full_size;
  if (full_size && !verify_read(r.offset, r.length, c.result, rr->buffer.data())) {
    ok = false;
    ++counters_.verify_failed;
  }
  if (!ok) ++counters_.replica_failed;

  if (r.answered) {
    // Losing read finishing after the answer: drained and counted, not re-answered.
    if (now_ns >= e.answer_ns) counters_.loser_cleanup_ns_total += now_ns - e.answer_ns;
  } else if (ok) {
    answer(e, true, is_duplicate, now_ns);
  } else if (all_issued_done(r)) {
    // No replica read succeeded and none is still running.
    answer(e, false, false, now_ns);
  }
  // else: this replica failed but another is still in flight; wait for it.

  if (r.answered && all_issued_done(r)) {
    r.state = State::Drained;
    if (r.duplicate.has_value()) policy_.on_pair_drained(r);
    reads_.erase(lid);  // frees the replica buffers: nothing can write into them now
  }
}

void MirrorReader::retry_waiting(uint64_t now_ns) {
  while (!waiting_.empty()) {
    auto it = reads_.find(waiting_.front());
    if (it == reads_.end()) {
      waiting_.pop_front();
      continue;
    }
    if (!issue_primary(*it->second, now_ns)) break;  // backend still full; keep FIFO
    waiting_.pop_front();
  }
}

void MirrorReader::fail_waiting(uint64_t now_ns) {
  std::deque<LogicalId> stuck;
  stuck.swap(waiting_);
  for (LogicalId id : stuck) {
    auto it = reads_.find(id);
    if (it == reads_.end()) continue;
    answer(*it->second, false, false, now_ns);
    reads_.erase(id);  // never issued, so there are no replica reads to drain
  }
}

void MirrorReader::run_once(uint64_t now_ns) {
  std::vector<ReadCompletion> batch;
  backend_.poll(batch);
  for (const ReadCompletion& c : batch) handle_completion(c, now_ns);
  retry_waiting(now_ns);
}

void MirrorReader::drain() {
  while (!reads_.empty()) {
    const uint64_t now = clock_ ? clock_() : steady_now_ns();
    run_once(now);
    if (replica_to_logical_.empty()) {
      // Nothing in flight, so no completion can ever free backend capacity.
      if (!waiting_.empty()) {
        fail_waiting(now);
      } else {
        break;  // nothing left that could make progress
      }
    }
  }
}

}  // namespace blr
