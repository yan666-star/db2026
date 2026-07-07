# Performance Consistency Investigation

## Scope

This is a read-only investigation for the Phase 3 performance failure:

```text
Performance consistency check failed after the transaction run.
Failure stage: Post-transaction consistency validation.
Hint: check transaction atomicity, district/order counters, stock updates, and whether aborted transactions leave partial writes.
```

No source code was changed while preparing this report. The goal is to map the framework, identify high-risk consistency paths, and define a safe repair route that does not disturb the first ten tasks.

## Codebase Map

The project is an RMDB C++17 database engine. The relevant subsystems are:

- `src/rmdb.cpp`: server loop, per-client transaction state, `load` command, `set output_file`, statement boundaries, implicit commit/abort handling.
- `src/parser/`: SQL grammar and AST, including transaction syntax, checkpoint, aggregation, union, order/limit, and isolation-level statements.
- `src/analyze/`: semantic analysis, column resolution, aggregate validation, union compatibility, derived table analysis.
- `src/optimizer/`: plan generation and index/seq scan selection.
- `src/portal.h`: physical executor construction, including the important rule that MVCC transactions force seq scan.
- `src/execution/`: select/update/delete/insert, aggregation, union, order, join, index scan, seq scan.
- `src/record/`: physical row storage, RID allocation, MVCC-visible reads, integer equality cache.
- `src/index/`: B+ tree index operations.
- `src/transaction/`: transaction lifecycle, MVCC version map, write conflict checks, commit/abort, GC, checkpoint quiescing.
- `src/recovery/`: WAL records, redo/undo, index recovery, checkpoint restart handling.
- `src/system/`: schema/catalog, index creation, rollback helpers, database flush/checkpoint support.

## Performance Test Assets

Local performance smoke assets are under the repository's SQL test directory, in `performance_test/`.

Important files:

- `00_create_tpcc_tables.sql`: creates TPC-C-shaped tables: `warehouse`, `district`, `customer`, `history`, `orders`, `new_orders`, `order_line`, `item`, `stock`.
- `01_load_tpcc_tables.sql`: loads CSV data via the custom `load file into table` path in `rmdb.cpp`.
- `02_create_primary_indexes.sql`: creates primary-key-like unique indexes.
- `04_tpcc_new_order_smoke.sql`: compact `NewOrder`-shaped snapshot transaction.
- `05_transaction_syntax_smoke.sql`: transaction spelling and rollback/failed-transaction probes.
- `run_performance_smoke.py`: local concurrent consistency probe. It checks `district.d_next_o_id`, `stock.s_quantity`, committed `orders/new_orders/order_line` visibility, and whether aborted orders leave child rows.

The official failure matches the same invariant family, only at larger scale and with benchmark-generated transaction mixes.

## First Ten Tasks: Critical Existing Implementations

These areas appear to support the already-passing ten tasks and should not be broadly rewritten:

- Parser and generated parser artifacts for SQL extensions: `src/parser/yacc.y`, `lex.l`, `ast.h`, generated `yacc.tab.*` and `lex.yy.*`.
- Analyze logic for column resolution, aliases, union branches, aggregation, group/having, and derived tables in `src/analyze/analyze.cpp`.
- Planner behavior in `src/optimizer/planner.cpp`, especially index selection and join construction.
- Executor behavior for aggregation, union, sorting, projection, nested-loop join, seq scan, and index scan.
- Static checkpoint flow in `src/execution/execution_manager.cpp`, `src/transaction/transaction_manager.cpp`, `src/recovery/log_recovery.cpp`, and `src/system/sm_manager.cpp`.
- Output and `load` special commands in `src/rmdb.cpp`.
- Recovery redo/undo and index rebuild/snapshot behavior in `src/recovery/`.

Safe fixes should be local to transaction atomicity/MVCC commit/abort/index synchronization unless evidence proves otherwise.

## Transaction and MVCC Call Chain

### Statement Lifecycle

1. `client_handler()` in `src/rmdb.cpp` receives one SQL request per socket message.
2. `SetTransaction()` starts a transaction if no active transaction exists.
3. Explicit `BEGIN` marks `txn_mode=true`; implicit single statements commit after execution.
4. Exceptions are caught in `client_handler()` and normally call `txn_manager->abort()`, release the transaction, and clear `txn_id`.
5. Successful implicit statements call `txn_manager->commit()` before the response is sent.

Risk:

- If an exception occurs after partial writes but before every write is represented in the transaction write set or MVCC pending map, abort can leave residual state.
- Explicit transactions stop executing only because the server clears `txn_id` after abort; this path must remain intact.

### MVCC Read Path

1. `RmFileHandle::get_record()` reads the physical slot if present.
2. For MVCC transactions, it calls `TransactionManager::get_visible_record()`.
3. Visibility prefers this transaction's pending version, otherwise the newest committed version with `commit_ts <= start_ts`.
4. For non-MVCC reads with a transaction manager, `get_latest_committed_record()` consults MVCC tombstones and then physical storage.

Risk:

- Physical records are authoritative for latest committed state, but MVCC tombstones can hide deleted rows. Any mismatch between physical slots, indexes, and MVCC tombstones can appear as scan/index inconsistency.

### MVCC Write Path

Insert:

1. `InsertExecutor::Next()` builds a record and checks unique indexes.
2. `RmFileHandle::insert_record(..., table_name)` calls `prepare_insert()` for MVCC.
3. It writes a physical row immediately and logs an insert.
4. `InsertExecutor` appends a `WriteRecord(INSERT_TUPLE)`.
5. Index entries are inserted immediately after physical insertion.

Update:

1. `Portal` first scans matching RIDs.
2. `UpdateExecutor::Next()` reads each record via MVCC visibility.
3. It calls `check_write_conflict()`.
4. It calls `prepare_update()` and writes an update log.
5. For MVCC, physical row and indexes are not changed immediately.
6. `commit_mvcc()` later applies physical updates and updates indexes.

Delete:

1. `DeleteExecutor::Next()` reads each record.
2. For MVCC, it calls `prepare_delete()` and writes a delete log.
3. Physical row is not deleted immediately.
4. `commit_mvcc()` removes index entries for committed deletes; GC/checkpoint later applies physical deletion.

Risk:

- Inserts are physically visible before commit, while updates/deletes are deferred until commit. Abort logic must handle this asymmetry perfectly.
- Unique index checks combine index lookup and full-table MVCC scans; under concurrency this is high-risk.

### Commit Path

`TransactionManager::commit()` currently writes and flushes the COMMIT log before `commit_mvcc()` applies MVCC physical changes. Then it clears write sets and marks committed.

Risk:

