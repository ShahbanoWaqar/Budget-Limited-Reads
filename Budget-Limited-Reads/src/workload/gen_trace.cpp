#include <stdexcept>
#include <string>
#include <iostream>
#include <random>
#include "workload/trace.h"
int main(int argc, char** argv) {
  try {
    if (argc != 6) throw std::runtime_error("usage: gen_trace FILE_BYTES RATE DURATION_SECONDS SEED OUT.csv");
    for (int i=1; i<=4; ++i) {
      std::string value=argv[i];
      if(value.empty() || value.find_first_not_of("0123456789")!=std::string::npos)
        throw std::runtime_error("numeric arguments must be unsigned decimal integers");
    }
    uint64_t size = std::stoull(argv[1]), rate = std::stoull(argv[2]), duration = std::stoull(argv[3]);
    if (size < 4096 || size > INT64_MAX || !duration || !rate || rate > 1000000000 || duration > 10000000 || rate*duration > 10000000)
      throw std::runtime_error("invalid size/rate/duration (maximum 10 million requests)");
    std::mt19937_64 rng(std::stoull(argv[4]));
    std::uniform_int_distribution<uint64_t> block(0, size/4096-1);
    std::vector<blr::TraceEntry> trace;
    for (uint64_t i=0; i<rate*duration; ++i) trace.push_back({i*(1000000000/rate), block(rng)*4096, 4096});
    if (!blr::save_trace(argv[5], trace)) throw std::runtime_error("cannot write trace");
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
