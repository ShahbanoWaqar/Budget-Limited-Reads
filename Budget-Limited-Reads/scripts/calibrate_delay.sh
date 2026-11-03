#!/usr/bin/env bash
# Choose three candidate hedge delays from a no-duplicate baseline.
# Uses a separate calibration trace (seed CAL_SEED), never the final test trace.
# Candidates: p50 (aggressive), p90 (middle), p95 (conservative) of baseline latency.
# Output: results/raw/calibration/candidates.env, read by the study scripts.
set -euo pipefail
source "$(dirname "$0")/common.sh"
need_ready
bg_stop
STUDY=calibration; D="$OUT/$STUDY"; mkdir -p "$D"; record_env "$D"
rate="${CAL_RATE:-$LIGHT_RATE}"; dur="${CAL_DUR:-$DUR}"
trace=$(make_trace "$rate" "$dur" "$CAL_SEED")

echo "== baseline: less-busy, no duplicates, rate=$rate"
run_blr $STUDY none lessbusy "$rate" "$trace" 1
base="$D/none_lessbusy_r${rate}_d-_p-_t-_rep1"
read -r n p50 p90 p95 p99 thr <<<"$(python3 "$ROOT/analysis/summarize.py" percentiles "$base")"
echo "baseline latency (us): p50=$p50 p90=$p90 p95=$p95 p99=$p99 (n=$n)"

# keep the three delays distinct and increasing (at least 10% apart)
low=$(( p50 < 100 ? 100 : p50 ))
mid=$(( p90 > low * 11 / 10 ? p90 : low * 11 / 10 ))
high=$(( p95 > mid * 11 / 10 ? p95 : mid * 11 / 10 ))
cat > "$D/candidates.env" <<EOT
DELAY_LOW_US=$low
DELAY_MID_US=$mid
DELAY_HIGH_US=$high
EOT
echo "candidates (us): low=$low mid=$mid high=$high"

echo "== fixed-delay hedging at each candidate (same calibration trace)"
for dl in $low $mid $high; do run_blr $STUDY none fixed "$rate" "$trace" 1 "$dl"; done
python3 "$ROOT/analysis/summarize.py" study "$D" || true
echo "Wrote $D/candidates.env. Review the summary above before running the studies."