- This order is dangerous for crash semantics: a COMMIT record can become durable before the physical MVCC updates/index changes are applied and before their durability is guaranteed. Recovery can treat the transaction as committed but not replay all intended state if logs/physical application are inconsistent.
- For post-transaction validation without crash, the more immediate concern is logical atomicity under concurrency: `commit_mvcc()` assigns commit timestamps under `mvcc_latch_`, then applies physical/index operations outside that latch.

### Abort Path

`TransactionManager::abort()` rolls back write records except MVCC update/delete write records. It still rolls back MVCC inserts through `SmManager::rollback_insert()`, because inserts physically write rows and indexes immediately. Then it writes an ABORT log and calls `abort_mvcc()` to remove pending MVCC versions.

Risk:

- This split is intentional, but fragile. If an MVCC insert appends its write record after physical row or index insertion, an exception in between can leave partial state that `abort()` does not know about.
- `SmManager::rollback_insert()` uses `get_record(rid, nullptr)`, deletes index entries, then deletes the physical record. This handles normal insert aborts but depends on the RID and write set being present.

## Recovery and Checkpoint Chain

Startup:

1. `main()` opens the DB and initializes log manager.
2. `recovery->analyze()` identifies active, committed, aborted, and checkpoint state.
3. `redo()` replays action logs except transactions with durable ABORT records.
4. `undo()` reverses loser actions and writes ABORT records.
5. `finish_recovery()` rebuilds free-page lists and possibly indexes.

Checkpoint:

1. `CREATE STATIC_CHECKPOINT` quiesces statements and transactions.
2. It applies committed MVCC deletes physically.
3. It writes a checkpoint log, flushes pages, snapshots indexes, and persists the restart offset.

Risk:

- The first ten tasks likely depend on the current checkpoint and recovery ordering. Avoid broad changes here.
- If Phase 3 includes kill-9 after transaction load, COMMIT/log/physical ordering becomes critical. Fixes must be WAL-consistent, not only memory-consistent.

## Index and Physical Storage Chain

Index creation builds unique B+ tree indexes from table rows. Runtime DML keeps indexes synchronized:

- Non-MVCC insert/update/delete modifies physical row and index immediately.
- MVCC insert modifies physical row and index immediately, then rolls back on abort.
- MVCC update modifies physical row and index only in `commit_mvcc()`.
- MVCC delete removes index entries in `commit_mvcc()`, but leaves physical rows until GC/checkpoint.

Risk:

- Official consistency checks often compare indexed lookup and scan results. A row can pass one path and fail the other if index entries and physical/MVCC state diverge.
- `Portal` forces seq scan for MVCC transactions, but verification queries after the run may use READ COMMITTED and indexes. This makes post-run index correctness essential.

## TPC-C Consistency Chains

### NewOrder

Expected atomic chain:

1. Read `customer` and `warehouse`.
2. Read `district.d_next_o_id`.
3. Increment `district.d_next_o_id`.
4. Insert one `orders` row.
5. Insert one `new_orders` row.
6. For each item, read `item`, read/update `stock`, insert one `order_line`.
7. Commit all changes or none.

Consistency indicators:

- `district.d_next_o_id` increases exactly by committed NewOrder count per `(w_id, d_id)`.
- Each committed order has exactly one parent `orders`, one `new_orders`, and all expected `order_line` rows.
- Aborted orders leave no `orders`, `new_orders`, or `order_line`.
- `stock.s_quantity` decreases exactly by committed quantities.

Highest-risk paths:

- Concurrent updates to the same district counter.
- Concurrent stock decrements.
- Insert failure after district/stock updates.
- Unique-index conflict during `orders`, `new_orders`, or `order_line` insert.

### Payment

Expected atomic chain:

1. Update `warehouse.w_ytd`.
2. Update `district.d_ytd`.
3. Update customer balance/payment counters.
4. Insert `history`.
5. Commit all changes or none.

Highest-risk paths:

- Multiple numeric updates in one transaction.
- History insert after previous updates.
- Abort after partially applying physical insert/update state.

### Stock / District / Order Tables

The official hint names these directly:

- `district`: `d_next_o_id` and likely `d_ytd`.
- `stock`: `s_quantity`, `s_ytd`, `s_order_cnt`, `s_remote_cnt`.
- `orders`: parent row count and uniqueness.
- `new_orders`: child row existence for NewOrder.
- `order_line`: expected line count and quantities.

These checks are sensitive to both lost updates and partial aborts.

## Risk Map

### Critical: No Real Locking for READ COMMITTED

`LockManager` methods currently return `true` without enforcing shared/exclusive locks. Default sessions begin as `READ_COMMITTED`, and official performance transactions may not all switch to snapshot isolation.

Impact:

- Two concurrent READ COMMITTED NewOrder transactions can read the same `district.d_next_o_id`, both compute `+1`, and one increment is lost.
- Concurrent stock decrements can lose one update.
- Since `READ_COMMITTED` does not use MVCC write conflict checks, this is a top suspect for district/stock mismatch.

Safe repair direction:

- Do not implement a broad lock manager first.
- Prefer a small, targeted lost-update guard for READ COMMITTED updates, or route explicit benchmark transactions to an existing MVCC isolation only if SQL semantics support it. This needs evidence from official SQL.

### Critical: MVCC Commit Log Before Physical Apply

`commit()` writes/flushed COMMIT before `commit_mvcc()` applies physical updates and index changes.

Impact:

- Crash after COMMIT flush but before physical/index apply can produce durable committed transactions missing updates.
- If official Phase 3 includes kill-9 validation, this is high risk.

Safe repair direction:

- Investigate whether action logs for MVCC update/delete represent intended physical changes well enough for redo.
- Any reorder must preserve first-ten crash recovery behavior.

### High: Insert Partial Failure Window

MVCC insert writes physical row, logs insert, appends write record, then inserts index entries. If an index insert fails after physical insertion but before all cleanup state is complete, aborted transactions may leak rows or indexes.

Impact:

- Aborted order can leave an `orders`, `new_orders`, or `order_line` physical row.
- Index and scan predicates can disagree.

Safe repair direction:

- Audit exact exception windows in `InsertExecutor::Next()` and `RmFileHandle::insert_record_internal()`.
- Add a reproduction that injects duplicate insert after prior changes, then checks parent/child tables.

### High: READ COMMITTED Reads Use Physical Latest With MVCC Tombstones

`get_latest_committed_record()` prefers physical rows unless latest MVCC version is a committed delete.

Impact:

- If physical row was changed by an uncommitted insert/update that is not represented correctly in MVCC state, READ COMMITTED validation can observe invalid data.

Safe repair direction:

- Verify that every physical early write has rollback coverage.
- For READ COMMITTED update lost updates, physical reads are not enough without conflict control.

### Medium: Index Delete/Insert Semantics Are Unique-Key Only

B+ tree `insert()` ignores duplicates by key, and indexes store one RID per key. This matches primary-key usage but makes index state fragile if a wrong RID remains.

