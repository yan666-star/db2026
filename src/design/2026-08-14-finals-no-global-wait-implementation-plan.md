# Finals No-Global-Wait Storage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the finals SI hot path with transaction-private staged writes, fail-fast sharded MVCC, page-local B+Tree structural modification, and a real PREPARE_SET/EXEC_BATCH 50-warehouse performance gate.

**Architecture:** Executors stage logical changes in a transaction-owned `TransactionWriteBatch`; a commit coordinator validates fail-fast intents, reserves Heap slots, batches WAL, applies Heap/Index pages, reaches durable COMMIT, then atomically publishes visibility. MVCC and unique intents are sharded with no transaction-to-transaction waits, while B+Tree fast inserts latch only one leaf and structural inserts retain only the unsafe page suffix.

**Tech Stack:** C++17, GCC/Clang-compatible standard library concurrency, CMake/CTest, existing RMDB Wire v3 and Python workload client, Linux `perf`/`strace`, GoogleTest-style repository tests.

## Global Constraints

- Modify and commit only paths under `src/`.
- Preserve Wire v3, SQL syntax, 4 KiB page size, Heap/B+Tree disk formats, and WAL record format.
- Preserve AUTO_ABORT, SI, SERIALIZABLE/SSI, FLOAT32, PageLSN, durable COMMIT, and SIGKILL recovery.
- COMMIT ACK requires `durable_lsn >= commit_lsn`; non-durable transactions remain invisible.
- Do not specialize on table names, column names, SQL text, statement ids, client count, warehouse count, filenames, or request interleaving.
- SI transactions never wait on another transaction; record and unique-key conflicts fail fast.
- Page pins remain mandatory while page memory is referenced, but each storage batch pins a touched page once.
- Each task is a separate commit. A failing correctness, recovery, completion, abandoned, RSS, or paired-performance gate stops the next task.
- Linux/GCC C++17 Release builds are normative for local verification.

## File Map

- `src/test/finals_workload_driver.py`: official-shape PREPARE_SET/EXEC_BATCH driver and JSON metrics.
- `src/test/finals_workload_contract_test.cpp`: cheap CTest contract for workload shape and prohibited EXEC_STREAM fallback.
- `src/transaction/transaction_write_batch.{h,cpp}`: transaction-private logical writes and own-write overlay.
- `src/transaction/mvcc_store.{h,cpp}`: sharded versions and fail-fast record intents.
- `src/transaction/txn_registry.{h,cpp}`: per-transaction atomic lifecycle and timestamp state.
- `src/transaction/storage_commit_executor.{h,cpp}`: grouped WAL/Heap/Index commit pipeline.
- `src/record/rm_file_handle.{h,cpp}`: slot reservation plus per-page batch apply/release.
- `src/index/ix_index_handle.{h,cpp}`: optimistic leaf insert and page-local structural restart.
- `src/storage/page_guard.{h,cpp}` and `src/common/perf_counters.h`: content-latch timing and page-pin evidence.
- Existing executors, SQL execution service, and transaction manager become adapters to these deeper modules.

---

### Task 1: Build the real finals performance feedback loop

**Files:**
- Create: `src/test/finals_workload_driver.py`
- Create: `src/test/finals_workload_contract_test.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Produces: `python3 src/test/finals_workload_driver.py --help`
- Produces: JSON fields `rounds`, `median_new_order_per_min`, `completion`, `abandoned`, `latency_ms`, `warehouse_coverage`, `cpu`, `rss`, `io`, and `server_counters`.
- Consumes: existing `SQL测试/performance_test/wire_client.py`, generated `tpcc_full` CSV files, and the current `rmdb` binary.

- [ ] **Step 1: Write the failing source-contract test**

Add `finals_workload_contract_test.cpp` that reads `src/test/finals_workload_driver.py` and requires all of these literals and invariants:

```cpp
require(source.find("prepare_set(") != std::string::npos,
        "driver must install PREPARE_SET");
require(source.find("execute_batch(") != std::string::npos,
        "ranked transactions must use EXEC_BATCH");
require(source.find("WAREHOUSES = 50") != std::string::npos,
        "driver must cover 50 warehouses");
require(source.find("MIX = (45, 43, 4, 4, 4)") != std::string::npos,
        "driver must use finals mix");
require(source.find("MEASURE_SECONDS = 150") != std::string::npos,
        "driver must use 150-second windows");
