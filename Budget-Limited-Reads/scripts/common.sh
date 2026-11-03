#!/usr/bin/env bash
# Shared helpers for the study scripts. Source this file, do not run it.
# Everything is configured with environment variables (defaults in brackets).
#
#   DIR_A, DIR_B      mount points of the two physical drives (required for real runs)
#   FILE_NAME         test file name on each drive            [blr-test.bin]
#   SIZE_BYTES        size of each test file                  [8589934592 = 8 GiB]
#   BIN               directory with blr, gen_trace, gen_data [<repo>/build]
#   OUT               raw result directory                    [<repo>/results/raw]
#   CAP               total outstanding physical reads        [32]
#   DUR               seconds per trace                       [60]
#   REPS              repetitions of each comparison          [3]
#   LIGHT_RATE        light load, reads per second            [40]
#   MODERATE_RATE     moderate load, reads per second         [100]
#   PERMITS           pair permits for budget mode            [2]
#   THRESHOLD         alternate-drive threshold               [8]
#   CPUSET            e.g. "0-3", pins blr with taskset       [unset]
#   WARMUP            seconds to let interference settle      [5]
#   BG_BS, BG_QD      interference block size, queue depth    [64k, 4]
#   BG_ENGINE         fio engine for interference             [libaio]
#   BLR_FAKE=1        dry run on the simulator (no drives, no fio)
#   ALLOW_SAME_DEVICE=1  skip the "two different drives" check (testing only)

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$ROOT/build}"
OUT="${OUT:-$ROOT/results/raw}"
DIR_A="${DIR_A:-}"
DIR_B="${DIR_B:-}"
FILE_NAME="${FILE_NAME:-blr-test.bin}"
SIZE_BYTES="${SIZE_BYTES:-8589934592}"
CAP="${CAP:-32}"
DUR="${DUR:-60}"
REPS="${REPS:-3}"
LIGHT_RATE="${LIGHT_RATE:-40}"
MODERATE_RATE="${MODERATE_RATE:-100}"
PERMITS="${PERMITS:-2}"
THRESHOLD="${THRESHOLD:-8}"
CPUSET="${CPUSET:-}"
WARMUP="${WARMUP:-5}"
COOLDOWN="${COOLDOWN:-2}"
BG_BS="${BG_BS:-64k}"
BG_QD="${BG_QD:-4}"
BG_ENGINE="${BG_ENGINE:-libaio}"
BG_SECS="${BG_SECS:-86400}"
BLR_FAKE="${BLR_FAKE:-0}"
FAKE_FAST_US="${FAKE_FAST_US:-500}"
FAKE_SLOW_US="${FAKE_SLOW_US:-8000}"
TRACE_SEED="${TRACE_SEED:-42}"
CAL_SEED="${CAL_SEED:-1001}"
CPUPREFIX=""
[[ -n "$CPUSET" ]] && CPUPREFIX="taskset -c $CPUSET"

die() { echo "error: $*" >&2; exit 1; }
nap() { [[ "$BLR_FAKE" == 1 ]] || sleep "$1"; }

file_of() { if [[ "$1" == A ]]; then echo "$DIR_A/$FILE_NAME"; else echo "$DIR_B/$FILE_NAME"; fi; }

parent_disk() {
  local src
  src=$(findmnt -no SOURCE -T "$1" 2>/dev/null || true)
  [[ -n "$src" ]] || { echo unknown; return; }
  local p; p=$(lsblk -no pkname "$src" 2>/dev/null | head -1 || true)
  echo "${p:-$src}"
}

# Checks that binaries exist and, for real runs, that both test files exist on different drives.
need_ready() {
  [[ -x "$BIN/blr" && -x "$BIN/gen_trace" ]] || die "build first: cmake -S . -B build && cmake --build build -j (BIN=$BIN)"
  mkdir -p "$OUT"
  [[ "$BLR_FAKE" == 1 ]] && return 0
  [[ -n "$DIR_A" && -n "$DIR_B" ]] || die "set DIR_A and DIR_B to the two drives' mount points (or BLR_FAKE=1)"
  [[ -f "$(file_of A)" && -f "$(file_of B)" ]] || die "test files missing; run scripts/make_testfiles.sh first"
  if [[ "${ALLOW_SAME_DEVICE:-0}" != 1 ]]; then
    [[ "$(stat -c %d "$DIR_A")" != "$(stat -c %d "$DIR_B")" ]] || die "DIR_A and DIR_B are on the same filesystem; two separate drives are required"
    local pa pb; pa=$(parent_disk "$DIR_A"); pb=$(parent_disk "$DIR_B")
    if [[ "$pa" != unknown && "$pa" == "$pb" ]]; then die "DIR_A and DIR_B sit on the same physical disk ($pa)"; fi
    if [[ "$pa" == unknown || "$pb" == unknown ]]; then echo "warning: could not identify the physical disk behind DIR_A/DIR_B; check with lsblk" >&2; fi
  fi
}