Impact:

- A stale index entry can hide the correct RID or cause false duplicate failures.

Safe repair direction:

- Avoid broad B+ tree rewrites.
- Validate index maintenance at transaction boundaries using small SQL probes.

### Medium: MVCC GC and Tombstone Reclamation

GC periodically removes version history and physically deletes tombstoned rows. It is guarded by `commit_apply_latch_` and watermark logic.

Impact:

- Incorrect reclamation could resurrect deleted rows or erase tombstones needed by readers.

Safe repair direction:

- Do not tune GC until lost-update/abort hypotheses are tested.

## Working Hypotheses

1. Most likely: official Phase 3 uses concurrent default `READ_COMMITTED` transactions, and because `LockManager` is a no-op, district/stock updates suffer lost updates.
2. Very likely if failed transactions are present: partial write cleanup around insert/index failure is incomplete for some error window.
3. Likely for crash validation: COMMIT is flushed before MVCC physical/index application, so crash recovery can acknowledge a transaction whose physical state was not fully installed.
4. Possible: index state diverges from physical/MVCC state after abort or recovery, causing post-run validation to see different counts through indexed vs scan predicates.

## Latest Linux Evidence: READ COMMITTED Lost Update

The existing performance smoke runner produced a concrete concurrent consistency failure:

```text
AssertionError: stock (1, 1) mismatch: expected 39, got 44
```

This means five units of committed stock decrement were lost after the concurrent transaction run. The failure matches the no-op `LockManager` risk: default sessions use `READ_COMMITTED`, `UpdateExecutor` pre-collects RIDs, then each executor read the current row, computed an arithmetic update, and wrote it back without serializing the full read-modify-write sequence.

A separate custom probe passed once:

```text
READ COMMITTED lost-update probe passed: 32 committed transactions, d_next_o_id 11->43, stock1 54->-9, stock2 17->-63
```

That pass does not disprove the smoke failure. The existing smoke remains the authoritative RED signal because it is closer to the repository's TPC-C-shaped workload and already caught a stock invariant mismatch.

The same smoke also exposed a separate explicit-transaction failure-state concern: after a duplicate order insert failed, `history.h_data` still showed `failed-after-conflict`. Treat this as the next candidate bug after the update lost-update fix; do not mix it into the first patch.

### Patch 1: UpdateExecutor Lost-Update Guard

Implemented scope:

- Add a per-table logical update latch on `RmFileHandle`.
- In `UpdateExecutor`, only for non-MVCC / `READ_COMMITTED` updates, hold that latch across current-row read, condition recheck, arithmetic calculation, WAL/write-record registration, physical update, and index maintenance.
- Re-evaluate `WHERE` conditions after acquiring the latch because `Portal` collected RIDs before the latch and another transaction may have changed the row.
- Leave MVCC update handling, commit/abort, recovery, parser/analyze/planner, and B+ tree algorithms untouched.

Expected effect:

- Concurrent `d_next_o_id = d_next_o_id + 1` and `s_quantity = s_quantity - n` updates should no longer overwrite each other's committed arithmetic results in `READ_COMMITTED`.
- If the next smoke failure is about aborted child rows or `failed-after-conflict`, continue with explicit transaction failure-state and insert atomicity investigation.

Linux verification for this patch:

```bash
cmake -S . -B build
cmake --build build -j
./build/bin/unit_test
./build/bin/test_parser
python3 "$(find . -path '*performance_test/run_performance_smoke.py' -print -quit)" --start-server
python3 "$(find . -path '*performance_test/probe_read_committed_lost_update.py' -print -quit)" --start-server
```

Expected result:

- The smoke should not report `stock ... mismatch` or `district.d_next_o_id` mismatch.
- If it still fails, paste the first assertion and the surrounding output. A remaining `failed-after-conflict` or aborted-order residue points to the next repair area, not to this update lost-update patch.

## Latest Linux Evidence: Explicit Transaction Failure State

After Patch 1, the Linux smoke and custom lost-update probe produced the following useful result:

```text
concurrent consistency probe passed (6 committed, 0 aborted)
performance smoke suite passed
READ COMMITTED lost-update probe passed: 32 committed transactions, d_next_o_id 11->43, stock1 54->-9, stock2 17->-63
```

This confirms the first lost-update symptom is no longer reproduced by the local smoke/probe pair. However, the same smoke output still showed a hidden explicit transaction bug:

```text
INSERT INTO orders VALUES (1, 1, 1, 2, '2026-07-01 10:00:03', 0, 1, 1);
failure
INSERT INTO history VALUES (... '2026-07-01 10:00:04', ... 'failed-after-conflict');
COMMIT TRANSACTION;
SELECT h_data FROM history WHERE h_date = '2026-07-01 10:00:04';
| failed-after-... |
Total record(s): 1
```

The smoke runner missed this because the displayed `CHAR` value was truncated to `failed-after-...`, while the Python assertion searched for the full string `failed-after-conflict`.

### Patch 2: Explicit Transaction Failed-State Guard

Implemented scope:

- Strengthen the smoke RED case by changing the final check to:

```sql
SELECT count(*) AS failed_after_count
FROM history
WHERE h_date = '2026-07-01 10:00:04';
```

- In `rmdb.cpp`, add a per-client `explicit_txn_failed` flag.
- When an explicit transaction statement throws `RMDBError`, `TransactionAbortException`, or another standard exception, abort and release the transaction as before, but remember that this connection is still in a failed explicit-transaction state.
- While `explicit_txn_failed` is set:
  - reject ordinary statements with `failure` and do not call `SetTransaction`;
  - reject `COMMIT` with `failure` and clear the failed state;
  - accept `ROLLBACK`/`ABORT` as cleanup and clear the failed state without opening a new transaction;
  - allow a new `BEGIN` to start a fresh explicit transaction.

Expected effect:

- The post-conflict `INSERT INTO history ... 'failed-after-conflict'` should no longer be committed as a new implicit transaction.
- The strong smoke check should return `failed_after_count = 0`.
- The previous `failed-before-conflict` rollback behavior should remain unchanged.

Linux verification for this patch:

```bash
cmake -S . -B build
cmake --build build -j
./build/bin/unit_test
./build/bin/test_parser
python3 "$(find . -path '*performance_test/run_performance_smoke.py' -print -quit)" --start-server
python3 "$(find . -path '*performance_test/probe_read_committed_lost_update.py' -print -quit)" --start-server
```

If the smoke still fails, inspect these first:

- `failed transaction executed statements after abort: expected 0 rows, got ...`
- any `aborted order ... left partial rows`
- any reintroduced `stock ... mismatch` or `district ... mismatch`

### Patch 2 Follow-up: Smoke Expected Failures

The first Linux run after adding `explicit_txn_failed` stopped at:

```text
INSERT INTO orders VALUES (1, 1, 1, 2, '2026-07-01 10:00:03', 0, 1, 1);
failure
INSERT INTO history VALUES (... '2026-07-01 10:00:04', ... 'failed-after-conflict');
failure
AssertionError: statement failed: INSERT INTO history VALUES (... 'failed-after-conflict');
```

This is the desired database behavior: after the explicit transaction failed, the next ordinary statement was rejected instead of being committed as a new implicit transaction. The smoke runner failed because its expected-failure list still only included the duplicate `orders` insert.

Smoke runner update:

- Mark the duplicate `orders` insert as the start of a failed explicit transaction.
- Treat the following `failed-after-conflict` insert as expected `failure`.
- Treat the following `COMMIT TRANSACTION` as expected `failure` and clear the failed-transaction marker.
- Continue to the `count(*)` check so the real invariant is verified:

```sql
SELECT count(*) AS failed_after_count
FROM history
WHERE h_date = '2026-07-01 10:00:04';
```

Expected result remains `failed_after_count = 0`.

### Patch 2 Verification: Smoke Passed

The next Linux run after updating the smoke expected-failure handling reached the strong count check and passed:

```text
INSERT INTO orders VALUES (1, 1, 1, 2, '2026-07-01 10:00:03', 0, 1, 1);
failure
INSERT INTO history VALUES (... '2026-07-01 10:00:04', ... 'failed-after-conflict');
failure
COMMIT TRANSACTION;
failure
SELECT h_data FROM history WHERE h_date = '2026-07-01 10:00:03';
Total record(s): 0
SELECT count(*) AS failed_after_count FROM history WHERE h_date = '2026-07-01 10:00:04';
|                0 |
Total record(s): 1
concurrent consistency probe passed (6 committed, 0 aborted)
performance smoke suite passed
```

Interpretation:

- The duplicate `orders` insert still fails as expected.
- The post-conflict `failed-after-conflict` insert is rejected and is not committed as a new implicit transaction.
- The following `COMMIT TRANSACTION` is rejected instead of committing partial state.
- The before-conflict row is rolled back, and the after-conflict row count is `0`.
- The concurrent district/stock/order consistency probe still passes.

The separate lost-update probe output was pasted with the final line truncated:

```text
READ COMMITTED lost-update probe passed: 32 committed transact
```

The prefix indicates the probe reached its pass path, but keep a full final line in later logs for audit quality.

## Latest Official Evidence: Phase 3 Still Fails

After Patch 1 and Patch 2, the local Linux smoke suite passes, but the official performance test still reports the same post-run consistency failure:

```text
[FAIL] Performance consistency check failed after the transaction run.
Failure stage: Post-transaction consistency validation.
Hint: check transaction atomicity, district/order counters, stock updates, and whether aborted transactions leave partial writes.
```

Interpretation:

- The local smoke now proves two narrow fixes: direct `UpdateExecutor` lost updates and explicit transaction failure-state handling.
- The official workload still exercises a broader transaction boundary than the smoke.
- The unchanged official result means the remaining bug is likely not the single-statement update race already fixed. It is more likely a transaction-lifetime isolation/atomicity gap: writes become physically visible before commit, and abort later restores old records without protecting against concurrent committed writes.

## Isolation and Atomicity Boundary Audit

### READ COMMITTED / Non-MVCC Path

Current behavior:

- Default sessions start as `READ_COMMITTED`.
- `LockManager` is still a no-op.
- Non-MVCC `UPDATE`, `INSERT`, and `DELETE` write physical table/index state immediately.
- Abort rolls back the write set by restoring old records or deleting inserted records.
- Patch 1 serializes the read-modify-write sequence inside one `UpdateExecutor` statement, but releases the guard at statement end.

Anomaly status:

- Dirty read: present. A second transaction can read physical rows inserted or updated by an uncommitted transaction.
- Lost update: partially fixed. Concurrent single-statement arithmetic updates on the same row are guarded, but a later abort can still overwrite another transaction's committed update.
- Non-repeatable read: present under `READ_COMMITTED`, which may be acceptable semantically, but dangerous for benchmark transactions that expect stable business state inside one explicit transaction.
- Phantom read: present under `READ_COMMITTED`, especially because inserts are physical and indexed before commit.
- Abort overwrite: high risk. Example: T1 updates `stock`, T2 reads/updates/commits the same row, then T1 aborts and restores T1's old copy over T2's committed value.

This abort-overwrite pattern directly matches the official hint because it can corrupt `district.d_next_o_id`, `stock.s_quantity`, and float counters such as `warehouse.w_ytd`, `district.d_ytd`, `customer.c_balance`, and `customer.c_ytd_payment`.

### SNAPSHOT ISOLATION / SERIALIZABLE MVCC Path

Current behavior:

- MVCC transactions are enabled only after `set transaction isolation level snapshot isolation` or `serializable`.
- MVCC reads use version visibility by transaction start timestamp.
- MVCC update/delete are deferred until commit.
- MVCC inserts are still physically inserted immediately and rolled back on abort.
- `Portal` forces seq scan for MVCC reads, reducing index/visibility disagreement during transaction execution.

Anomaly status:

- Dirty read: mostly prevented for MVCC-visible reads, but MVCC insert physical/index early visibility remains a fragile area for non-MVCC readers.
- Lost update: guarded by `check_write_conflict()` for MVCC writes.
- Non-repeatable read: prevented by snapshot visibility.
- Phantom read: prevented for snapshot reads; serializable adds predicate tracking, but only for MVCC transactions.
- Crash durability: still risky because COMMIT log is flushed before `commit_mvcc()` applies physical/index changes.

### Float Handling Audit

Float values are stored as 4-byte `float`. Literal integers can be cast to float during analysis. Comparisons read float values and compare via `double`, while arithmetic update reads/writes `float`.

Observed risk:

- Float representation itself is not the primary suspect.
- Float business counters are affected by the same transaction-lifetime race as integer counters because `UPDATE ... SET float_col = float_col + amount` writes physical state immediately in non-MVCC mode.
- Official Payment consistency can fail even if integer `district/stock` probes pass, because `w_ytd`, `d_ytd`, `c_balance`, `c_ytd_payment`, and `history` form another atomic chain.

## Next Optimization / Modification Plan

### Plan A: Build a Probe for Abort Overwriting Committed Updates

Create `performance_test/probe_abort_overwrites_committed_update.py`.

Minimal scenario:

1. Create a small table with one indexed row: `id=1, qty=100`.
2. T1 starts an explicit default `READ_COMMITTED` transaction and updates `qty = qty - 5`.
3. T2 starts another explicit default `READ_COMMITTED` transaction, updates `qty = qty - 3`, and commits.
4. T1 aborts or fails after its update.
5. Expected final value: `97`, because only T2 committed.
6. High-risk current result: `100` or another wrong value, because T1 rollback restores its old copy over T2.