require(source.find("ROUNDS = 3") != std::string::npos,
        "driver must use three ranked rounds");
require(source.find("client.execute(sql)") == std::string::npos,
        "ranked transactions must not fall back to EXEC_STREAM");
```

- [ ] **Step 2: Run RED**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2 --target test_finals_workload_contract
ctest --test-dir build -R test_finals_workload_contract --output-on-failure
```

Expected: build or test failure because the driver/target does not exist.

- [ ] **Step 3: Implement the driver without production specialization**

Define immutable operation metadata:

```python
@dataclass(frozen=True)
class PreparedOp:
    name: str
    sql: str
    parameter_types: tuple[int, ...]

PREPARED: tuple[PreparedOp, ...] = build_prepared_catalog()
WAREHOUSES = 50
CLIENTS = 32
MIX = (45, 43, 4, 4, 4)
MEASURE_SECONDS = 150
ROUNDS = 3
```

`build_prepared_catalog()` returns entries for `begin`, `commit`, `rollback`,
`new_order_customer`, `new_order_district`, `new_order_district_update`,
`new_order_orders_insert`, `new_order_queue_insert`, `new_order_item`,
`new_order_stock`, `new_order_stock_update`, `new_order_line_insert`,
`payment_warehouse_update`, `payment_district_update`, `payment_customer`,
`payment_customer_update`, `payment_history_insert`, `order_status_customer`,
`order_status_order`, `order_status_lines`, `delivery_oldest`,
`delivery_order`, `delivery_amount`, `delivery_queue_delete`,
`delivery_order_update`, `delivery_lines_update`, `delivery_customer_update`,
`stock_level_next_order`, and `stock_level_count`. Every SQL template uses `?`
parameters and declares the matching Wire INT32/FLOAT32/CHAR types.

`connect_worker()` must set session SI and call `prepare_set(PREPARED)` once.
Each transaction builder returns one ordered list of `(statement_id,
typed_parameters)` operations and calls exactly
`client.execute_batch(operations, auto_abort=True)`. NewOrder chooses 5--15
order lines, every round records all
50 warehouse ids, and expected business rollback is separated from abandoned.

Support `--exploratory --warmup 2 --measure 8 --rounds 1` without changing the
default constants. Refuse to label exploratory output as a formal median.

- [ ] **Step 4: Run GREEN and driver self-test**

Run:

```bash
cmake --build build -j2 --target test_finals_workload_contract
ctest --test-dir build -R test_finals_workload_contract --output-on-failure
python3 src/test/finals_workload_driver.py --self-test
```

Expected: PASS; self-test decodes five transaction shapes, 5- and 15-line
NewOrder batches, and confirms no ranked `EXEC_STREAM` call.

- [ ] **Step 5: Capture current RED baseline**

Use one loaded database and record its absolute path before running:

```bash
RMDB_PERF_DIAG=1 python3 src/test/finals_workload_driver.py \
  --start-server --exploratory --clients 32 --warehouses 5 \
  --data-dir src/test/performance_test/table_data/tpcc_small \
  --db-dir build/finals_red_small \
  --json-output build/finals_red_small.json
```

Expected: the driver completes and records low NewOrder throughput, completion,
latency, conflict, latch, fsync, and byte-write evidence. This is exploratory,
not a formal finals score.

- [ ] **Step 6: Commit**

```bash
git add src/CMakeLists.txt src/test/finals_workload_driver.py \
  src/test/finals_workload_contract_test.cpp
git commit -m "Add finals-shaped typed batch performance gate"
```

---

### Task 2: Add the transaction-private write batch

**Files:**
- Create: `src/transaction/transaction_write_batch.h`
- Create: `src/transaction/transaction_write_batch.cpp`
- Create: `src/test/transaction_write_batch_test.cpp`
- Modify: `src/transaction/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`
- Modify: `src/transaction/transaction.h`

**Interfaces:**
- Produces:

