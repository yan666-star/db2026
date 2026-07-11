# Commit Apply Gate A/B Design

## Goal

Improve throughput under the current official-like Snapshot Isolation workload without weakening MVCC visibility, conflict detection, WAL ordering, transaction atomicity, or crash recovery.

## Controlled Change

Freeze all files under `SQL测试/performance_test/` during the A/B experiment. The A baseline is the existing 16-client, 10-second warmup, 60-second measurement run with `RMDB_PENDING_WAIT_US=10000`: 138.63 NewOrder tpmC and 22.74 TPS, with consistency and crash recovery passing.

The B candidate changes only the commit-apply synchronization primitive. Replace the current shared mutex plus writer turnstile with the repository's original exclusive `std::mutex`. Record reads and physical commit application therefore use the same exclusive gate. Batch MVCC visibility, SI conflict rules, abort flush rules, BufferPool locking, index lookup, parser scope, and benchmark SQL remain unchanged.

## Acceptance

Run the current quick correctness gate first, followed by the exact A-baseline long command. Keep the candidate only when all consistency and crash-recovery checks pass and same-harness throughput improves. Historical benchmark numbers are diagnostic context only and are not acceptance criteria.

Before and after the experiment, record the SHA-256 of `run_official_like_benchmark.py` and verify that its diff is unchanged.
