#include "mirror/mirror_reader.h"

namespace blr {

MirrorReader::MirrorReader(IoBackend& backend, Policy& policy, Counters& counters)
    : backend_(backend), policy_(policy), counters_(counters) {}

void MirrorReader::submit(uint64_t, size_t, uint64_t) {
  // TODO
}

void MirrorReader::run_once(uint64_t) {
  // TODO
}

void MirrorReader::drain() {
  // TODO
}

}  // namespace blr