```cpp
using TempRowId = uint64_t;

enum class LogicalWriteKind { INSERT, UPDATE, DELETE };
enum class OverlayKind { ABSENT, VALUE, DELETED };

struct RecordIdentity {
    uint64_t file_id;
    Rid rid;
};

struct StagedWrite {
    LogicalWriteKind kind;
    std::string table_name;
    uint64_t file_id;
    std::optional<Rid> rid;
    TempRowId temp_id;
    std::vector<char> before;
    std::vector<char> after;
};

class TransactionWriteBatch {
public:
    TempRowId stage_insert(std::string table_name, uint64_t file_id,
                           std::vector<char> record);
    void stage_update(std::string table_name, uint64_t file_id, Rid rid,
                      std::vector<char> before,
                      std::vector<char> after);
    void stage_delete(std::string table_name, uint64_t file_id, Rid rid,
                      std::vector<char> before);
    OverlayKind lookup(uint64_t file_id, Rid rid,
                       std::vector<char> *value) const;
    const std::vector<StagedWrite> &writes() const noexcept;
    std::vector<StagedWrite> freeze();
    void discard() noexcept;
};
```

- Consumes: `Rid` and transaction ownership only; it must not depend on
  `SmManager`, BufferPool, Index, WAL, or SQL types.

- [ ] **Step 1: Write RED tests**

Cover insert temp ids, update coalescing, insert-then-update, insert-then-delete
cancellation, update-then-delete, own-write lookup, deterministic freeze order,
and discard:

```cpp
TempRowId id = batch.stage_insert("t", 7, bytes("one"));
batch.stage_update("t", 7, Rid{3, 4}, bytes("old"), bytes("new"));
require(batch.lookup(7, Rid{3, 4}, &visible) == OverlayKind::VALUE,
        "own update is not visible");
require(visible == bytes("new"), "wrong overlay bytes");
```

- [ ] **Step 2: Run RED**

```bash
cmake --build build -j2 --target test_transaction_write_batch
ctest --test-dir build -R test_transaction_write_batch --output-on-failure
```

Expected: missing header/target failure.

- [ ] **Step 3: Implement minimal pure module**

Use a vector for stable statement order and an unordered map from
`(file_id,Rid)` to vector position for O(1) coalescing. `freeze()` moves the
vector out after stable sorting existing-RID writes by `(file_id,page,slot)`;
inserts remain stable by `temp_id`. A frozen or discarded batch rejects later
stage calls with `InternalError`.

- [ ] **Step 4: Attach one batch to each transaction**

Add:

```cpp
TransactionWriteBatch &write_batch() noexcept;
const TransactionWriteBatch &write_batch() const noexcept;
```

to `Transaction`; remove no existing write-set behavior yet.

- [ ] **Step 5: Run GREEN and full CTest**

```bash
cmake --build build -j2 --target test_transaction_write_batch
ctest --test-dir build -R test_transaction_write_batch --output-on-failure
ctest --test-dir build --output-on-failure
```

Expected: new tests and all existing tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/transaction src/test/transaction_write_batch_test.cpp \
  src/CMakeLists.txt
git commit -m "Add transaction-private logical write batches"
```

---

### Task 3: Stage executor writes and eliminate normal-abort physical rollback

**Files:**
- Create: `src/transaction/storage_commit_executor.h`
- Create: `src/transaction/storage_commit_executor.cpp`
- Create: `src/test/staged_write_visibility_test.cpp`
- Create: `src/test/abort_without_checkpoint_test.cpp`
- Modify: `src/execution/executor_insert.h`
- Modify: `src/execution/executor_update.h`
- Modify: `src/execution/executor_delete.h`
- Modify: `src/execution/executor_index_scan.h`
- Modify: `src/execution/executor_seq_scan.h`
- Modify: `src/record/rm_file_handle.h`
- Modify: `src/record/rm_file_handle.cpp`
- Modify: `src/storage/disk_manager.h`
- Modify: `src/transaction/transaction_manager.cpp`
- Modify: `src/transaction/transaction_manager.h`
- Modify: `src/transaction/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `TransactionWriteBatch::freeze()` from Task 2.
- Produces:

```cpp
struct ReservedInsert {
    TempRowId temp_id;
    Rid rid;
    std::vector<char> record;
};

struct PreparedStorageCommit {
    std::vector<StagedWrite> writes;
    std::vector<ReservedInsert> inserts;
    std::unordered_map<TempRowId, Rid> resolved_insert_rids;
    lsn_t greatest_row_lsn = INVALID_LSN;
    bool physical_apply_started = false;
};

class HeapSlotReservation {
public:
    std::vector<ReservedInsert> reserve(
        const std::vector<StagedWrite> &inserts);
    void release() noexcept;
};

class StorageCommitExecutor {
public:
    PreparedStorageCommit prepare(Transaction *txn);
    void apply(PreparedStorageCommit *commit, Context *context);
};
```

