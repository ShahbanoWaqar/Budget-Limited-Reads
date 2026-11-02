# Budget-Limited Duplicate Reads on Mirrored Disks

CSC 720 Storage Systems, Project 04. A read-only mirrored-file prototype that
compares hedged (duplicate) reads against no-duplicate baselines, with a bounded
pool of hedge-pair permits.

Research question: can a limit on simultaneous hedged request pairs preserve
useful latency reductions while avoiding excessive duplicate I/O?

## Status

| Component | State |
|-----------|-------|
| Verification (`src/mirror/verify.*`, `pattern.h`) | Done. Expected content is an offset-derived pattern, so any range can be checked without a checksum table. |
| Mirror reader, no duplicates (`src/mirror/mirror_reader.*`) | Done. Primary read, verify, answer once, drain, free buffers. Reads the backend rejects are queued, not dropped. |
| Fake backend (`src/engine/fake_backend.*`) | Done. Simulated time, per-drive latency, injected failures and corruption, outstanding cap. |
| Hedge path in the reader (calls `Policy::try_hedge`, sets `holds_permit`) | Not started. Tests that need duplicate reads are placeholders in `tests/test_main.cpp`. |
| `io_uring` backend, CLI (`blr`), scripts, analysis | Stubs. |

Test files must use the pattern in `src/mirror/pattern.h` so the verifier accepts
them; the planned `make_testfiles.sh` / generator must call the same code.

## Dependencies

| Need | Version | Where |
|------|---------|-------|
| C++ compiler (C++17) | g++ 9+ or clang 10+ | all platforms |
| CMake | 3.16+ | all platforms |
| make or ninja | any recent | all platforms |
| pkg-config | any | Linux (locates liburing) |
| liburing (`liburing-dev`) | 2.x recommended | Linux only |
| Linux kernel | 5.6+ recommended (`io_uring` read opcodes; 5.1 minimum) | Linux only |
| Bash | 4+ | scripts |
| Python 3 + matplotlib, pandas | optional | plotting in `analysis/` |

Testing framework: none. `tests/test_main.cpp` is a small self-contained
assert-based runner, registered with CTest. No external test dependency.

Hardware for the real experiments: two separate physical drives (HDDs
preferred) holding identical, immutable test files. Two files on one physical
drive are not a valid substitute.

### Install dependencies

Debian/Ubuntu:

    sudo apt install build-essential cmake pkg-config liburing-dev

Fedora/RHEL:

    sudo dnf install gcc-c++ cmake make pkgconf-pkg-config liburing-devel

macOS (fake backend and unit tests only; `io_uring` does not exist on macOS):

    xcode-select --install
    brew install cmake

## Build

Linux (full build, includes the `io_uring` backend):

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ctest --test-dir build --output-on-failure

macOS (the `io_uring` backend is skipped automatically):

    cmake -S . -B build
    cmake --build build -j
    ctest --test-dir build --output-on-failure

Binaries land in `build/`: `blr` (the engine), `gen_trace` (trace generator)
and `blr_tests`.

## Running the experiments (outline, Linux only)

1. Mount the two drives and create identical test files: `scripts/make_testfiles.sh`.
2. Generate arrival traces with `build/gen_trace`.
3. Calibrate candidate hedge delays: `scripts/calibrate_delay.sh`.
4. Run the studies: `scripts/run_study_A.sh`, `run_study_B.sh`, `run_study_C.sh`
   (at least 3 repetitions of each principal comparison).
5. Plot from the logs in `results/` using the scripts in `analysis/`.

The scripts and the CLI are stubs for now.

## Layout

| Path | Contents |
|------|----------|
| `src/engine/` | `IoBackend` interface, `io_uring` backend (Linux), fake backend (tests) |
| `src/mirror/` | Logical/replica request state machine, mirrored reader, data verification |
| `src/policy/` | Primary selectors, no-duplicate, fixed-delay and budget-limited policies |
| `src/accounting/` | Logical vs. replica counters, CSV logging |
| `src/workload/` | Arrival-trace loading and generation |
| `tests/` | Buffer lifetime, completion race, failure, full pool and shutdown tests |
| `scripts/` | Test-file creation, interference, delay calibration, studies A/B/C |
| `analysis/`, `results/`, `report/` | Plots, raw logs, IEEE/ACM report |

## Modes

1. No duplicate, round-robin
2. No duplicate, less-busy first
3. Fixed-delay duplication (same total physical-I/O cap)
4. Budget-limited duplication (pair permits + alternate-drive threshold)

## Safety rules

- Use only approved test files; never touch a live mirror.
- Never free or reuse a buffer while its read may still write into it.
- Losing reads are not cancelled; they are drained and counted.
