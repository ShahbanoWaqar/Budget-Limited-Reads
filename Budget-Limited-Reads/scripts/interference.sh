#!/usr/bin/env bash
# Background random-read load (fio) on one or both drives, for Studies B and C.
# Usage: scripts/interference.sh start A|B|both | stop | status
# The load reads the same test file (read-only, O_DIRECT) with larger blocks and a deeper queue.
set -euo pipefail
source "$(dirname "$0")/common.sh"
trap - EXIT   # this script must leave the load running after it exits
mkdir -p "$OUT"
case "${1:-}" in
  start)
    need_ready
    case "${2:-}" in
      A) bg_start A;;
      B) bg_start B;;
      both) bg_start A; bg_start B;;
      *) die "usage: interference.sh start A|B|both";;
    esac
    echo "interference: $(bg_state)";;
  stop) bg_stop; echo "interference: none";;
  status) echo "interference: $(bg_state)";;
  *) die "usage: interference.sh start A|B|both | stop | status";;
esac