- [ ] **Step 1: Write RED visibility and abort tests**

`staged_write_visibility_test` must prove: physical Heap/index state is
unchanged before commit; the writer sees its staged insert/update/delete; a
second SI transaction sees the old snapshot; commit makes the complete result
visible.

`abort_without_checkpoint_test` uses a `CountingDiskManager` and requires:

```cpp
txn_manager.abort(writer, log_manager);
require(disk.sync_all_calls() == 0,
        "pre-apply abort synchronized all database files");
require(disk.database_fsync_calls() == 0,
        "pre-apply abort fsynced a database file");
```

Make `DiskManager::sync_all_open_files()` and `DiskManager::sync_file()`
virtual so the counting adapter observes the real abort seam without changing
production call sites.

- [ ] **Step 2: Run RED**

```bash
cmake --build build -j2 --target test_staged_write_visibility \
  test_abort_without_checkpoint
ctest --test-dir build -R 'test_(staged_write_visibility|abort_without_checkpoint)' \
  --output-on-failure
```

Expected: tests fail because executors still mutate Heap/Index eagerly and
abort calls `flush_for_checkpoint()`.

- [ ] **Step 3: Stage INSERT/UPDATE/DELETE**

Replace eager Heap/Index/WAL operations in SI executors with
`txn->write_batch().stage_*`. Retain the old direct path for non-MVCC modes.
Expression evaluation and unique-key derivation remain generic catalog-driven
logic. Do not append the legacy `WriteRecord` for a staged SI write.

- [ ] **Step 4: Merge own-write overlays into reads**

For RID reads, call `write_batch().lookup(file_id,rid)` before returning the
stored snapshot. For table/range scans, filter staged updates/deletes from
stored RIDs and append staged inserts whose records satisfy the executor's
existing `eval_conditions`. Deduplicate by real RID or transaction-local id.

- [ ] **Step 5: Reserve insert slots without exposing rows**

Add per-file reservation metadata guarded by `reservation_latch_`. Reservation
scans the bitmap under a read guard, excludes already-reserved slots, and
records `(page,slot)` only in memory. `apply_reserved_inserts` takes one write
guard per page, revalidates every slot, sets bitmap/data/count, assigns PageLSN,
and marks dirty. `release_reserved_slots` removes only un-applied reservations.

- [ ] **Step 6: Route SI commit through `StorageCommitExecutor`**

At this task's scope, reuse current MVCC validation and Index methods, but
materialize the frozen batch only inside commit. Record whether physical apply
started. In abort, call `flush_for_checkpoint()` only for the exceptional
partial-apply lane; a transaction that never entered apply discards its batch
and performs no database-file sync.

- [ ] **Step 7: Run GREEN plus AUTO_ABORT/ACID smoke**

```bash
ctest --test-dir build -R 'test_(staged_write_visibility|abort_without_checkpoint|batch_visibility|mvcc_commit_visibility)' --output-on-failure
python3 SQL测试/performance_test/run_acid_tests.py --start-server --quick \
  --isolation snapshot --build-dir build --db-dir build/task3_acid_si
```

Expected: all pass; diagnostic abort counters show zero checkpoint flushes for
pre-commit write conflicts.

- [ ] **Step 8: Run paired exploratory benchmark**

Run the Task 1 command against the same data, seed, clients, and window. Keep
the task only if completion/abandoned improve and no correctness metric
regresses; record median/spread even for shortened repeats.

- [ ] **Step 9: Commit**

```bash
git add src/execution src/record src/transaction src/test \
  src/CMakeLists.txt
git commit -m "Stage SI writes until validated commit"
```

---

### Task 4: Replace global MVCC coordination with fail-fast shards

