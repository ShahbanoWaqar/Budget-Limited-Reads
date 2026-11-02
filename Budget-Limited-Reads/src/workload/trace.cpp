#include "workload/trace.h"
#include <fstream>
#include <sstream>
#include <limits>
namespace blr {
bool load_trace(const std::string& path, std::vector<TraceEntry>& out) {
  std::ifstream f(path); if (!f) return false;
  std::string line; if (!std::getline(f, line) || line != "arrival_ns,offset,length") return false;
  std::vector<TraceEntry> entries;
  while (std::getline(f, line)) {
    for (char c : line) if ((c < '0' || c > '9') && c != ',') return false;
    std::istringstream s(line); uint64_t t, o, l; char c, d;
    if (!(s >> t >> c >> o >> d >> l) || c != ',' || d != ',' || s.peek() != EOF ||
        l != 4096 || o % 4096 || o > INT64_MAX - l ||
        (!entries.empty() && t < entries.back().arrival_ns)) return false;
    entries.push_back({t, o, static_cast<uint32_t>(l)});
  }
  if (!f.eof()) return false;
  out = std::move(entries); return true;
}
bool save_trace(const std::string& path, const std::vector<TraceEntry>& trace) {
  std::ofstream f(path); if (!f) return false;
  f << "arrival_ns,offset,length\n";
  for (auto& e : trace) f << e.arrival_ns << ',' << e.offset << ',' << e.length << '\n';
  f.flush(); return static_cast<bool>(f);
}
}
