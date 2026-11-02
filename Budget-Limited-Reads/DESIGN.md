# Request lifecycle and accounting

## Mirrored reads

Each logical request has a unique ID, offset, length, and scheduled arrival time.
Primary and duplicate operations have separate replica IDs and 4096-byte-aligned
buffers. Both operations read the same offset and length from their respective
files. Known data is generated from each absolute byte offset using a
SplitMix64-derived pattern.

The first full-length, byte-verified result answers the logical request exactly
once. Errors, short reads, and content mismatches are failed replicas. If every
issued replica fails, the logical request fails. A primary failure before a
hedge is issued does not trigger an additional retry.

Buffers remain owned until all issued operations finish. Losing reads are not
cancelled. Shutdown drains accepted reads without adding new duplicates. The
backend, policy, and counters must outlive the reader; callbacks must not throw
or reenter the reader.

## Hedging

The event loop processes ready completions before checking hedge timers. An
unfinished, unanswered primary becomes eligible after the configured delay.
Only one duplicate can be submitted, always to the alternate drive. A completion
between the last poll and submission can still produce two completed replicas;
the result is delivered only once.

Both hedging modes use less-busy-first primary selection with alternating ties.
Budget mode additionally reserves a pair permit and requires alternate-drive
outstanding operations to be strictly below the threshold. A permit remains
held until both replicas complete. Submission rejection or setup failure rolls
back an unused reservation. `--no-threshold` disables only the load check.

All modes share the total physical-operation cap. Full-cap arrivals wait for
admission; logical latency includes this waiting time, while the hedge delay
starts at primary submission.

## Backends and logs

Linux uses read-only O_DIRECT files and io_uring. Windows uses read-only,
unbuffered overlapped ReadFile operations with separate OVERLAPPED structures
and events. The fake backend provides a virtual clock, configurable latencies,
and injected errors, short reads, and corruption.

Logical CSV rows record answers. Replica CSV rows record every completed
operation, including post-answer losers. Activity CSV rows are event-loop
snapshots and can combine several completions or submissions. Completion times
are polling observation times, not device-reported hardware timestamps.

Counters distinguish logical results from replica operations. Returned bytes
include positive short or corrupt results because the operation still performed
work. Loser-cleanup time spans the logical answer to the final pair completion.
A policy denial is counted at most once per logical request; stalls caused by
the total physical cap are retried without incrementing that count.