**Files:**
- Create: `src/transaction/txn_registry.h`
- Create: `src/transaction/txn_registry.cpp`
- Create: `src/transaction/mvcc_store.h`
- Create: `src/transaction/mvcc_store.cpp`
- Create: `src/test/mvcc_fail_fast_test.cpp`
- Create: `src/test/mvcc_disjoint_progress_test.cpp`
- Modify: `src/transaction/transaction.h`
- Modify: `src/transaction/transaction_manager.h`
- Modify: `src/transaction/transaction_manager.cpp`
- Modify: `src/transaction/index_version_store.{h,cpp}`
- Modify: `src/transaction/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
enum class TxnVisibilityState : uint8_t {
    ACTIVE, VALIDATING, APPLYING, DURABLE, VISIBLE, ABORTED
};

struct TxnControl {
    txn_id_t id;
    timestamp_t start_ts;
    std::atomic<timestamp_t> commit_ts{INVALID_TS};
    std::atomic<TxnVisibilityState> state{TxnVisibilityState::ACTIVE};
    std::mutex serializable_latch;
};

struct RecordKey {
    uint64_t file_id;
    Rid rid;
    bool operator==(const RecordKey &other) const noexcept;
};

struct RecordKeyHash {
    size_t operator()(const RecordKey &key) const noexcept {
        uint64_t rid_bits =
            (static_cast<uint64_t>(static_cast<uint32_t>(key.rid.page_no))
             << 32) |
            static_cast<uint32_t>(key.rid.slot_no);
        size_t seed = std::hash<uint64_t>{}(key.file_id);
        seed ^= std::hash<uint64_t>{}(rid_bits) + 0x9e3779b9 +
                (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct VisibleVersion {
    bool exists = false;
    bool deleted = false;
    std::vector<char> data;
};

class MvccStore {
public:
    bool try_acquire_record_intent(const std::shared_ptr<TxnControl> &txn,
                                   RecordKey key);
    bool try_acquire_unique_intent(const std::shared_ptr<TxnControl> &txn,
                                   int index_id,
                                   std::string_view binary_key);
    VisibleVersion resolve(const std::shared_ptr<TxnControl> &reader,
                           RecordKey key,
                           const RmRecord *physical) const;
    void install_pending(const std::shared_ptr<TxnControl> &txn,
                         const PreparedStorageCommit &commit);
    void publish(const std::shared_ptr<TxnControl> &txn,
                 timestamp_t commit_ts);
    void abort(const std::shared_ptr<TxnControl> &txn) noexcept;
};
```

- [ ] **Step 1: Write deterministic RED tests**

Pause transaction A after it acquires one record intent. Transaction B writing
the same key must return `WRITE_CONFLICT` within 50 ms; B writing a different
shard must finish while A remains paused. Source-contract assertions reject
`mvcc_cv_.wait`, `UniqueIntentShard::cv`, `txn_state_latch_`, and
`commit_apply_latch_` on the SI path.

- [ ] **Step 2: Run RED**

```bash
cmake --build build -j2 --target test_mvcc_fail_fast \
  test_mvcc_disjoint_progress
ctest --test-dir build -R 'test_mvcc_(fail_fast|disjoint_progress)' \
  --output-on-failure
```

Expected: same-key request waits under current wait-die and source contract
finds global coordination.

- [ ] **Step 3: Implement `TxnRegistry` and attach `TxnControl`**

`Transaction` owns its `shared_ptr<TxnControl>`, so normal reads do not look up
a global transaction map. Registry locking is limited to begin/release and
recovery diagnostics. Commit timestamps use `fetch_add(1)`.

- [ ] **Step 4: Implement fail-fast record and unique intents**

Use 256 shards. Under one shard mutex, insert owner if absent; return false if
owned by another ACTIVE/VALIDATING/APPLYING transaction. Never wait or sleep.
Release owned entries on both publish and abort.

- [ ] **Step 5: Publish through owner state instead of global latch**

Install pending versions into touched shards, each carrying the owner
`TxnControl`. Assign commit timestamp to all versions, transition DURABLE after
WAL stabilization, then perform one release-store to VISIBLE. Readers only use
versions whose owner state acquire-load is VISIBLE; before that they reconstruct
the previous value from the version's before image.

- [ ] **Step 6: Isolate SERIALIZABLE metadata**

Keep point/range/table SIREAD data in its own shards. Protect one transaction's
incoming/outgoing dependency sets with `serializable_latch`; keep immediate
current-transaction abort semantics. SI functions must not enter these shards.

- [ ] **Step 7: Run GREEN and SI/SER crash gates**

```bash
ctest --test-dir build -R 'test_(mvcc|batch_visibility|ssi)' --output-on-failure
python3 SQL测试/performance_test/run_acid_tests.py --start-server --quick \
  --crash-check --isolation snapshot --build-dir build \
  --db-dir build/task4_acid_si
python3 SQL测试/performance_test/run_acid_tests.py --start-server --quick \
  --crash-check --isolation serializable --build-dir build \
  --db-dir build/task4_acid_ser
```