Add a float variant:

1. Table row: `id=1, ytd=100.0`.
2. T1 updates `ytd = ytd + 5.0`, then aborts.
3. T2 updates `ytd = ytd + 3.0`, then commits.
4. Expected final value: `103.0`.

Diagnostic value:

- If this probe fails, it explains why official consistency remains unchanged despite the local smoke passing.
- It directly targets the official hint categories: counters, stock updates, and aborted transactions leaving or undoing wrong state.

### Plan B: Patch 3 Candidate - Serialize Default Explicit Write Transactions

Minimal correctness-first repair:

- Add a server-level guard for default `READ_COMMITTED` explicit transactions.
- Acquire it when a client enters `BEGIN` under `READ_COMMITTED`.
- Hold it until `COMMIT`, `ROLLBACK`, `ABORT`, connection close, or explicit transaction failure cleanup.
- Do not take this guard for `SNAPSHOT_ISOLATION` or `SERIALIZABLE`, because those already use MVCC conflict checks and version visibility.

Expected effect:

- Prevent dirty reads among benchmark explicit write transactions.
- Prevent T1 abort rollback from overwriting T2 committed physical updates because T2 cannot run concurrently with T1.
- Protect both integer and float update chains.
- Avoid broad parser/analyze/planner/MVCC/recovery rewrites.

Tradeoff:

- This is conservative and may reduce concurrency for default `READ_COMMITTED` explicit transactions.
- It is a safer contest-oriented fix than implementing a full row/table lock manager at this stage.

### Patch 3 Implementation: READ COMMITTED Explicit Transaction Guard

Implemented after the official Phase 3 result remained unchanged.

Files changed:

- `src/rmdb.cpp`
- `performance_test/probe_abort_overwrites_committed_update.py`

Probe behavior:

- Creates `abort_probe(id INT, qty INT, ytd FLOAT)`.
- T1 starts a default `READ_COMMITTED` explicit transaction and updates `qty = qty - 5`, `ytd = ytd + 5.0`.
- T2 starts a second default `READ_COMMITTED` explicit transaction and updates `qty = qty - 3`, `ytd = ytd + 3.0`, then commits.
- T1 rolls back.
- Expected final values are `qty = 97` and `ytd = 103.0`, because only T2 committed.
- A result of `qty = 100` or `ytd = 100.0` indicates T1's rollback restored its old copy over T2's committed update.

Guard behavior:

- Add a server-level mutex for default `READ_COMMITTED` explicit transactions.
- Acquire the mutex before executing `BEGIN` when the current session isolation is `READ_COMMITTED`.
- Hold the mutex for the transaction lifetime.
- Release on `COMMIT`, `ROLLBACK`, `ABORT`, explicit transaction failure cleanup, parse failure of `BEGIN`, or connection teardown.
- Do not acquire this guard for `SNAPSHOT_ISOLATION` or `SERIALIZABLE` sessions.

Expected effect:

- Prevent dirty reads among default explicit transactions.
- Prevent aborted transactions from overwriting another default explicit transaction's committed physical update.
- Protect both integer counters and float Payment counters.
- Keep MVCC paths untouched.

### Plan C: Verification After Patch 3

Run, in order:

```bash
cmake -S . -B build
cmake --build build -j
./build/bin/unit_test
./build/bin/test_parser
python3 "$(find . -path '*performance_test/run_performance_smoke.py' -print -quit)" --start-server
python3 "$(find . -path '*performance_test/probe_read_committed_lost_update.py' -print -quit)" --start-server
python3 "$(find . -path '*performance_test/probe_abort_overwrites_committed_update.py' -print -quit)" --start-server
```

Then rerun official Phase 3.

### Plan D: If Official Still Fails

Continue in this order:

1. Add a Payment-focused probe that concurrently updates `warehouse`, `district`, `customer`, and `history`, including aborted payments.
2. Add a scan-vs-index consistency probe after aborts for `orders`, `new_orders`, and `order_line`.
3. Investigate MVCC insert early physical/index visibility if official SQL mixes snapshot and read-committed sessions.
4. Investigate crash durability ordering only if the official Phase 3 includes process kill/restart between transaction run and validation.

## Repair Route

## Linux Verification Protocol

The user runs verification on a Linux VM. When requesting verification, always give commands based on this repository's existing CMake build, SQL test assets, and Python smoke runners. Do not ask the user to invent ad-hoc steps.

### Baseline Build

From the repository root on Linux:

```bash
cmake -S . -B build
cmake --build build -j
```

Basic compiled checks:

```bash
./build/bin/unit_test
./build/bin/test_parser
ctest --test-dir build
```

If a command is unavailable or a binary is missing, rebuild first and report the exact missing path.

### Locate SQL Test Scripts Safely

The SQL test directory has a non-ASCII name on disk. To avoid path encoding mistakes, locate scripts with `find`:

```bash
find . -path '*performance_test/run_performance_smoke.py' -print
find . -path '*10_sql_test/run_full_recovery.sh' -print
find . -path '*9_sql_test/run_q9_tests.py' -print
```

Then run the discovered path exactly. Example:

```bash
python3 "$(find . -path '*performance_test/run_performance_smoke.py' -print -quit)" --start-server
```

### Performance Smoke Verification

Use this after any change touching `rmdb.cpp`, `execution/`, `record/`, `index/`, `transaction/`, `recovery/`, or `system/`:

```bash
python3 "$(find . -path '*performance_test/run_performance_smoke.py' -print -quit)" --start-server
```

This script already covers:

- `load file into table`
- `set output_file off`
- aggregate smoke checks
- primary-key indexes on TPC-C-shaped tables
- one compact `NewOrder` transaction
- transaction syntax and rollback probes
- concurrent consistency checks for `district`, `stock`, `orders`, `new_orders`, and `order_line`

### Recovery Verification

Use recovery scripts after any change touching commit, abort, WAL, checkpoint, delete tombstones, or index recovery:

```bash
script="$(find . -path '*10_sql_test/run_full_recovery.sh' -print -quit)"
test -n "$script" && bash "$script"
```

Also run any narrower recovery script relevant to the changed behavior:

```bash
find . -path '*10_sql_test/run_*recovery*.sh' -o -path '*10_sql_test/run_*recovery*.py'
```

### Task-Specific Regression Verification

Because the first ten tasks already pass, verification should include the tasks most likely affected by the change:

- Transaction/MVCC/recovery changes: task 9, task 10, and `10_sql_test`.
- Parser/analyze/planner changes: parser test plus the SQL task script that exercises the syntax.
- Aggregation/order/union changes: aggregation smoke, union task scripts, and `test_parser`.
- Index changes: performance smoke plus any public single/index recovery probe in `10_sql_test`.

