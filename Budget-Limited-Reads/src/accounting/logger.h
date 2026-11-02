#pragma once

#include <fstream>
#include <string>

#include "mirror/request.h"
#include "accounting/counters.h"

namespace blr {

// CSV logs: one row per logical read and one row per replica read (losers
// included). Plots are generated from these files, not from live counters.
class Logger {
 public:
  bool open(const std::string& logical_path, const std::string& replica_path);

  bool open_activity(const std::string& path);
  void log_activity(uint64_t now_ns, const Counters& counters);
  void log_logical(const LogicalRead& read, bool ok);
  void log_replica(LogicalId logical, const ReplicaRead& replica, bool winner);

 private:
  std::ofstream activity_;
  std::ofstream logical_;
  std::ofstream replica_;
};

}  // namespace blr
