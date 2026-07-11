# Commit Apply Gate A/B Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Test whether restoring the exclusive commit-apply gate improves throughput under the current frozen SI benchmark while preserving all correctness gates.

**Architecture:** Use one `std::mutex` to serialize record reads and physical commit application. Change only synchronization types and call signatures; retain all MVCC, WAL, rollback, BufferPool, index, parser, and benchmark behavior.

**Tech Stack:** C++17, CMake, Python 3 benchmark scripts, Linux RMDB runtime.

## Global Constraints

- Do not modify `SQL测试/performance_test/` during the A/B experiment.
- Do not weaken SI visibility, write-conflict handling, WAL ordering, rollback, or crash recovery.
- Preserve unrelated working-tree changes and do not create a git commit.

---

### Task 1: Restore the Exclusive Commit-Apply Gate

**Files:**
- Modify: `src/common/perf_counters.h`
- Modify: `src/transaction/transaction_manager.h`
- Modify: `src/transaction/transaction_manager.cpp`

**Interfaces:**
- Produces: `std::unique_lock<std::mutex> TransactionManager::acquire_commit_apply_latch()`.
- Produces: `lock_commit_apply_read(std::mutex&)` and `lock_commit_apply_write(std::mutex&)`.

- [ ] **Step 1: Capture the frozen harness and current source diff**

Run `sha256sum SQL测试/performance_test/run_official_like_benchmark.py`, `git diff -- SQL测试/performance_test`, and `git diff --check`.

Expected: benchmark SHA is recorded; no new benchmark change is introduced by this task.

- [ ] **Step 2: Verify the current candidate differs from the desired gate**

Run `grep -R "commit_apply_turnstile\|shared_mutex commit_apply" -n src/common src/transaction`.

Expected: current source contains the turnstile and shared commit-apply mutex.

- [ ] **Step 3: Apply the minimal synchronization change**

Change the read/write helpers and manager API to `std::unique_lock<std::mutex>`, remove the turnstile argument from every call, remove `commit_apply_turnstile_`, and change `commit_apply_latch_` to `std::mutex`. Do not change any protected operation or lock ordering beyond removing the turnstile.

- [ ] **Step 4: Run static verification**

Run `git diff --check`, inspect `git diff -- SQL测试/performance_test`, and repeat the stale-symbol grep.

Expected: no whitespace error; performance-test diff is unchanged; grep finds no stale turnstile/shared gate.

- [ ] **Step 5: Build and run correctness gates on Linux**

Run `cmake --build build -j` and `SKIP_BUILD=1 bash SQL测试/performance_test/run_local_evaluation.sh quick`.

Expected: build succeeds; isolation probe, functional checks, TPCC consistency, order-line gap, and crash recovery pass.

- [ ] **Step 6: Run the same-harness B measurement**

Run the exact 16-client, 10-second warmup, 60-second measurement, 10ms pending-wait command used for A, including `--isolation snapshot`, `--perf-diag`, and `--crash-check`.

Expected: correctness and recovery pass. Retain the candidate only if repeated current-harness tpmC/TPS improve; otherwise revert only this task's synchronization edits.
