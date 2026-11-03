#!/usr/bin/env bash
# Study B: slow one drive, then both.
#   scenarios: slowA (load on A), slowB (roles reversed), slowBoth
#   modes: lessbusy (no duplicate), fixed, budget; REPS times.
# Rate is B_RATE (default LIGHT_RATE) because interference lowers capacity.
# Run scripts/calibrate_delay.sh first. Output: results/raw/studyB/
set -euo pipefail
source "$(dirname "$0")/common.sh"
need_ready; need_fio; load_candidates
STUDY=studyB; record_env "$OUT/$STUDY"
rate="${B_RATE:-$LIGHT_RATE}"
trace=$(make_trace "$rate" "$DUR" "$TRACE_SEED")
for rep in $(seq 1 "$REPS"); do
  for scenario in slowA slowB slowBoth; do
    bg_stop
    case $scenario in
      slowA) bg_start A;; slowB) bg_start B;; slowBoth) bg_start A; bg_start B;;
    esac
    nap "$WARMUP"
    run_blr $STUDY $scenario lessbusy "$rate" "$trace" "$rep"; nap "$COOLDOWN"
    run_blr $STUDY $scenario fixed    "$rate" "$trace" "$rep" "$DELAY_MID_US"; nap "$COOLDOWN"
    run_blr $STUDY $scenario budget   "$rate" "$trace" "$rep" "$DELAY_MID_US" "$PERMITS" "$THRESHOLD"; nap "$COOLDOWN"
  done
done
bg_stop
python3 "$ROOT/analysis/summarize.py" study "$OUT/$STUDY"