When paths contain non-ASCII names, use `find` instead of typing the directory name manually.

### Small Probe Script Rule

For small targeted validation, the assistant should create a runnable probe script instead of asking for manual SQL entry. Preferred location:

```text
SQL test directory/performance_test/
```

Script naming pattern:

```text
probe_<suspected_bug>.py
```

The script should:

1. Start from an existing built `build/bin/rmdb` or accept `--start-server`.
2. Create/load only the minimum needed schema/data, or reuse the TPC-C smoke setup.
3. Run concurrent clients when testing lost updates.
4. Print expected vs actual values for each invariant.
5. Exit non-zero on failure so the user can paste the result back.

For the first READ COMMITTED lost-update hypothesis, the probe should check:

- Initial `district.d_next_o_id`.
- Number of successful concurrent transactions.
- Final `district.d_next_o_id`.
- Initial/final `stock.s_quantity`.
- Absence of rows for aborted order ids in `orders`, `new_orders`, and `order_line`.

### Verification Request Template

When asking the user to verify on Linux, use this shape:

```text
Please run these from the repo root on the Linux VM:

1. Build:
   cmake -S . -B build
   cmake --build build -j

2. Fast compiled checks:
   ./build/bin/unit_test
   ./build/bin/test_parser

3. Relevant SQL/performance check:
   python3 "$(find . -path '*performance_test/run_performance_smoke.py' -print -quit)" --start-server

Please paste the full failing section if any command fails.
```

For a small custom probe, include the exact script path and exact command.

### Step 1: Reproduce the Official Symptom Locally

Use or extend the existing smoke runner on Linux:

- Run the performance smoke runner from the SQL test directory: `performance_test/run_performance_smoke.py --start-server`.
- If it passes locally, add a heavier READ COMMITTED concurrent update probe:
  - Many clients update the same `district` row.
  - Each transaction inserts one order tree.
  - Verify `d_next_o_id == initial + committed`.
  - Verify stock decrement sum.

Expected diagnostic value:

- If this fails, the root cause is likely missing READ COMMITTED write serialization.
- If this passes but official fails, focus on Payment/Delivery/abort/crash paths.

### Step 2: Add Minimal Diagnostics Before Fixing

Add temporary logs only on Linux investigation branch, not final submission:

- On abort, print transaction id and write-set entries.
- On `RMDBError("failure")` inside explicit transactions, print whether rollback ran.
- Around `commit_mvcc()`, print transaction id, touched RIDs, and table names.
- In duplicate insert failure probes, verify whether physical row/index was already created.

Expected diagnostic value:

- Locate whether mismatches come from lost updates, partial aborts, or crash/recovery.

### Step 3: Fix One Root Cause at a Time

Candidate fix order:

1. READ COMMITTED lost-update protection for update/delete/insert conflicts on existing rows.
2. Insert atomicity around physical row and index insertion.
3. MVCC commit durability order and recovery replay consistency.
4. Index synchronization only if probes show scan/index divergence remains.

Constraints:

- No broad parser/analyze/planner/executor rewrites.
- No benchmark-specific SQL/table hardcoding.
- No change to first-ten feature semantics.
- Each fix needs a narrow SQL or Python regression probe before larger testing.

### Step 4: Verify Against First-Ten Risk Areas

After any code change, run:

- Existing unit/parser tests.
- SQL tests for task 7, task 9, task 10, and recovery.
- Performance smoke with concurrent consistency probe.
- User's official Linux VM performance Phase 1/2/3.

## Do Not Broadly Rewrite

Avoid broad changes in:

- Parser grammar and generated parser files, unless a syntax bug is proven.
- Analyze and planner logic for union/group/order/index choice.
- Aggregation, union, sort, projection, and join executors.
- Checkpoint and recovery ordering, except for a narrowly justified WAL/commit fix.
- B+ tree structure algorithms, unless an index-specific reproduction proves corruption.
- `load` and `output_file` handling, because Phase 1/2 already pass.

## Immediate Next Question for Implementation

Before modifying code, obtain one extra data point from Linux:

```text
Does the local performance smoke suite pass on the current commit?
```

If yes, run a heavier READ COMMITTED concurrent district/stock update probe. If it fails, fix lost-update protection first. If it passes, inspect official transaction mix and focus on Payment/Delivery/abort/crash recovery paths.

## 2026-07-07 Handoff: Phase 3 Still Fails

Current official status reported by the user:

```text
Phase 1 functional tests pass.
Phase 2 load data passes.
Phase 3 post-transaction consistency validation still fails.
```

The previous local fixes/probes addressed narrower symptoms, but did not close
the official Phase 3 failure. Treat all previous fixes as hypotheses, not proof
that the corresponding subsystem is globally correct.

### First-Principles Model

TPC-C consistency is a set of conservation laws over table state. The database
must preserve these laws regardless of interleaving:

1. Atomicity: every transaction contributes either its full delta or zero delta.
2. Isolation: a transaction must not base a committed delta on another
   transaction's uncommitted or later-aborted delta.
3. Durability: after a success response, redo must be able to reconstruct the
   same committed delta after `kill -9`.
4. Index equivalence: an indexed lookup and a table scan must enumerate the same
   committed logical rows.
5. Validation SQL correctness: `GROUP BY`, `MIN/MAX/SUM/COUNT`, and `ORDER BY`
   must compute the invariant from the same logical snapshot.

For Phase 3, reason in deltas, not in individual SQL statements:

- `NewOrder` delta:
  - `district.d_next_o_id += 1`
  - one `orders`
  - one `new_orders`
  - `o_ol_cnt` `order_line` rows
  - per-item stock quantity/count deltas
- `Payment` delta:
  - warehouse/district/customer money counters
  - one `history`
- `Delivery` delta:
  - one `new_orders` delete per district with work
  - matching `orders`, `order_line`, and `customer` updates

If a checker reports mismatch, find which delta was partially applied, applied
twice, overwritten by abort, or invisible through one access path.

### Adversarial Review Matrix

Severity scale:

- Critical: can directly cause official Phase 3 consistency failure.
- High: can corrupt data under common benchmark interleavings.
- Medium: can fail edge probes or lower ranking by causing abort storms.
- Low: unlikely to explain current failure, but worth documenting.

