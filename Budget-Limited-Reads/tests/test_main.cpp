// Minimal assert-based runner; no external test framework.
// Real tests cover verify and the policy-free mirror reader (NoDupRoundRobin +
// FakeBackend). Cases that need duplicate reads are still placeholders until
// the hedge path is wired into the reader.

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "accounting/counters.h"
#include "engine/fake_backend.h"
#include "mirror/mirror_reader.h"
#include "mirror/pattern.h"
#include "mirror/verify.h"
#include "policy/selector.h"

namespace {

using namespace blr;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("    check failed: %s (line %d)\n", #cond, __LINE__);  \
      return false;                                                       \
    }                                                                     \
  } while (0)

struct TestCase {
  std::string name;
  std::function<bool()> fn;  // return true on pass
  bool placeholder = false;
};

bool pending() { return true; }  // TODO: replace once the hedge path exists

std::vector<uint8_t> pattern_buf(uint64_t offset, size_t len) {
  std::vector<uint8_t> buf(len);
  fill_pattern(offset, buf.data(), len);
  return buf;
}

// Reader + fake backend + no-duplicate policy, with simulated time.
struct Rig {
  FakeBackend fake;
  NoDupRoundRobin policy;
  Counters counters;
  MirrorReader reader{fake, policy, counters};
  std::vector<std::pair<LogicalId, bool>> results;
  bool data_ok = true;  // every answered-ok result had verifiable bytes
  uint64_t t = 0;

  Rig() {
    reader.set_result_callback([this](const LogicalRead& r, bool ok) {
      results.emplace_back(r.id, ok);
      if (ok) {
        data_ok = data_ok && verify_read(r.offset, r.length, r.primary.result,
                                         r.primary.buffer.data());
      }
    });
    // drain() has no time argument: each tick advances simulated time.
    reader.set_clock([this] {
      t += 1000;
      fake.advance_to(t);
      return t;
    });
  }
  Rig(const Rig&) = delete;
  Rig& operator=(const Rig&) = delete;

  void step(uint64_t ns) {
    t += ns;
    fake.advance_to(t);
    reader.run_once(t);
  }
};

// ---- verify ----------------------------------------------------------------

bool verify_accepts_correct_data() {
  const std::pair<uint64_t, size_t> cases[] = {
      {0, 64}, {0, 1}, {3, 13}, {7, 1}, {5, 8}, {8, 8}, {1000, 4096}, {12345, 777}};
  for (const auto& c : cases) {
    auto buf = pattern_buf(c.first, c.second);
    CHECK(verify_read(c.first, c.second, static_cast<int64_t>(c.second), buf.data()));
  }
  return true;
}

bool verify_rejects_flipped_byte() {
  // offset 5, len 100: index 0 is in the unaligned head, 50 in the body, 99 in the tail.
  for (size_t idx : {size_t{0}, size_t{50}, size_t{99}}) {
    auto buf = pattern_buf(5, 100);
    buf[idx] ^= 0x01;
    CHECK(!verify_read(5, 100, 100, buf.data()));
  }
  return true;
}

bool verify_rejects_wrong_size() {
  auto buf = pattern_buf(0, 64);
  CHECK(!verify_read(0, 64, 63, buf.data()));  // short read
  CHECK(!verify_read(0, 64, 65, buf.data()));  // long read
  CHECK(!verify_read(0, 64, 0, buf.data()));
  return true;
}

bool verify_rejects_negative_result() {
  auto buf = pattern_buf(0, 64);
  CHECK(!verify_read(0, 64, -5, buf.data()));
  return true;
}

bool verify_rejects_null_data() {
  CHECK(!verify_read(0, 8, 8, nullptr));
  return true;
}

bool verify_zero_length() {
  CHECK(verify_read(100, 0, 0, nullptr));
  CHECK(!verify_read(100, 0, 1, nullptr));
  return true;
}

bool verify_rejects_data_from_wrong_offset() {
  auto buf = pattern_buf(4096, 64);
  CHECK(verify_read(4096, 64, 64, buf.data()));
  CHECK(!verify_read(4104, 64, 64, buf.data()));  // shifted by one word
  CHECK(!verify_read(4097, 64, 64, buf.data()));  // shifted by one byte
  return true;
}

// ---- mirror reader (no duplicates) -----------------------------------------

