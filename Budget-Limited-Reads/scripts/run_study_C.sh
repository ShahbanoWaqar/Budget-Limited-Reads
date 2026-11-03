#!/usr/bin/env bash
# Study C: interference changes mid-run (load on A, then B joins at DUR/2).
#   compares lessbusy, fixed, and budget with each permit count in PERMIT_SET,
#   with the alternate-drive threshold on and with it removed (--no-threshold).
# Rate is C_RATE (default LIGHT_RATE). Output: results/raw/studyC/
# Note: with BLR_FAKE=1 the simulator cannot change latency mid-run, so only the
# first-half condition (load on A) is modeled. Real runs do change it.
set -euo pipefail
source "$(dirname "$0")/common.sh"
need_ready; need_fio; load_candidates
STUDY=studyC; record_env "$OUT/$STUDY"
rate="${C_RATE:-$LIGHT_RATE}"
PERMIT_SET="${PERMIT_SET:-1 2 4}"
trace=$(make_trace "$rate" "$DUR" "$TRACE_SEED")
half=$(( DUR / 2 ))

one_run() {  # MODE REP [DELAY PERMITS THRESHOLD]
  local mode=$1 rep=$2; shift 2
  bg_stop; bg_start A; nap "$WARMUP"
  launch_blr $STUDY changing "$mode" "$rate" "$trace" "$rep" "$@"
  nap "$half"
  bg_start B
  date +%s%N > "$LAST_PREFIX.bgB_ns"
  finish_blr
  bg_stop; nap "$COOLDOWN"
}

for rep in $(seq 1 "$REPS"); do
  one_run lessbusy "$rep"
  one_run fixed "$rep" "$DELAY_MID_US"
  for p in $PERMIT_SET; do
    one_run budget "$rep" "$DELAY_MID_US" "$p" "$THRESHOLD"
    one_run budget "$rep" "$DELAY_MID_US" "$p" off
  done
done
python3 "$ROOT/analysis/summarize.py" study "$OUT/$STUDY"