| Area | Attacker angle | Severity | What can break | Improvement direction |
| --- | --- | --- | --- | --- |
| Extreme input | NewOrder invalid item after district/order inserts | Critical | abort leaves `orders/new_orders/order_line` or counter deltas | Build a forced-invalid-item transaction probe with checks before/after every table |
| Extreme input | Delivery district has no `new_orders` while other districts do | High | transaction updates some districts then fails later | Probe partial Delivery abort and commit chains |
| Extreme input | Payment by customer last name returns many candidates | High | wrong customer updated, history still inserted | Probe median-customer selection and abort after customer update |
| Concurrency | Two transactions update same `district` or `stock` row | Critical | lost update or abort overwrite | Keep READ COMMITTED guard/MVCC conflict checks; add heavier concurrent probes |
| Concurrency | Delivery deletes `new_orders` while NewOrder inserts nearby keys | Critical | gaps or wrong min/max/count relation | Add scan-vs-index and Delivery min-delete probe |
| Concurrency | Snapshot SET only reaches one connection | Critical | some workers run READ COMMITTED and corrupt counters | Verify server-wide/default isolation behavior with multi-socket probe |
| Concurrency | Physical `get_record` races with commit-time `update_record` | High | torn old record becomes committed update input | Keep short physical read latch; consider page-level read/write discipline if probe fails |
| Failure recovery | Commit log durable before MVCC physical apply | Critical for kill-9 stage | committed update missing after restart | Probe kill between commit acknowledgement and restart; consider WAL redo completeness |
| Failure recovery | Abort log durable after incomplete rollback | Critical | loser txn skipped by recovery while partial rows remain | Audit rollback-before-abort-log ordering and injected failure windows |
| Failure recovery | Insert log exists before index insert/write set append | High | row/index divergence after crash or abort | Make insert/index/write-set ordering atomic or add compensating cleanup |
| Malicious misuse | Client continues statements after explicit txn failure | High | post-failure statements become implicit commits | Preserve `explicit_txn_failed` drain behavior; test with exact count queries |
| Malicious misuse | Multiple `BEGIN`/`COMMIT` spellings and missing semicolons | Medium | transaction guard not released or isolation not applied | Probe `BEGIN WORK`, `START TRANSACTION`, `COMMIT TRANSACTION`, disconnect |
| Validation SQL | Multi-column group with several aggregates | High | checker reports false mismatch | Add local checker clone using official consistency queries |
| Index equivalence | Aborted insert leaves index key or committed delete leaves stale key | Critical | index scan count differs from table scan | Add scan-vs-index consistency probe after abort/commit/restart |

### Updated Plan

1. Freeze known-good constraints:
   - Do not touch `src/rmdb.cpp` socket/fd setup.
   - Do not rewrite recovery broadly while Phase 1 passes.
   - Do not add table-name or SQL-text hardcoding.
   - Keep changes in generic transaction, record, index, or validation logic.

2. Reconstruct current code state:
   - Confirm whether Patch 1, Patch 2, and Patch 3 are present in code, not only
     documented.
   - Confirm any uncommitted local artifacts are unrelated test database files.

3. Build a local clone of the official post-run consistency checker:
   - Run the documented queries for `district`, `orders`, `new_orders`,
     `order_line`, and total `orders`.
   - Add derived assertions:
     - `d_next_o_id == max(o_id) + 1`
     - optional `sum(o_ol_cnt) == count(order_line)` for generated workloads.
       Do not enable it for the bundled miniature CSV baseline: that data has
       random `orders.o_ol_cnt` values and a fixed 10 `order_line` rows per
       order, so the relation is false immediately after load.
     - `new_orders` ranges/counts are internally consistent after Delivery
       semantics.
   - This separates "data is wrong" from "checker SQL is wrong".
   - Current helper script:
     `SQL测试/performance_test/check_tpcc_consistency.py`.
     It connects to an already-running server and reports the first failing
     district/order invariant.

4. Add the next probe before changing core code:
   - Priority A: Delivery min/delete/update/customer chain, with concurrent
     NewOrder inserts.
   - Priority B: Payment money counters plus `history`, including forced abort.
   - Priority C: snapshot isolation propagation across multiple sockets.
   - Priority D: scan-vs-index equivalence after aborts and deletes.

5. Fix only the first reproduced failing invariant:
   - If Delivery creates `new_orders` gaps: inspect DELETE visibility and index
     maintenance.
   - If Payment counters mismatch: inspect multi-row update atomicity and abort
     rollback.
   - If snapshot propagation fails: fix connection/session isolation handling.
   - If scan-vs-index diverges: fix generic index maintenance, not table names.

6. Verification order after each patch:
   - `git diff --check`
   - build on Linux
   - parser/unit tests
   - performance smoke
   - new targeted probe
   - official Phase 3

### Immediate Next Implementation Target

The best next target is a local official-checker clone plus a Delivery-focused
probe. The existing smoke covers compact NewOrder and READ COMMITTED lost update
patterns; it does not stress Delivery's `MIN(no_o_id)` selection, delete,
orders/order_line/customer update chain, or the resulting `new_orders`
range/count invariants. Delivery is the largest uncovered gap that can explain
why local smoke passes while official Phase 3 still fails.

### Current Finding: MVCC Delete Physical State

Code review found a generic MVCC delete inconsistency in
`src/transaction/transaction_manager.cpp`:

- `DeleteExecutor` under MVCC calls `prepare_delete()` and writes the delete log,
  but does not physically remove the tuple immediately.
- `commit_mvcc()` marked the pending delete committed and removed index entries,
  but did not delete the physical record slot.
- Therefore an indexed lookup could stop seeing a committed delete while a table
  scan or aggregate over physical slots could still count the row until GC.

This directly matches a Phase 3 shape: Delivery deletes rows from `new_orders`,
then the official post-run checker may aggregate the table before GC has
reclaimed the stale physical slots. The fix is generic, not TPC-C specific:

- During MVCC commit, for each committed delete, remove index entries using the
  saved `before` image, then delete the physical record if the slot still exists.
- Keep the tombstone version in `record_versions_` so older snapshots can still
  decide visibility from the version chain.
- GC/checkpoint reclamation already calls `record_exists()` before physical
  deletion, so the new immediate physical delete is idempotent with later
  cleanup.

Post-patch probes to prioritize:

1. Delivery-style `DELETE FROM new_orders ...` under snapshot isolation followed
   immediately by scan-based `COUNT/MIN/MAX`.
2. Concurrent snapshot reader started before the delete must still see the old
   row through MVCC history.
3. Scan-vs-index count after committed delete and after aborting a delete.

## 2026-07-07 Deferred Hypothesis: Isolation Propagation Risk

Official Phase 3 still failed after the MVCC delete physical-state fix. One
possible risk is isolation mode propagation, but do not treat it as proven
without a local probe or official-driver evidence:

- The benchmark statement says TPC-C correctness is tested on snapshot
  isolation.
- `rmdb.cpp` kept `session_isolation` as a per-socket local variable initialized
  to READ COMMITTED for every new client.
- `SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION` updated only the current
  socket through `context->session_isolation_`.
- If the performance driver sends SET on one control connection and then opens
  worker connections, those workers silently run READ COMMITTED.

Adversarial consequence:

- MVCC INSERT writes the physical slot before COMMIT, and hides it from MVCC
  readers using the pending version chain.
