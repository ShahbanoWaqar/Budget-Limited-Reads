#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace blr {

// One logical read in an arrival trace. All modes replay identical traces.
struct TraceEntry {
  uint64_t arrival_ns;
  uint64_t offset;
  uint32_t length;
};

// CSV: arrival_ns,offset,length
// Returns false on malformed input; leaves output unchanged on failure.
bool load_trace(const std::string& path, std::vector<TraceEntry>& out);
bool save_trace(const std::string& path, const std::vector<TraceEntry>& trace);

}  // namespace blr
