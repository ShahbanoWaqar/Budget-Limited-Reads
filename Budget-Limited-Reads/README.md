# Budget-Limited Duplicate Reads on Mirrored Disks

CSC 720 Storage Systems, Project 04. A read-only mirrored-file prototype comparing
round-robin, less-busy-first, fixed-delay hedging, and budget-limited hedging.
See [DESIGN.md](DESIGN.md) for request lifetime, hedging, and accounting details.

Windows 11 is supported natively, including real file reads. See
[WINDOWS.md](WINDOWS.md) for build instructions and PowerShell examples.
For the Windows machine with separate D: and E: HDDs, run
`run_windows_hdds.cmd` or `bin\blr_hdd.exe` when using a binary distribution.
It creates new test files on each HDD and runs all four modes automatically.

## Build and test

Requires a C++17 compiler and CMake 3.16+. Python 3 enables the CLI integration
test. Linux disk I/O additionally requires pkg-config and liburing development
headers (`sudo apt install build-essential cmake pkg-config liburing-dev`).
Windows uses native overlapped file I/O and does not need liburing.
macOS automatically builds the simulator only.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBLR_SANITIZERS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Use `-DBLR_ENABLE_URING=OFF` for simulator-only builds on Linux. Disable
`BLR_SANITIZERS` for performance runs. Tests use explicit checks even in Release
builds. Binaries: `blr`, `gen_trace`, `gen_data`, and `blr_tests`.

## Run a portable demonstration

```sh
./build/gen_trace 1048576 100 1 42 /tmp/blr-trace.csv
./build/blr --fake --mode budget --trace /tmp/blr-trace.csv \
  --delay-us 10 --permits 1 --threshold 8 --max-outstanding 16 \
  --out-prefix /tmp/blr-budget
```

Choose `--mode rr`, `lessbusy`, `fixed`, or `budget`. The simulator defaults to
200 microseconds on A and 50 on B; set `--fake-a-us` and `--fake-b-us` to change
these. Virtual time advances in 1 microsecond steps while requests are active.
The simulator generates the expected bytes; it does not read disk files or
provide disk-performance evidence.

## Real files and Linux io_uring

`gen_data` creates the deterministic bytes required by verification and refuses
to overwrite an existing file. Generate the same size on two separately mounted
physical drives using dedicated test-file paths:

```sh
./build/gen_data 1073741824 /mnt/driveA/blr-test.bin
./build/gen_data 1073741824 /mnt/driveB/blr-test.bin
./build/gen_trace 1073741824 100 10 42 /tmp/blr-disk-trace.csv
./build/blr --mode budget --trace /tmp/blr-disk-trace.csv \
  --file-a /mnt/driveA/blr-test.bin --file-b /mnt/driveB/blr-test.bin \
  --delay-us 1000 --permits 4 --threshold 16 --max-outstanding 128 \
  --out-prefix /tmp/blr-disk
```

The engine opens both files read-only with `O_DIRECT`, uses a normal io_uring
ring, and never cancels losing reads. Buffers, offsets, and lengths are aligned
to 4096 bytes. Files must remain immutable, contain the generator's known data,
and cover every offset in the trace. Arbitrary identical files will fail byte
verification. Two files on the same disk do not validate a two-drive experiment.

## Trace and logging contracts

Trace CSV has the exact header `arrival_ns,offset,length`. Entries must have
nondecreasing arrival times, aligned offsets, and length 4096. The generator
creates seeded uniform random offsets with evenly spaced arrivals; it is not a
Poisson arrival generator. All comparison modes can replay the same saved file.
When the physical cap is full, arrivals wait and are retried. Logical latency
includes this waiting time; hedge delay starts at actual primary submission.

Each run creates three CSVs:

- `PREFIX.logical.csv`: exactly one answer per accepted logical request,
  including scheduled arrival, answer, latency, success, winning ID, duplication,
  and whether the answer was returned while holding a pair permit.
- `PREFIX.replica.csv`: every completed replica, including losers, with IDs,
  drive (0=A, 1=B), observed submission/completion times, byte count or negative
  errno, verification outcome, and winner flag.
- `PREFIX.activity.csv`: event-loop snapshots when issued/completed totals change,
  with outstanding operations per drive, active permits, and operation totals.
  Snapshots can combine several events; they are not a per-kernel-event trace.

Completion times are when the polling loop observes completion. The console
reports final logical/replica counts, bytes, duplicate wins, peak outstanding
reads, peak pair permits, and total answer-to-loser-cleanup time. Exit codes:
0=all logical reads succeeded, 1=configuration/setup failure, 2=logical failures.
A replica failure includes an I/O error, short read, or content mismatch.
A denied-duplicate count records each request at most once for a policy denial;
physical-cap stalls are retried without incrementing this count.

## Tests and experiment scripts

The core test runner covers completion races, failed and corrupt reads, buffer
lifetime, permit exhaustion, submission failures, and shutdown. The Python CLI
test checks all four modes and reconciles generated CSV logs. Windows builds
also include native file-I/O tests and the HDD launcher's help test.

The Bash study scripts in `scripts/` remain unimplemented. The Windows HDD
launcher provides a single four-mode functional comparison. Repeated runs,
background interference, delay calibration, and plotting are separate tasks.