- [ ] **Step 8: Run identical paired benchmark and commit**

Require transaction-wait count to be zero, no abandoned regression, and a
repeatable NewOrder improvement before committing:

```bash
git add src/transaction src/test src/CMakeLists.txt
git commit -m "Shard MVCC state and fail fast on write conflicts"
```

---

### Task 5: Remove the B+Tree-wide split lock

**Files:**
- Create: `src/test/ix_disjoint_split_test.cpp`
- Modify: `src/index/ix_index_handle.h`
- Modify: `src/index/ix_index_handle.cpp`
- Modify: `src/common/perf_counters.h`
- Modify: `src/storage/page_guard.h`
- Modify: `src/storage/page_guard.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Produces private methods:

```cpp
std::optional<page_id_t> try_insert_leaf_optimistic(
    const char *key, const Rid &rid, Transaction *txn);
page_id_t insert_with_structural_path(
    const char *key, const Rid &rid, Transaction *txn);
bool leaf_still_owns_key(const IxNodeHandle &leaf, const char *key) const;
```

- Produces counters: `ix_leaf_fast_insert`, `ix_structural_restart`,
  `ix_split`, `ix_structural_active`, `ix_structural_max_active`, and
  `page_content_latch_wait_us`.

- [ ] **Step 1: Write RED concurrency tests**

Build an index with at least four leaves, choose keys in disjoint leaf ranges,
and concurrently force both leaves to split. Require all keys to match a
`std::map`, `ix_structural_max_active >= 2`, and root/page invariants to hold.
Add a fast-path test requiring one leaf write guard and zero ancestor write
guards when the target leaf is safe.

- [ ] **Step 2: Run RED**

```bash
cmake --build build -j2 --target test_ix_disjoint_split
ctest --test-dir build -R test_ix_disjoint_split --output-on-failure
```

Expected: structural max concurrency remains one because `split_latch_`
serializes the index.

- [ ] **Step 3: Instrument page content latch acquisition**

Construct read/write guards with `std::defer_lock`, time only the subsequent
`lock()` call when diagnostics are enabled, and record
`page_content_latch_wait_us`. Keep guard destruction order: unlock content,
then unpin.

- [ ] **Step 4: Implement optimistic safe-leaf insertion**

Traverse with read coupling while retaining only the current guard. Record
leaf page id and generation, release the read guard, acquire one leaf write
guard, and validate leaf type plus lower/upper range. Insert and return if the
leaf has a spare slot; otherwise return `nullopt` for structural restart.

- [ ] **Step 5: Implement local structural restart**

Remove `split_latch_`. Restart from the current root with write coupling.
Release all ancestors whenever the newly locked child is safe. Retain only the
unsafe suffix, split siblings in increasing page-number order, and acquire
`root_latch_` exclusively only to compare-and-install a new root number.
Concurrent validation failure restarts from root.

- [ ] **Step 6: Adapt sorted batch insertion**

Group sorted entries by validated leaf. Hold one leaf write guard for all keys
that fit. Invoke structural restart for only the first key that cannot fit,
then re-locate the remaining suffix.

- [ ] **Step 7: Run GREEN and stress**

```bash
ctest --test-dir build -R 'test_ix_(concurrency|disjoint_split)' \
  --output-on-failure
for i in $(seq 1 20); do build/bin/test_ix_disjoint_split || exit 1; done
```

Expected: all runs pass, disjoint structural concurrency reaches at least two,
and fast inserts do not retain ancestor write guards.

- [ ] **Step 8: Paired benchmark and commit**

Keep only if B+Tree content-latch wait and NewOrder p99/throughput improve with
no completion regression:

```bash
git add src/index src/storage/page_guard.* src/common/perf_counters.h \
  src/test/ix_disjoint_split_test.cpp src/CMakeLists.txt
