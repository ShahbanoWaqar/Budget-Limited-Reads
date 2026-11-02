#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include "engine/windows_backend.h"
#include "mirror/mirror_reader.h"
#include "mirror/verify.h"
#include "policy/hedge_budget.h"
#include "policy/hedge_fixed.h"
#include "policy/selector.h"
using namespace blr;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while(0)
namespace {
struct TempDir {
  std::filesystem::path path = std::filesystem::temp_directory_path() /
    ("blr-windows-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TempDir() { CHECK(std::filesystem::create_directory(path)); }
  ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
void make_file(const std::filesystem::path& path, bool corrupt = false) {
  uint8_t bytes[8192]; fill_expected(0, sizeof(bytes), bytes);
  if (corrupt) { bytes[0] ^= 1; bytes[4096] ^= 1; }
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes)); out.close(); CHECK(out);
}
}
int main() {
  try {
    TempDir temp;
    auto a = temp.path / "a.bin", b = temp.path / "b.bin";
    make_file(a); make_file(b);
    for (int mode = 0; mode < 4; ++mode) {
      WindowsBackend backend(a.string(), b.string(), 4);
      Counters counters;
      std::unique_ptr<Policy> policy;
      if (mode == 0) policy = std::make_unique<NoDupRoundRobin>();
      if (mode == 1) policy = std::make_unique<NoDupLessBusy>();
      if (mode == 2) policy = std::make_unique<HedgeFixed>(0);
      if (mode == 3) policy = std::make_unique<HedgeBudget>(0, 1, 4);
      MirrorReader reader(backend, *policy, counters, 4);
      unsigned answers = 0; bool all_ok = true;
      reader.set_result_callback([&](const auto&, bool ok) { ++answers; all_ok = all_ok && ok; });
      auto start = std::chrono::steady_clock::now();
      unsigned submitted = 0;
      while (submitted < 64 || reader.pending()) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        CHECK(elapsed < std::chrono::seconds(30));
        auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
        reader.run_once(now);
        while (submitted < 64 && reader.submit((submitted % 2) * 4096, 4096, now)) ++submitted;
        std::this_thread::yield();
      }
      CHECK(answers == 64 && all_ok && counters.logical_succeeded == 64);
      CHECK(counters.replica_issued == counters.replica_completed);
      CHECK(counters.outstanding[0] == 0 && counters.outstanding[1] == 0);
      CHECK(counters.peak_outstanding <= 4 && counters.active_pair_permits == 0);
      CHECK(counters.peak_pair_permits <= 1);
    }
    // Exercise invalid content, EOF, and short reads through actual ReadFile calls.
    make_file(a, true); make_file(b, true);
    {
      WindowsBackend backend(a.string(), b.string(), 4);
      Counters counters; HedgeBudget policy(0, 1, 4);
      MirrorReader reader(backend, policy, counters, 4);
      CHECK(reader.submit(0, 4096, 0));
      CHECK(reader.submit(8192, 4096, 0));
      CHECK(reader.submit(4096, 8192, 0));
      reader.drain();
      CHECK(counters.logical_failed == 3 && counters.logical_succeeded == 0);
      CHECK(counters.replica_completed == 3 && counters.replica_failed == 3);
    }
    {
      WindowsBackend backend(a.string(), b.string(), 1);
      ReplicaRead first, second; first.allocate(4096); second.allocate(4096);
      CHECK(backend.submit({1, Drive::A, 0, 4096, first.buffer.get()}));
      CHECK(!backend.submit({2, Drive::B, 0, 4096, second.buffer.get()}));
      std::vector<ReadCompletion> completed;
      while (completed.empty()) { backend.poll(completed); std::this_thread::yield(); }
      CHECK(completed[0].result == 4096 && backend.outstanding(Drive::A) == 0);
    }
    // Constructor cleanup must also work when opening only the first file succeeds.
    bool rejected = false;
    try { WindowsBackend missing(a.string(), (temp.path / "missing.bin").string(), 1); }
    catch (const std::exception&) { rejected = true; }
    CHECK(rejected);
    std::cout << "Windows native reads: four modes, caps, corruption, EOF, short reads, cleanup passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