bool reader_answers_once_with_verified_data() {
  Rig r;
  r.reader.submit(0, 4096, r.t);
  r.step(50000);  // before the 100 us latency
  CHECK(r.results.empty());
  r.step(100000);
  CHECK(r.results.size() == 1 && r.results[0].second);
  r.step(100000);
  r.step(100000);
  CHECK(r.results.size() == 1);  // never answered twice
  CHECK(r.data_ok);
  CHECK(r.counters.logical_submitted == 1 && r.counters.logical_succeeded == 1);
  CHECK(r.counters.replica_issued == 1 && r.counters.replica_completed == 1);
  CHECK(r.counters.replica_bytes == 4096);
  CHECK(r.reader.pending() == 0);
  return true;
}

bool reader_alternates_primary_drives() {
  Rig r;
  r.reader.submit(0, 512, r.t);
  r.reader.submit(512, 512, r.t);
  CHECK(r.fake.outstanding(Drive::A) == 1);
  CHECK(r.fake.outstanding(Drive::B) == 1);
  return true;
}

bool failed_read_reports_failure_not_success() {
  Rig r;
  r.fake.fail_next(Drive::A, 1);  // round-robin sends the first read to A
  r.reader.submit(0, 4096, r.t);
  r.step(200000);
  CHECK(r.results.size() == 1 && !r.results[0].second);
  CHECK(r.counters.logical_failed == 1 && r.counters.logical_succeeded == 0);
  CHECK(r.counters.replica_failed == 1 && r.counters.verify_failed == 0);
  CHECK(r.reader.pending() == 0);
  return true;
}

bool corrupt_data_is_rejected_and_counted_separately() {
  Rig r;
  r.fake.corrupt_next(Drive::A, 1);
  r.reader.submit(0, 4096, r.t);
  r.step(200000);
  CHECK(r.results.size() == 1 && !r.results[0].second);
  CHECK(r.counters.verify_failed == 1 && r.counters.replica_failed == 1);
  CHECK(r.counters.logical_succeeded == 0);
  return true;
}

bool many_reads_in_flight_all_verify() {
  Rig r;
  const int n = 200;
  for (int i = 0; i < n; ++i) {
    r.reader.submit(static_cast<uint64_t>(i) * 513, 1 + static_cast<size_t>(i % 300), r.t);
  }
  CHECK(r.fake.outstanding_total() == static_cast<size_t>(n));
  r.step(200000);
  CHECK(r.results.size() == static_cast<size_t>(n));
  for (const auto& res : r.results) CHECK(res.second);
  CHECK(r.data_ok);
  CHECK(r.counters.logical_succeeded == static_cast<uint64_t>(n));
  CHECK(r.counters.replica_issued == static_cast<uint64_t>(n));
  CHECK(r.reader.pending() == 0);
  return true;
}

bool submits_beyond_backend_cap_are_queued_not_dropped() {
  Rig r;
  r.fake.set_max_outstanding(2);
  for (int i = 0; i < 5; ++i) r.reader.submit(static_cast<uint64_t>(i) * 4096, 4096, r.t);
  CHECK(r.fake.outstanding_total() == 2);
  r.reader.drain();
  CHECK(r.results.size() == 5);
  for (const auto& res : r.results) CHECK(res.second);
  CHECK(r.fake.submitted_total() == 5);
  CHECK(r.counters.logical_succeeded == 5);
  return true;
}

bool queued_reads_fail_if_backend_never_accepts() {
  Rig r;
  r.fake.set_reject_all(true);
  r.reader.submit(0, 4096, r.t);
  r.reader.submit(4096, 4096, r.t);
  r.reader.drain();  // must return rather than loop forever
  CHECK(r.results.size() == 2);
  CHECK(!r.results[0].second && !r.results[1].second);
  CHECK(r.counters.logical_failed == 2 && r.counters.replica_issued == 0);
  CHECK(r.reader.pending() == 0);
  return true;
}

bool shutdown_drains_all_replica_reads() {
  Rig r;
  for (int i = 0; i < 10; ++i) r.reader.submit(static_cast<uint64_t>(i) * 4096, 4096, r.t);
  r.reader.drain();
  CHECK(r.fake.outstanding_total() == 0);
  CHECK(r.counters.replica_issued == 10 && r.counters.replica_completed == 10);
  CHECK(r.counters.logical_succeeded == 10);
  CHECK(r.reader.pending() == 0);
  return true;
}