- A READ COMMITTED reader can call `get_latest_committed_record()` and return
  the physical row even though the corresponding MVCC version is still pending.
- That can let another transaction see an uncommitted `orders`, `new_orders`, or
  `order_line` row, base counter updates on it, or trip unique checks. The final
  checker then reports exactly the same generic symptom: partial writes,
  district/order counter mismatch, stock mismatch, or aborted work leaking.

Possible patch direction if proven:

- Keep READ COMMITTED as the process default until a SET command is received.
- When a client sends `SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION` or
  `... SERIALIZABLE`, update both the current socket and a process-wide default
  used by subsequently accepted sockets.
- Do not change socket/fd handling, recovery, parser grammar, or table-specific
  behavior.
- Preserve the explicit-failed-transaction drain behavior: SET is handled only
  after the existing `explicit_txn_failed` guard has rejected disallowed
  statements.

Targeted probe if this path becomes relevant:

- `SQL测试/performance_test/probe_snapshot_isolation_propagation.py`
- It sends SET on one connection, then opens a writer connection that inserts a
  `new_orders` row inside an uncommitted transaction, and a third reader
  connection that counts that row.
- Expected result: the reader sees count 0. If it sees 1, new connections are
  still READ COMMITTED and can observe uncommitted physical inserts.

Important correction after local smoke:

- Do not keep process-wide SET propagation unless the official driver proves it
  is required.
- `run_performance_smoke.py` intentionally has a default READ COMMITTED
  concurrent NewOrder probe. A process-wide SET from earlier setup changes later
  probe connections to snapshot isolation and causes expected READ COMMITTED
  transactions to abort.
- Current safe behavior: each new connection starts READ COMMITTED; SET changes
  only that connection.
- Do not hand-parse `SET TRANSACTION ISOLATION ...` in `rmdb.cpp`. The parser
  already supports the standard semicolon-terminated statement. The only
  semicolon-less special command required by the performance statement is
  `set output_file off`.

## 2026-07-07 Follow-up: READ COMMITTED Must Not See Pending MVCC Inserts

Re-reading the official failure stage matters: the current failure is
post-transaction table consistency, not the later kill-9 recovery gate. The most
direct invariant violation is any transaction reading uncommitted rows during
the pressure run and then committing dependent counter or stock updates.

Concrete code issue:

- MVCC inserts call `prepare_insert()` and write the physical record before
  COMMIT.
- `get_latest_committed_record()` is used by non-MVCC/READ COMMITTED reads.
- Before the fix, if a RID had only a pending insert version and a physical
  slot, `get_latest_committed_record()` returned the physical row.
- That violates the function name and READ COMMITTED semantics: a row with no
  committed version must not be visible merely because its physical slot was
  prewritten.

Adversarial P3 effect:

- A concurrent transaction can see an uncommitted `orders`, `new_orders`, or
  `order_line` row.
- It can then update `district`, `stock`, `customer`, or Delivery state based on
  data that later aborts.
- After the pressure window, all individual statements may have returned
  success/failure, but table-level conservation laws no longer balance.

Patch direction:

- In `TransactionManager::get_latest_committed_record()`, scan the MVCC history
  for pending insert versions (`before_deleted && !deleted`).
- If there is no committed version for that RID and a pending insert exists,
  return `nullptr` even if the physical slot exists.
- Keep returning the physical row for normal loaded rows, committed inserts, and
  committed latest state, because those either have no MVCC history or have a
  committed version.

New targeted probe:

- `SQL测试/performance_test/probe_uncommitted_mvcc_insert_visibility.py`
- It opens a READ COMMITTED reader first, then opens a snapshot writer that
  inserts into `new_orders` inside an uncommitted transaction.
- Expected result: the reader's `COUNT(*)` is 0 until the writer commits.

## 2026-07-07 Follow-up: Optimized Read Paths Must Use MVCC Visibility

A second concrete visibility hole was found after the pending-insert fix:

- `RmFileHandle::get_record()` applies MVCC/latest-committed visibility rules.
- Plain seq scan and per-RID index scan use `get_record()`.
- `RmFileHandle::batch_get_records()` copied physical slots directly and only
  returned physical records.
- Non-MVCC `IndexScanExecutor` uses `batch_get_records()` for performance.
- The integer equality-cache path in `SeqScanExecutor` also assumed every cached
  RID produced a non-null record.

Adversarial P3 effect:

- A snapshot transaction can physically insert an `orders`, `new_orders`, or
  `order_line` row before commit.
- The index entry also exists before commit.
- A concurrent READ COMMITTED index lookup can hit `batch_get_records()` and see
  the uncommitted physical row, bypassing `get_latest_committed_record()`.
- That reader may then commit dependent updates to `district`, `stock`,
  `orders`, `customer`, or Delivery state based on data that later aborts.

Patch direction:

- Make `batch_get_records()` filter every physical record through
  `get_visible_record()` for MVCC transactions or
  `get_latest_committed_record()` for non-MVCC transactions.
- Keep the returned `records` vector and caller-owned `rids` vector aligned by
  filtering both together.
- In equality-cache seq scan, skip null records returned by `get_record()`.

Relevant probe:

- `SQL测试/performance_test/probe_uncommitted_mvcc_insert_visibility.py`
- Its `COUNT(*) FROM new_orders WHERE no_w_id = ... AND no_d_id = ... AND
  no_o_id = ...` query is expected to choose the `new_orders(no_w_id,no_d_id,
  no_o_id)` index, so it specifically exercises the optimized index path.

## 2026-07-07 Follow-up: Physical Reads Must Not Interleave With MVCC Commit Apply

Another post-run consistency window remains even after visibility filtering:

- `commit_mvcc()` uses `commit_apply_latch_` to serialize physical application
  of committed MVCC updates/deletes.
- Inside that latch it first assigns commit timestamps in `record_versions_`,
  then applies physical record and index changes.
- Ordinary readers copied physical bytes without taking `commit_apply_latch_`.
- A reader could therefore observe an old physical value while the version map
  already considered a newer value committed.

Adversarial P3 effect:

- Transaction A commits `district.d_next_o_id += 1` or a `stock` decrement.
- Transaction B reads the same row during the small gap between logical commit
  timestamp assignment and physical application.
- B bases its own committed delta on the stale physical value, producing lost
  counter/stock updates even though both transactions eventually commit.

Patch direction:

- Expose a narrow `TransactionManager::acquire_commit_apply_latch()` helper.
- `RmFileHandle::get_record()` and `batch_get_records()` take that latch before
  copying physical bytes when a transaction manager is present.
- The lock order remains `commit_apply_latch_ -> mvcc_latch_`, matching the
  existing transaction-manager lock-ordering rule.
- Reads with no transaction manager, such as recovery/catalog maintenance paths,
  keep their previous behavior.