git commit -m "Replace global B+Tree split locking with local crabbing"
```

---

### Task 6: Apply each Heap page and Index leaf once per commit

**Files:**
- Create: `src/test/storage_commit_batch_test.cpp`
- Modify: `src/transaction/storage_commit_executor.{h,cpp}`
- Modify: `src/record/rm_file_handle.{h,cpp}`
- Modify: `src/index/ix_index_handle.{h,cpp}`
- Modify: `src/recovery/log_manager.{h,cpp}`
- Modify: `src/common/perf_counters.h`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
enum class HeapMutationKind { INSERT, UPDATE, DELETE };

struct HeapMutation {
    HeapMutationKind kind;
    Rid rid;
    std::vector<char> before;
    std::vector<char> after;
};

struct HeapPageMutationBatch {
    int fd;
    page_id_t page_no;
    std::vector<HeapMutation> mutations;
};

enum class IndexMutationKind { INSERT, DELETE };

struct IndexMutation {
    IndexMutationKind kind;
    std::vector<char> key;
    Rid rid;
};

struct IndexLeafMutationBatch {
    int index_fd;
    std::vector<IndexMutation> mutations;
};

void RmFileHandle::apply_page_batch(const HeapPageMutationBatch &batch,
                                    lsn_t page_lsn);
void IxIndexHandle::apply_sorted_batch(
    std::vector<IndexMutation> mutations, Transaction *txn);
```

- [ ] **Step 1: Write RED counter-based test**

Stage 15 inserts targeting two Heap pages and one safe Index leaf. Reset
counters, commit, then require exactly two Heap page write-guard acquisitions,
one Index leaf write-guard acquisition, one WAL append-latch acquisition, and
one transaction write set.

- [ ] **Step 2: Run RED**

```bash
cmake --build build -j2 --target test_storage_commit_batch
ctest --test-dir build -R test_storage_commit_batch --output-on-failure
```

Expected: current per-row paths exceed the guard/WAL acquisition bounds.

- [ ] **Step 3: Group the frozen commit**

After slot reservation, sort Heap mutations by `(fd,page,slot)` and Index
mutations by `(index_fd,key)`. Build row WAL records in logical transaction
order and call `add_logs_to_buffer` once. Assign each page the greatest LSN of
its contained mutations.

- [ ] **Step 4: Apply one Heap guard per page**

`apply_page_batch` validates every before image/slot before changing the first
byte, then applies all bitmap/record/header changes, sets PageLSN once, and
marks dirty once. On validation failure, change no slot.

- [ ] **Step 5: Apply one Index guard per safe leaf**

Use Task 5's sorted leaf batching. Derive Index mutations exclusively from
catalog metadata and before/after records. Preserve old-key history for
key-changing update/delete until the active-snapshot watermark permits GC.

- [ ] **Step 6: Run GREEN, memory, and paired performance checks**

```bash
ctest --test-dir build -R 'test_(storage_commit_batch|batch_insert|heap_page|ix_)' --output-on-failure
/usr/bin/time -v python3 src/test/finals_workload_driver.py \
  --start-server --exploratory --clients 32 --warehouses 5 \
  --reuse-db build/finals_red_small \
  --json-output build/task6_paired.json
```

Require RSS below 8 GiB and fewer page/WAL acquisitions with repeatable
NewOrder improvement.

- [ ] **Step 7: Commit**

```bash
git add src/transaction src/record src/index src/recovery \
  src/common/perf_counters.h src/test/storage_commit_batch_test.cpp \
  src/CMakeLists.txt
git commit -m "Batch committed writes by Heap page and Index leaf"
```

---

### Task 7: Close WAL, failure, and AUTO_ABORT boundaries

**Files:**
- Create: `src/test/commit_failure_injection_test.cpp`
- Create: `src/test/auto_abort_storage_batch_test.cpp`
- Modify: `src/transaction/storage_commit_executor.{h,cpp}`
- Modify: `src/transaction/transaction_manager.{h,cpp}`
- Modify: `src/recovery/log_manager.{h,cpp}`
- Modify: `src/recovery/log_recovery.cpp`
- Modify: `src/network/request_dispatcher.cpp`
- Modify: `src/execution/sql_execution_service.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Produces test-only failure points selected before server construction:

```cpp
enum class CommitFailurePoint {
    BEFORE_HEAP, AFTER_HEAP, AFTER_INDEX, AFTER_COMMIT_WRITE,
    AFTER_COMMIT_SYNC, AFTER_PUBLISH, BEFORE_ACK
};
```

- [ ] **Step 1: Write RED failure-boundary tests**

For each failure point, run one committed candidate and one unrelated committed
transaction, terminate/restart, and require: durable COMMIT is complete;
non-durable work has no partial Heap/Index visibility; `show tables;` becomes
ready within 90 seconds. AUTO_ABORT test requires rollback completion before
the batch failure frame and zero surviving staged writes.

- [ ] **Step 2: Run RED**

```bash
cmake --build build -j2 --target test_commit_failure_injection \
  test_auto_abort_storage_batch