bool corrupt_data_on_drive_b_is_rejected() {
  Rig r;
  r.fake.corrupt_next(Drive::B, 1);
  r.reader.submit(0, 4096, r.t);     // round-robin: drive A
  r.reader.submit(4096, 4096, r.t);  // round-robin: drive B (corrupted)
  CHECK(r.fake.outstanding(Drive::A) == 1 && r.fake.outstanding(Drive::B) == 1);
  r.step(200000);
  CHECK(r.results.size() == 2);
  CHECK(r.results[0].first == 1 && r.results[0].second);
  CHECK(r.results[1].first == 2 && !r.results[1].second);
  CHECK(r.counters.verify_failed == 1 && r.counters.replica_failed == 1);
  CHECK(r.counters.logical_succeeded == 1 && r.counters.logical_failed == 1);
  CHECK(r.data_ok);
  CHECK(r.reader.pending() == 0);
  return true;
}

bool queued_reads_complete_in_fifo_order() {
  Rig r;
  r.fake.set_max_outstanding(1);  // one read at a time: everything else queues
  const int n = 4;
  for (int i = 0; i < n; ++i) r.reader.submit(static_cast<uint64_t>(i) * 4096, 4096, r.t);
  CHECK(r.fake.outstanding_total() == 1);
  r.reader.drain();
  CHECK(r.results.size() == static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    CHECK(r.results[static_cast<size_t>(i)].first == static_cast<LogicalId>(i + 1));
    CHECK(r.results[static_cast<size_t>(i)].second);
  }
  CHECK(r.data_ok);
  CHECK(r.reader.pending() == 0);
  return true;
}

}  // namespace

int main() {
  std::vector<TestCase> tests = {
      // verify
      {"verify_accepts_correct_data", verify_accepts_correct_data},
      {"verify_rejects_flipped_byte", verify_rejects_flipped_byte},
      {"verify_rejects_wrong_size", verify_rejects_wrong_size},
      {"verify_rejects_negative_result", verify_rejects_negative_result},
      {"verify_rejects_null_data", verify_rejects_null_data},
      {"verify_zero_length", verify_zero_length},
      {"verify_rejects_data_from_wrong_offset", verify_rejects_data_from_wrong_offset},
      // mirror reader, no duplicates
      {"reader_answers_once_with_verified_data", reader_answers_once_with_verified_data},
      {"reader_alternates_primary_drives", reader_alternates_primary_drives},
      {"failed_read_reports_failure_not_success", failed_read_reports_failure_not_success},
      {"corrupt_data_is_rejected_and_counted_separately",
       corrupt_data_is_rejected_and_counted_separately},
      {"many_reads_in_flight_all_verify", many_reads_in_flight_all_verify},
      {"submits_beyond_backend_cap_are_queued_not_dropped",
       submits_beyond_backend_cap_are_queued_not_dropped},
      {"queued_reads_fail_if_backend_never_accepts", queued_reads_fail_if_backend_never_accepts},
      {"shutdown_drains_all_replica_reads", shutdown_drains_all_replica_reads},
      {"corrupt_data_on_drive_b_is_rejected", corrupt_data_on_drive_b_is_rejected},
      {"queued_reads_complete_in_fifo_order", queued_reads_complete_in_fifo_order},
      // need duplicate reads (hedge step)
      {"simultaneous_completions_return_one_result", pending, true},
      {"first_request_fails_second_succeeds", pending, true},
      {"both_requests_fail_is_not_success", pending, true},
      {"full_permit_pool_denies_duplicate", pending, true},
      {"original_completes_before_duplicate_submit", pending, true},
      {"never_creates_third_request", pending, true},
      {"buffers_not_freed_while_read_may_write", pending, true},
      {"loser_counted_after_answer", pending, true},
  };

  int failed = 0;
  int placeholders = 0;
  for (const auto& t : tests) {
    bool ok = t.fn();
    std::printf("[%s] %s%s\n", ok ? "PASS" : "FAIL", t.name.c_str(),
                t.placeholder ? " (placeholder)" : "");
    if (!ok) ++failed;
    if (t.placeholder) ++placeholders;
  }
  std::printf("%zu tests, %d failed, %d placeholders\n", tests.size(), failed, placeholders);
  return failed == 0 ? 0 : 1;
}
