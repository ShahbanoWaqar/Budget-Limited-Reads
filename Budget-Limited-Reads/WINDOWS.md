# Windows 11

Windows builds use native asynchronous file I/O and support all four policies,
the simulator, data and trace generation, and CSV logging.

## Build and test

Install Visual Studio Build Tools with Desktop development with C++, the Windows
SDK, and CMake tools. Python 3 enables the optional CLI integration test. In a
Developer PowerShell for Visual Studio, run:

```powershell
.\build_windows.cmd
```

Equivalent commands:

```powershell
cmake -S . -B build-windows -DBLR_SANITIZERS=OFF
cmake --build build-windows --config Release --parallel
ctest --test-dir build-windows -C Release --output-on-failure
```

Visual Studio puts executables in `build-windows\Release`; Ninja uses
`build-windows`. The scripts also look in `bin` for prebuilt executables.
ASan+UBSan is supported with GCC/Clang, not through the MSVC build option.

## D: and E: HDD run

Run `run_windows_hdds.cmd` or `blr_hdd.exe`. The launcher expects D: and E: to
be separate physical HDDs. It checks drive accessibility and free space; it does
not query physical disk identity.

Each run creates new files at:

```text
D:\BudgetLimitedReads\<run-id>\mirror.bin
E:\BudgetLimitedReads\<run-id>\mirror.bin
```

Existing files are not overwritten. The files remain after the run and can be
removed manually when no longer needed. Defaults:

| Setting | Value |
|---|---:|
| File size per HDD | 64 MiB |
| Request size | 4 KiB |
| Arrival rate | 100 reads/s |
| Trace duration per mode | 10 seconds |
| Hedge delay | 5 ms |
| Pair permits | 2 |
| Alternate-drive threshold | 8 |
| Total outstanding cap | 32 |
| Trace seed | 42 |

The launcher replays the same trace for round-robin, less-busy-first,
fixed-delay, and budget-limited modes. Queued reads may extend runtime beyond
the scheduled trace duration. These defaults are a functional starting point;
performance studies require delay calibration and repeated runs.

Results are saved under:

```text
%LOCALAPPDATA%\BudgetLimitedReads\results\<run-id>
```

Each result directory contains `run-summary.txt`, `trace.csv`, file-generation
logs, and logical, replica, activity, and console logs for each mode. Setup
failures stop the run. A comparison failure is recorded and the remaining
modes are attempted.

Custom settings:

```powershell
.\build-windows\Release\blr_hdd.exe --size-mib 256 --rate 100 --duration 30 --delay-us 5000 --permits 2 --threshold 8 --max-outstanding 32 --no-pause
```

`--output-dir "C:\BLR-Results"` changes the result parent directory. Keep logs
on a separate drive from D: and E: when measuring disk performance.
`--no-pause` prevents the final console prompt in automated runs.

## Simulator and individual commands

`run_windows_demo.cmd` runs all four modes against simulated drives.
For individual commands, set the executable directory:

```powershell
$bin = ".\build-windows\Release"
& "$bin\gen_trace.exe" 1048576 100 1 42 "$env:TEMP\blr-trace.csv"
& "$bin\blr.exe" --fake --mode budget --trace "$env:TEMP\blr-trace.csv" --delay-us 10 --permits 1 --threshold 8 --max-outstanding 16 --out-prefix "$env:TEMP\blr-budget"
```

Use `$bin = ".\bin"` for a binary distribution. To read existing generated
files, omit `--fake` and pass `--file-a` and `--file-b`. Files must contain the
pattern produced by `gen_data.exe`, remain unchanged during the run, and cover
every requested offset.

## File I/O and tests

The backend opens files with FILE_FLAG_OVERLAPPED and FILE_FLAG_NO_BUFFERING.
Each operation owns a stable OVERLAPPED structure, event, and aligned buffer.
GetOverlappedResult reports completion; losing reads are drained without
cancellation. Windows errors are mapped to negative errno values; EOF is zero.

Unbuffered reads require compatible sector alignment. Requests and buffers are
aligned to 4096 bytes; use local storage supporting that alignment. There is no
fallback to cached reads. Command-line paths should use characters supported by
the active Windows code page.

`blr_tests.exe` checks shared request and policy behavior.
`blr_windows_tests.exe` checks native reads, all four modes, operation caps,
corruption, EOF, short reads, and resource cleanup using temporary files.
CTest also checks launcher help and, when Python is available, CLI log
reconciliation. The existing Bash study scripts are not Windows tools.

References: [ReadFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile),
[GetOverlappedResult](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresult),
[file buffering and alignment](https://learn.microsoft.com/en-us/windows/win32/fileio/file-buffering).