need_fio() { [[ "$BLR_FAKE" == 1 ]] || command -v fio >/dev/null || die "fio is needed for interference (sudo apt install fio)"; }

record_env() {
  mkdir -p "$1"
  {
    echo "date: $(date -Is)"
    echo "uname: $(uname -a)"
    echo "git: $(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "liburing: $(pkg-config --modversion liburing 2>/dev/null || echo unknown)"
    echo "fake: $BLR_FAKE"
    echo "dir_a: $DIR_A"; echo "dir_b: $DIR_B"
    echo "settings: CAP=$CAP DUR=$DUR REPS=$REPS LIGHT_RATE=$LIGHT_RATE MODERATE_RATE=$MODERATE_RATE PERMITS=$PERMITS THRESHOLD=$THRESHOLD SIZE_BYTES=$SIZE_BYTES CPUSET=${CPUSET:-none} BG_BS=$BG_BS BG_QD=$BG_QD"
    echo "--- drives (ROTA=1 means spinning)"; lsblk -d -o NAME,ROTA,MODEL,SIZE,TRAN 2>/dev/null || true
    for d in "$DIR_A" "$DIR_B"; do
      [[ -n "$d" ]] || continue
      echo "--- $d"; df -h "$d" 2>/dev/null | tail -1; findmnt -T "$d" -no SOURCE,FSTYPE 2>/dev/null || true
    done
    echo "--- io schedulers"
    for f in /sys/block/sd*/queue/scheduler /sys/block/nvme*/queue/scheduler; do [[ -e "$f" ]] && echo "$f: $(cat "$f")"; done
  } > "$1/env.txt" 2>&1 || true
}

# ---------- traces ----------
# make_trace RATE DURATION SEED -> prints the trace path (creates it if needed)
make_trace() {
  local rate=$1 dur=$2 seed=$3
  mkdir -p "$OUT/traces"
  local t="$OUT/traces/trace_${SIZE_BYTES}_${rate}x${dur}_s${seed}.csv"
  [[ -f "$t" ]] || "$BIN/gen_trace" "$SIZE_BYTES" "$rate" "$dur" "$seed" "$t"
  echo "$t"
}

# ---------- background interference ----------
bg_pidfile() { echo "$OUT/.bg_$1.pid"; }
bg_active() {
  local f; f=$(bg_pidfile "$1")
  [[ -f "$f" ]] || return 1
  [[ "$BLR_FAKE" == 1 ]] && return 0
  kill -0 "$(cat "$f")" 2>/dev/null
}
bg_start() {
  local d=$1 f; f=$(bg_pidfile "$d"); mkdir -p "$OUT"
  bg_active "$d" && return 0
  if [[ "$BLR_FAKE" == 1 ]]; then echo fake > "$f"; return 0; fi
  need_fio
  nohup fio --name="bg_$d" --filename="$(file_of "$d")" --readonly --rw=randread \
      --bs="$BG_BS" --iodepth="$BG_QD" --ioengine="$BG_ENGINE" --direct=1 \
      --time_based --runtime="$BG_SECS" >/dev/null 2>&1 &
  echo $! > "$f"
}
bg_stop() {
  local d f pid
  for d in A B; do
    f=$(bg_pidfile "$d")
    [[ -f "$f" ]] || continue
    if [[ "$BLR_FAKE" != 1 ]]; then
      pid=$(cat "$f")
      pkill -P "$pid" 2>/dev/null || true
      kill "$pid" 2>/dev/null || true
    fi
    rm -f "$f"
  done
}
bg_state() {
  local a=0 b=0
  bg_active A && a=1; bg_active B && b=1
  if (( a && b )); then echo both; elif (( a )); then echo A; elif (( b )); then echo B; else echo none; fi
}
fake_latencies() {
  local a=$FAKE_FAST_US b=$FAKE_FAST_US
  case "$(bg_state)" in
    A) a=$FAKE_SLOW_US;; B) b=$FAKE_SLOW_US;; both) a=$FAKE_SLOW_US; b=$FAKE_SLOW_US;;
  esac
  echo "$a $b"
}

