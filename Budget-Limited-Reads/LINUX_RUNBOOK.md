# Linux runbook (Studies A, B, C)

These scripts run the project on Linux with the io_uring backend. They were
dry-run on the simulator (`BLR_FAKE=1`) and on local files, so the plumbing is
tested. Real HDD numbers still have to come from the real machine.

## One-time setup

    sudo apt install build-essential cmake pkg-config liburing-dev fio python3-pandas python3-matplotlib
    git apply patches/single-drive-modes.patch      # adds --mode a and --mode b (each drive alone)
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBLR_SANITIZERS=OFF
    cmake --build build -j && ctest --test-dir build --output-on-failure

## Run order

    export DIR_A=/mnt/diskA DIR_B=/mnt/diskB      # two separate physical drives
    scripts/make_testfiles.sh                     # identical file on each drive (default 8 GiB)
    scripts/find_capacity.sh                      # pick LIGHT_RATE and MODERATE_RATE from the table
    export LIGHT_RATE=40 MODERATE_RATE=100
    scripts/calibrate_delay.sh                    # writes results/raw/calibration/candidates.env
    scripts/run_study_A.sh
    scripts/run_study_B.sh
    scripts/run_study_C.sh
    python3 analysis/plot_studies.py results/raw/studyA   # also studyB, studyC

Each study prints its summary table and writes `per_run.csv` and `summary.csv`
(mean and standard deviation over REPS repetitions) in `results/raw/<study>/`.
Every run keeps its own logical, replica and activity CSVs. `env.txt` in each
study folder records the drives, kernel, liburing version and settings.

## Extra figures

    python3 analysis/plot_timeseries.py results/raw/studyC/<run prefix>   # permits, queues, p99 over time
    python3 analysis/plot_pairs.py results/raw/studyB/<run prefix>        # answer returned, loser still running

## Settings you may change (environment variables)

DUR (seconds per trace, 60), REPS (3), CAP (32), PERMITS (2), THRESHOLD (8),
PERMIT_SET (Study C, "1 2 4"), B_RATE and C_RATE, CPUSET (e.g. "0-3", pins blr),
BG_BS and BG_QD (interference block size and queue depth), WARMUP, SIZE_BYTES.
Hedge delays can be forced with DELAY_LOW_US, DELAY_MID_US, DELAY_HIGH_US.

## Dry run without drives

    BLR_FAKE=1 DUR=4 REPS=2 scripts/calibrate_delay.sh
    BLR_FAKE=1 DUR=4 REPS=2 DELAY_LOW_US=100 DELAY_MID_US=300 DELAY_HIGH_US=450 scripts/run_study_A.sh

## Things to know

- Calibration uses its own trace (seed 1001); the studies use seed 42, the same trace for every mode and repetition.
- Interference is fio reading the same test file (read-only, O_DIRECT, 64k, queue depth 4). Describe it in the report.
- Check `env.txt` and `lsblk -d -o NAME,ROTA,MODEL,SIZE,TRAN`: ROTA=1 means spinning, and the report must say whether the two drives share a controller.
- The scripts refuse to run if DIR_A and DIR_B are on the same filesystem or the same physical disk.
- Study C changes interference at DUR/2 using wall-clock sleep, so the switch point is approximate. The time of the switch is saved next to each run.
- Not covered: the second request size sensitivity check, if your handout version asks for one.
