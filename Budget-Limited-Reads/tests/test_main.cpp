#include <cerrno>
#include <cstdio>
#include <functional>
#include <filesystem>
#include <chrono>
#include <stdexcept>
#include <vector>
#include "engine/fake_backend.h"
#include "mirror/mirror_reader.h"
#include "mirror/verify.h"
#include "accounting/logger.h"
#include "policy/hedge_budget.h"
#include "policy/hedge_fixed.h"
#include "policy/selector.h"
#include "workload/trace.h"
using namespace blr;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while(0)
struct ThrowingBackend : FakeBackend {
  bool reject = true;
  bool submit(const ReadSubmission& req) override {
    if (reject) throw std::runtime_error("injected backend setup failure");
    return FakeBackend::submit(req);
  }
};
struct Fixture {
  FakeBackend backend;
  Counters c;
  HedgeBudget policy{10, 1, 10};
  unsigned answers=0; bool ok=false;
  MirrorReader reader{backend,policy,c};
  Fixture() { reader.set_result_callback([&](const auto&,bool success){++answers;ok=success;}); }
  void tick(uint64_t t) { backend.set_time(t); reader.run_once(t); }
};
int main() {
  std::vector<std::pair<const char*,std::function<void()>>> tests = {
    {"simultaneous_completions_return_one_result", [] {
      Fixture f; f.backend.set_latency(Drive::A,20); f.backend.set_latency(Drive::B,10);
      CHECK(f.reader.submit(0,4096,0)); f.tick(10); CHECK(f.policy.active_permits()==1);
      f.tick(20); CHECK(f.answers==1 && f.ok && f.c.replica_completed==2 && !f.reader.pending());
      CHECK(f.policy.active_permits()==0);
    }},
    {"first_request_fails_second_succeeds", [] {
      Fixture f; f.backend.enqueue_outcome(Drive::A,{20,-EIO}); f.backend.enqueue_outcome(Drive::B,{20,4096});
      f.reader.submit(0,4096,0); f.tick(10); f.tick(20); CHECK(f.answers==0);
      f.tick(30); CHECK(f.ok && f.answers==1 && f.c.replica_failed==1 && f.c.duplicates_won==1);
    }},
    {"both_requests_fail_is_not_success", [] {
      Fixture f; f.backend.enqueue_outcome(Drive::A,{20,-EIO}); f.backend.enqueue_outcome(Drive::B,{20,-EIO});
      f.reader.submit(0,4096,0); f.tick(10); f.tick(30);
      CHECK(!f.ok && f.answers==1 && f.c.logical_failed==1 && f.c.replica_failed==2);
    }},
    {"full_permit_pool_denies_duplicate", [] {
      Fixture f; f.reader.submit(0,4096,0); f.reader.submit(4096,4096,0); f.tick(10);
      CHECK(f.c.duplicates_issued==1 && f.c.duplicates_denied==1 && f.policy.active_permits()==1);
      f.reader.drain(); CHECK(f.policy.active_permits()==0);
    }},
    {"original_completes_at_duplicate_deadline", [] {
      Fixture f; f.backend.set_latency(Drive::A,10); f.reader.submit(0,4096,0); f.tick(10);
      CHECK(f.answers==1 && f.c.duplicates_issued==0);
    }},
    {"never_creates_third_request", [] {
      Fixture f; f.reader.submit(0,4096,0); for(int i=10;i<50;++i) f.tick(i);
      CHECK(f.c.replica_issued==2); f.reader.drain();
    }},
    {"buffers_remain_valid_and_loser_counted_after_answer", [] {
      Fixture f; f.backend.set_latency(Drive::A,100); f.backend.set_latency(Drive::B,10);
      f.reader.submit(4096,4096,0); f.tick(10); f.tick(20);
      CHECK(f.ok && f.answers==1 && f.reader.pending()==1 && f.c.replica_completed==1);
      CHECK(f.policy.active_permits()==1);
      f.tick(100); CHECK(f.c.replica_completed==2 && f.c.replica_bytes==8192);
      CHECK(f.c.loser_cleanup_ns_total==80 && f.policy.active_permits()==0);
    }},
    {"shutdown_drains_all_replica_reads", [] {
      Fixture f; f.reader.submit(0,4096,0); f.tick(10); f.reader.drain();
      CHECK(f.c.replica_completed==f.c.replica_issued && f.answers==1 && !f.reader.pending());
    }},
    {"destructor_drains_inflight_buffers", [] {
      FakeBackend b; Counters c; HedgeFixed p(0);
      { MirrorReader r(b,p,c); r.submit(0,4096,0); r.run_once(0); }
      CHECK(c.replica_completed==2 && b.outstanding(Drive::A)==0 && b.outstanding(Drive::B)==0);
    }},
    {"threshold_blocks_and_ablation_allows", [] {
      FakeBackend b; Counters c; HedgeBudget p(0,1,0); MirrorReader r(b,p,c);
      r.submit(0,4096,0); r.run_once(0); CHECK(c.duplicates_issued==0); r.drain();
      HedgeBudget q(0,1,0,false); MirrorReader s(b,q,c); s.submit(0,4096,0); s.run_once(0);
      CHECK(c.duplicates_issued==1); s.drain(); CHECK(q.active_permits()==0);
    }},
    {"physical_cap_and_submit_backpressure", [] {
      FakeBackend b; Counters c; HedgeFixed p(0); MirrorReader r(b,p,c,1);
      CHECK(r.submit(0,4096,0)); CHECK(!r.submit(4096,4096,0)); r.run_once(0);
      CHECK(c.replica_issued==1); r.drain(); CHECK(c.logical_submitted==1);
    }},
    {"rejected_duplicate_releases_permit", [] {
      FakeBackend b(1); Counters c; HedgeBudget p(0,1,10); MirrorReader r(b,p,c,2);
      r.submit(0,4096,0); r.run_once(0); CHECK(p.active_permits()==0 && c.duplicates_issued==0); r.drain();
    }},
    {"corrupt_and_short_reads_are_failures", [] {
      Fixture f; f.backend.enqueue_outcome(Drive::A,{20,4096,true}); f.backend.enqueue_outcome(Drive::B,{20,1024});
      f.reader.submit(0,4096,0); f.tick(10); f.tick(30);
      CHECK(f.c.logical_failed==1 && f.c.replica_failed==2 && f.c.replica_bytes==5120);
    }},
    {"single_failure_does_not_create_late_hedge", [] {
      Fixture f; f.backend.enqueue_outcome(Drive::A,{5,-EIO}); f.reader.submit(0,4096,0); f.tick(10);
      CHECK(f.c.logical_failed==1 && f.c.replica_issued==1 && !f.reader.pending());
    }},
    {"fixed_policy_checks_completed_primary", [] {
      FakeBackend b; HedgeFixed p(10); LogicalRead r; r.primary.done=true;
      CHECK(!p.try_hedge(r,b,100)); r.primary.done=false; CHECK(p.try_hedge(r,b,100));
      r.duplicate.emplace(); CHECK(!p.try_hedge(r,b,100));
    }},
    {"selectors_and_known_data", [] {
      FakeBackend b; NoDupRoundRobin rr; NoDupLessBusy lb;
      CHECK(rr.choose_primary(b)==Drive::A && rr.choose_primary(b)==Drive::B);
      uint8_t data[4096]; fill_expected(4096,4096,data);
      CHECK(verify_read(4096,4096,4096,data)); CHECK(!verify_read(0,4096,4096,data));
      ReadSubmission req{1,Drive::A,0,4096,data}; CHECK(b.submit(req)); CHECK(lb.choose_primary(b)==Drive::B);
      b.set_time(100000); std::vector<ReadCompletion> done; b.poll(done);
    }},
    {"primary_setup_exception_preserves_shutdown", [] {
      ThrowingBackend b; Counters c; HedgeBudget p(0,1,10); MirrorReader r(b,p,c);
      bool threw=false;
      try { r.submit(0,4096,0); } catch(const std::runtime_error&) { threw=true; }
      CHECK(threw && r.pending()==0 && c.replica_issued==0); r.drain();
    }},
    {"hedge_setup_exception_releases_reservation", [] {
      ThrowingBackend b; b.reject=false; Counters c; HedgeBudget p(0,1,10); MirrorReader r(b,p,c);
      CHECK(r.submit(0,4096,0)); b.reject=true; bool threw=false;
      try { r.run_once(0); } catch(const std::runtime_error&) { threw=true; }
      CHECK(threw && p.active_permits()==0 && c.active_pair_permits==0);
      r.drain(); CHECK(c.replica_completed==1 && c.logical_succeeded==1);
    }},
    {"trace_roundtrip_and_logging", [] {
      auto dir = std::filesystem::temp_directory_path() /
          ("blr-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
      CHECK(std::filesystem::create_directory(dir));
      struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
      } cleanup{dir};
      std::string base=(dir / "test").string();
      CHECK(save_trace(base+".trace.csv",{{0,0,4096},{10,4096,4096}}));
      std::vector<TraceEntry> trace; CHECK(load_trace(base+".trace.csv",trace)); CHECK(trace.size()==2);
      Fixture f; Logger logger; CHECK(logger.open(base+".logical.csv",base+".replica.csv"));
      f.reader.set_result_callback([&](const auto& r,bool ok){logger.log_logical(r,ok);});
      f.reader.set_replica_callback([&](auto id,const auto& r,bool win){logger.log_replica(id,r,win);});
      f.reader.submit(0,4096,0); f.reader.drain();
      std::ifstream in(base+".replica.csv"); std::string line; unsigned lines=0;
      while(std::getline(in,line)) ++lines; CHECK(lines==2);
    }}
  };
  unsigned failed=0;
  for(auto& t:tests) { try { t.second(); std::printf("[PASS] %s\n",t.first); }
    catch(const std::exception& e) { ++failed; std::printf("[FAIL] %s: %s\n",t.first,e.what()); } }
  return failed?1:0;
}