# ---------- running blr ----------
# launch_blr STUDY SCENARIO MODE RATE TRACE REP [DELAY_US PERMITS THRESHOLD|off]
# Starts blr in the background. Use finish_blr to wait and record it, or run_blr for both.
launch_blr() {
  local study=$1 scenario=$2 mode=$3 rate=$4 trace=$5 rep=$6 delay=${7:--} permits=${8:--} thr=${9:--}
  local dir="$OUT/$study"; mkdir -p "$dir"
  LAST_STUDY=$study
  LAST_PREFIX="$dir/${scenario}_${mode}_r${rate}_d${delay}_p${permits}_t${thr}_rep${rep}"
  LAST_ROW="$study,$scenario,$mode,$rate,$delay,$permits,$thr,$rep,$LAST_PREFIX"
  local args=(--mode "$mode" --trace "$trace" --max-outstanding "$CAP" --out-prefix "$LAST_PREFIX")
  [[ "$delay" != - ]] && args+=(--delay-us "$delay")
  [[ "$permits" != - ]] && args+=(--permits "$permits")
  if [[ "$thr" == off ]]; then args+=(--no-threshold); elif [[ "$thr" != - ]]; then args+=(--threshold "$thr"); fi
  if [[ "$BLR_FAKE" == 1 ]]; then
    local fa fb; read -r fa fb <<<"$(fake_latencies)"
    args+=(--fake --fake-a-us "$fa" --fake-b-us "$fb")
  else
    args+=(--file-a "$(file_of A)" --file-b "$(file_of B)")
  fi
  echo "[$(date +%T)] $study/$scenario mode=$mode rate=$rate delay=$delay permits=$permits thr=$thr rep=$rep"
  date +%s%N > "$LAST_PREFIX.start_ns"
  # shellcheck disable=SC2086
  $CPUPREFIX "$BIN/blr" "${args[@]}" > "$LAST_PREFIX.console.txt" 2>&1 &
  BLR_PID=$!
}
finish_blr() {
  local rc=0
  wait "$BLR_PID" || rc=$?
  if (( rc == 1 )); then cat "$LAST_PREFIX.console.txt" >&2; die "blr failed to start (see message above)"; fi
  if (( rc == 2 )); then echo "  warning: some logical reads failed, see $LAST_PREFIX.console.txt" >&2; fi
  local m="$OUT/$LAST_STUDY/manifest.csv"
  [[ -f "$m" ]] || echo "study,scenario,mode,rate,delay_us,permits,threshold,rep,prefix" > "$m"
  echo "$LAST_ROW" >> "$m"
  sed 's/^/  /' "$LAST_PREFIX.console.txt" | tail -1
}
run_blr() { launch_blr "$@"; finish_blr; }

# Hedge delays: values exported in the environment win over calibration/candidates.env.
load_candidates() {
  local f="$OUT/calibration/candidates.env"
  local el=${DELAY_LOW_US:-} em=${DELAY_MID_US:-} eh=${DELAY_HIGH_US:-}
  # shellcheck disable=SC1090
  [[ -f "$f" ]] && source "$f"
  DELAY_LOW_US="${el:-${DELAY_LOW_US:-}}"; DELAY_MID_US="${em:-${DELAY_MID_US:-}}"; DELAY_HIGH_US="${eh:-${DELAY_HIGH_US:-}}"
  if [[ -z "$DELAY_LOW_US" || -z "$DELAY_MID_US" || -z "$DELAY_HIGH_US" ]]; then
    die "no hedge delays yet: run scripts/calibrate_delay.sh first (or export DELAY_LOW_US, DELAY_MID_US, DELAY_HIGH_US)"
  fi
}

# Always clean up interference when a script exits.
trap 'bg_stop' EXIT
