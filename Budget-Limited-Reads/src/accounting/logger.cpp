#include "accounting/logger.h"
namespace blr {
bool Logger::open(const std::string& a, const std::string& b) {
  logical_.open(a); replica_.open(b);
  if (!logical_ || !replica_) return false;
  logical_ << "logical_id,offset,length,arrival_ns,answer_ns,latency_ns,ok,winner,duplicated,holds_permit\n";
  replica_ << "logical_id,replica_id,drive,submit_ns,complete_ns,result,verified,winner\n";
  return true;
}
bool Logger::open_activity(const std::string& path) {
  activity_.open(path);
  activity_ << "time_ns,outstanding_a,outstanding_b,active_permits,replicas_issued,replicas_completed\n";
  return static_cast<bool>(activity_);
}
void Logger::log_activity(uint64_t now, const Counters& c) {
  activity_ << now << ',' << c.outstanding[0] << ',' << c.outstanding[1] << ','
    << c.active_pair_permits << ',' << c.replica_issued << ',' << c.replica_completed << '\n';
}
void Logger::log_logical(const LogicalRead& r, bool ok) {
  logical_ << r.id << ',' << r.offset << ',' << r.length << ',' << r.arrival_ns << ','
    << r.answer_ns << ',' << r.answer_ns-r.arrival_ns << ',' << ok << ',' << r.winner << ','
    << r.duplicate.has_value() << ',' << r.holds_permit << '\n';
  logical_.flush();
}
void Logger::log_replica(LogicalId id, const ReplicaRead& r, bool winner) {
  replica_ << id << ',' << r.id << ',' << static_cast<int>(r.drive) << ',' << r.submit_ns
    << ',' << r.complete_ns << ',' << r.result << ',' << r.verified << ',' << winner << '\n';
  replica_.flush();
}
}