ctest --test-dir build -R 'test_(commit_failure_injection|auto_abort_storage_batch)' \
  --output-on-failure
```

- [ ] **Step 3: Enforce commit ordering**

Require positive row/COMMIT WAL bytes, apply Heap/Index with PageLSN, append
COMMIT, call `force_flush_up_to(commit_lsn)`, verify durable coverage, publish,
then allow ACK. Group followers independently check their own commit LSN.

- [ ] **Step 4: Remove normal abort full flush**

Delete the `flush_for_checkpoint()` path for transactions that have not begun
physical application. Partial-apply failures retain WAL-based undo and never
ACK. Do not add compensation-record formats; use existing redo/undo records
and the durable COMMIT decision already consumed by recovery.

- [ ] **Step 5: Run GREEN plus SIGKILL suites**

```bash
ctest --test-dir build -R 'test_(commit_failure|auto_abort|group_commit|wal_page_lsn|log_manager)' --output-on-failure
python3 SQL测试/performance_test/run_acid_tests.py --start-server --quick \
  --crash-check --isolation snapshot --build-dir build \
  --db-dir build/task7_crash_si
python3 SQL测试/performance_test/run_acid_tests.py --start-server --quick \
  --crash-check --isolation serializable --build-dir build \
  --db-dir build/task7_crash_ser
```

- [ ] **Step 6: Commit**

```bash
git add src/transaction src/recovery src/network src/execution src/test \
  src/CMakeLists.txt
git commit -m "Enforce durable batched commit and cheap pre-apply abort"
```

---

### Task 8: Run final gates, remove dead coordination, and publish

**Files:**
- Modify: current `src/` files only where dead legacy SI paths can be deleted.
- Test artifacts: build-local only; do not commit databases, logs, profiles, or JSON.

**Interfaces:**
- Consumes all prior task interfaces.
- Produces a clean `xin` branch with separate passing commits and recorded
  paired performance evidence.

- [ ] **Step 1: Delete dead SI coordination**

Remove unused `txn_state_latch_`, `mvcc_cv_`, `commit_apply_latch_`,
`phase2_latch_`, `split_latch_`, SI admission queues, eager SI executor writes,
and compatibility adapters with no callers. Run `rg` and require no ranked SI
call site references them.

- [ ] **Step 2: Full build and unit gates**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/bin/unit_test
git diff --check 561480a
```

Expected: zero failures and zero diff-check errors.

- [ ] **Step 3: Full functional/recovery gates**

Run SI and SER quick crash suites, the functional 25 cases, durable COMMIT
audit, AUTO_ABORT tests, and post-crash aggregate/relational validation. Record
commands, elapsed time, and exact pass counts.

- [ ] **Step 4: Formal-shape performance run**

Estimate disk before loading and preserve one reusable full baseline. Then run:

```bash
/usr/bin/time -v python3 src/test/finals_workload_driver.py \
  --start-server --clients 32 --warehouses 50 \
  --data-dir src/test/performance_test/table_data/tpcc_full \
  --rounds 3 --measure 150 --json-output build/finals_final.json
```

Require all transaction types, 50/50 coverage, no durability/recovery failure,
RSS below 8 GiB, materially lower abandoned and p99, and a repeatable median
above the plan's 20,000 NewOrder/min architecture threshold. Do not claim
200,000 without measured evidence.

- [ ] **Step 5: Cleanup generated artifacts**

Resolve every agent-created database/log/profile path, verify it is under
`C:\db2026\build` or the intended generated-data directory, then delete only
artifacts no longer needed. Preserve the final JSON evidence and user-created
baselines. Run `du -sh build/*`, `df -h .`, and `git status --short`.

- [ ] **Step 6: Final commit and push**

```bash
git add src
git commit -m "Complete no-global-wait finals storage architecture"
git push origin xin
```

Report the pushed commit, full test counts, formal median/spread, completion,
abandoned, p50/p99, CPU, RSS, I/O, WAL group size, and any remaining risk.
