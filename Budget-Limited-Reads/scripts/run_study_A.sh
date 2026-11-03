#!/usr/bin/env bash
# Study A: cost without interference.
#   each drive alone (modes a, b), the two no-duplicate baselines (rr, lessbusy),
#   fixed-delay hedging at three calibrated delays, and budget-limited hedging,
#   at a light and a moderate offered load, REPS times.
# Order is rep > rate > mode so slow drift does not favor one mode.
# Run scripts/calibrate_delay.sh first. Output: results/raw/studyA/
set -euo pipefail
source "$(dirname "$0")/common.sh"
need_ready; load_candidates; bg_stop
STUDY=studyA; record_env "$OUT/$STUDY"
for rep in $(seq 1 "$REPS"); do
  for rate in $LIGHT_RATE $MODERATE_RATE; do
    trace=$(make_trace "$rate" "$DUR" "$TRACE_SEED")
    run_blr $STUDY alone    a        "$rate" "$trace" "$rep"; nap "$COOLDOWN"
    run_blr $STUDY alone    b        "$rate" "$trace" "$rep"; nap "$COOLDOWN"
    run_blr $STUDY mirrored rr       "$rate" "$trace" "$rep"; nap "$COOLDOWN"
    run_blr $STUDY mirrored lessbusy "$rate" "$trace" "$rep"; nap "$COOLDOWN"
    for dl in $DELAY_LOW_US $DELAY_MID_US $DELAY_HIGH_US; do
      run_blr $STUDY mirrored fixed  "$rate" "$trace" "$rep" "$dl"; nap "$COOLDOWN"
    done
    run_blr $STUDY mirrored budget   "$rate" "$trace" "$rep" "$DELAY_MID_US" "$PERMITS" "$THRESHOLD"; nap "$COOLDOWN"
  done
done
python3 "$ROOT/analysis/summarize.py" study "$OUT/$STUDY"
