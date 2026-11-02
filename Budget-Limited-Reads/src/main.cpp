#include <stdexcept>
#include <string>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <thread>
#include "accounting/logger.h"
#include "engine/fake_backend.h"
#ifdef BLR_HAVE_URING
#include "engine/uring_backend.h"
#endif
#ifdef BLR_HAVE_WINDOWS
#include "engine/windows_backend.h"
#endif
#include "mirror/mirror_reader.h"
#include "policy/hedge_budget.h"
#include "policy/hedge_fixed.h"
#include "policy/selector.h"
#include "workload/trace.h"
int main(int argc, char** argv) {
  try {
    std::map<std::string, std::string> opt;
    const std::string allowed = " --mode --trace --file-a --file-b --delay-us --permits --threshold --max-outstanding --out-prefix --fake-a-us --fake-b-us ";
    for (int i=1; i<argc; ++i) {
      std::string k=argv[i];
      if (k=="--fake" || k=="--no-threshold") { opt[k]="1"; continue; }
      if (allowed.find(" "+k+" ")==std::string::npos || i+1==argc) throw std::runtime_error("unknown/incomplete option: "+k);
      opt[k]=argv[++i];
    }
    auto number = [&](const std::string& k, uint64_t fallback) {
      if (!opt.count(k)) return fallback;
      const auto& s=opt[k];
      if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("invalid number: "+k);
      auto n=std::stoull(s); if(n>1000000000ULL) throw std::runtime_error("option too large: "+k);
      return static_cast<uint64_t>(n);
    };
    if (!opt.count("--mode") || !opt.count("--trace")) throw std::runtime_error("usage: blr --mode rr|lessbusy|fixed|budget --trace CSV [--fake | --file-a A --file-b B] [--delay-us N --permits N --threshold N --max-outstanding N --out-prefix PATH --no-threshold]");
    size_t cap=number("--max-outstanding",128); if (!cap) throw std::runtime_error("cap must be positive");
    auto delay=number("--delay-us",50)*1000;
    std::unique_ptr<blr::Policy> policy;
    auto mode=opt["--mode"];
    if(mode=="rr") policy=std::make_unique<blr::NoDupRoundRobin>();
    else if(mode=="lessbusy") policy=std::make_unique<blr::NoDupLessBusy>();
    else if(mode=="fixed") policy=std::make_unique<blr::HedgeFixed>(delay);
    else if(mode=="budget") policy=std::make_unique<blr::HedgeBudget>(delay,number("--permits",4),number("--threshold",16),!opt.count("--no-threshold"));
    else throw std::runtime_error("unknown mode");
    std::vector<blr::TraceEntry> trace;
    if(!blr::load_trace(opt["--trace"],trace)) throw std::runtime_error("cannot load valid trace");
    std::unique_ptr<blr::IoBackend> backend;
    blr::FakeBackend* fake=nullptr;
    if(opt.count("--fake")) {
      auto b=std::make_unique<blr::FakeBackend>(cap); fake=b.get();
      fake->set_latency(blr::Drive::A,number("--fake-a-us",200)*1000);
      fake->set_latency(blr::Drive::B,number("--fake-b-us",50)*1000); backend=std::move(b);
    } else {
#if defined(BLR_HAVE_URING) || defined(BLR_HAVE_WINDOWS)
      if(!opt.count("--file-a") || !opt.count("--file-b")) throw std::runtime_error("both mirrored file paths required");
#ifdef BLR_HAVE_WINDOWS
      backend=std::make_unique<blr::WindowsBackend>(opt["--file-a"],opt["--file-b"],static_cast<unsigned>(cap));
#else
      backend=std::make_unique<blr::UringBackend>(opt["--file-a"],opt["--file-b"],cap);
#endif
#else
      throw std::runtime_error("real disk backend requires Windows or Linux/liburing; use --fake here");
#endif
    }
    blr::Logger logger; auto prefix=opt.count("--out-prefix")?opt["--out-prefix"]:"blr";
    if(!logger.open(prefix+".logical.csv",prefix+".replica.csv")) throw std::runtime_error("cannot open logs");
    if(!logger.open_activity(prefix+".activity.csv")) throw std::runtime_error("cannot open activity log");
    blr::Counters counters;
    blr::MirrorReader reader(*backend,*policy,counters,cap);
    reader.set_result_callback([&](const auto& r,bool ok){logger.log_logical(r,ok);});
    reader.set_replica_callback([&](auto id,const auto& r,bool win){logger.log_replica(id,r,win);});
    auto start=std::chrono::steady_clock::now(); uint64_t now=0; size_t next=0;
    uint64_t last_issued=UINT64_MAX, last_completed=UINT64_MAX;
    while(next<trace.size() || reader.pending()) {
      if(fake) { fake->set_time(now); } else now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
      reader.run_once(now);
      while(next<trace.size() && trace[next].arrival_ns<=now) {
        const auto& e=trace[next];
        if(!reader.submit(e.offset,e.length,now,e.arrival_ns)) break;
        ++next;
      }
      if(counters.replica_issued!=last_issued || counters.replica_completed!=last_completed) {
        logger.log_activity(now,counters);
        last_issued=counters.replica_issued; last_completed=counters.replica_completed;
      }
      if(fake) {
        now+=1000;
        if(!reader.pending() && next<trace.size()) now=std::max(now,trace[next].arrival_ns);
      } else std::this_thread::yield();
    }
    std::cout << "logical=" << counters.logical_submitted << " succeeded=" << counters.logical_succeeded
      << " failed=" << counters.logical_failed << " replicas=" << counters.replica_issued
      << " completed=" << counters.replica_completed << " duplicates=" << counters.duplicates_issued
      << " duplicate_wins=" << counters.duplicates_won << " replica_bytes=" << counters.replica_bytes << " peak_outstanding=" << counters.peak_outstanding
      << " peak_permits=" << counters.peak_pair_permits << " loser_cleanup_ns=" << counters.loser_cleanup_ns_total << '\n';
    return counters.logical_failed ? 2 : 0;
  } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
