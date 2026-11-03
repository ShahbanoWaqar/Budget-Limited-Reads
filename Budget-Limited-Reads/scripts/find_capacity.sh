#!/usr/bin/env bash
# Sweep offered load with the less-busy baseline to see where latency stops being flat.
# Use the output to choose LIGHT_RATE and MODERATE_RATE for the studies.
# Usage: scripts/find_capacity.sh        (SWEEP_RATES="25 50 100 150 200", SWEEP_DUR=15)
set -euo pipefail
source "$(dirname "$0")/common.sh"
need_ready
bg_stop
SWEEP_RATES="${SWEEP_RATES:-25 50 75 100 150 200 300}"
SWEEP_DUR="${SWEEP_DUR:-15}"
D="$OUT/capacity"; mkdir -p "$D"; record_env "$D"
printf '%-8s %-10s %-10s %-10s %-12s\n' rate n p50_ms p99_ms achieved/s
for rate in $SWEEP_RATES; do
  trace=$(make_trace "$rate" "$SWEEP_DUR" "$CAL_SEED")
  run_blr capacity none lessbusy "$rate" "$trace" 1 > /dev/null
  prefix="$D/none_lessbusy_r${rate}_d-_p-_t-_rep1"
  read -r n p50 p90 p95 p99 thr <<<"$(python3 "$ROOT/analysis/summarize.py" percentiles "$prefix")"
  printf '%-8s %-10s %-10s %-10s %-12s\n' "$rate" "$n" "$(awk "BEGIN{printf \"%.2f\", $p50/1000}")" "$(awk "BEGIN{printf \"%.2f\", $p99/1000}")" "$thr"
done
echo "Pick LIGHT_RATE where p99 is still close to p50, and MODERATE_RATE just before p99 climbs sharply."
