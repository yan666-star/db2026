# Finals No-Global-Wait Storage Redesign

## Outcome

Replace the current eager-write, globally coordinated SI path with a
transaction-private write batch and page-local storage application. The ranked
32-client workload must not queue one transaction behind another transaction,
must not serialize unrelated B+Tree subtrees, and must not turn an ordinary
abort into a database-wide checkpoint flush.

This design preserves Wire v3, SQL syntax, the 4 KiB disk page, heap and B+Tree
disk formats, the WAL record format, AUTO_ABORT, SI, SERIALIZABLE/SSI, FLOAT32,
and SIGKILL recovery. It introduces no table-name, column-name, SQL-text,
statement-id, client-count, or warehouse-count specialization.

## Confirmed Problems

1. `EXEC_BATCH` only batches adjacent operations that reference the same
   prepared INSERT. A NewOrder is still executed as many independent storage
   operations.
2. `txn_state_latch_` coordinates unrelated MVCC records and is held across
   paths that can touch Heap state.
3. wait-die permits an older transaction to wait without a deadline for a
   younger writer.
4. `commit_apply_latch_` is acquired by Heap readers and exclusively by every
   publisher.
5. B+Tree structural modification is serialized by one index-wide
   `split_latch_`.
6. an abort after eager physical INSERT rolls pages back and calls
   `flush_for_checkpoint()`, producing database-wide page writes and fsyncs.
7. the local ranking harness does not use PREPARE_SET/EXEC_BATCH and ignores the
   generated multi-warehouse dataset, so it cannot validate the finals hot
   path.

## Architecture

### 1. TransactionWriteBatch module

The seam between executors and mutable storage becomes one transaction-owned
module:

```cpp
class TransactionWriteBatch {
public:
    TempRowId stage_insert(TableId table, OwnedRecord record);
    void stage_update(RecordKey key, OwnedRecord before, OwnedRecord after);
    void stage_delete(RecordKey key, OwnedRecord before);
    void merge_visible(const ReadDescriptor &read,
                       std::vector<VisibleRow> *stored_rows) const;
    CommitBatch freeze_and_group(const CatalogSnapshot &catalog);
    void discard() noexcept;
};
```

Executors validate types and evaluate expressions, then stage logical writes.
They do not modify Heap pages, B+Tree pages, or append row WAL records during
normal statement execution. A staged insert receives a transaction-local row
identifier; its physical RID is assigned only during commit. Point, range, and
table reads pass a generic `ReadDescriptor` through `merge_visible`, which
adds matching staged inserts, substitutes staged updates, and removes staged
deletes. This preserves own-write visibility without table-specific logic.

`freeze_and_group` sorts existing-row writes by real `RecordKey`, derives
unique-key intents for staged inserts from catalog metadata, and produces the
logical commit batch. The commit-time Heap module reserves insert slots and
resolves transaction-local row identifiers to physical RIDs before WAL records
and Index deltas are finalized. Callers do not manage page guards or index
latches.

### 2. Fail-fast MVCC module

MVCC state is divided into independent modules:

- `TxnRegistry`: lookup by transaction id; each entry has atomic lifecycle,
  start timestamp, commit timestamp, and a private dependency mutex used only
  by SERIALIZABLE metadata.
- `RecordVersionShard[256]`: version chains and record write intents.
- `UniqueIntentShard[256]`: unique index-key intents.
- `SerializableReadShard`: point/range/table SIREAD metadata used only by SER.

SI record and unique-key intents use try-acquire. If another live transaction
owns the intent, the current transaction aborts immediately with
`WRITE_CONFLICT`; it never waits on a transaction condition variable. A shard
latch protects only one in-memory lookup/update and is never held during Heap,
Index, WAL, or disk work.

Readers use immutable committed version entries plus the transaction's private
overlay. Publication changes a transaction entry from DURABLE to VISIBLE with
release semantics after all touched version shards have received the commit
timestamp. There is no global reader/publisher latch.

SER preserves immediate dangerous-structure abort behavior. Its dependency
metadata does not run on the SI ranked path.

### 3. Commit pipeline

Commit proceeds as follows:

1. freeze the private write batch;
2. acquire every record and unique-key intent with fail-fast try-acquire in a
   deterministic key order;
3. validate SI write conflicts and SER dependencies without storage latches;
4. reserve the commit timestamp but do not publish it;
5. reserve Heap slots for staged inserts without setting their visible bitmap
   bits, then resolve transaction-local row identifiers to physical RIDs;
6. finalize Index deltas and append all row WAL records with one WAL-latch
   acquisition;
7. apply Heap batches, one write guard per touched page;
8. apply Index batches, one write guard per touched leaf when no split occurs;
9. append COMMIT WAL and join group commit;
10. require `durable_lsn >= commit_lsn`;
11. publish all versions and mark the transaction VISIBLE;
12. release intents and return COMMIT ACK.

All normal conflicts occur before physical application. Therefore a normal
abort discards the private write batch, releases intents, appends the required
ABORT record, and returns only after AUTO_ABORT rollback completes. It does not
flush all pages or sync every database file.

An unexpected exception after physical application is a separate recovery
failure lane. The existing WAL undo path remains authoritative; the server
must not acknowledge the transaction, and tests inject failures at every
Heap/Index/WAL/publication boundary.

### 4. B+Tree page-local structural modification

Remove the index-wide `split_latch_`.

Point reads use shared child-before-parent latch coupling and retain only the
current page guard. Fast inserts optimistically traverse with read guards, pin
the target leaf once, acquire its write guard, and revalidate the leaf range.
If the leaf is safe, only that leaf is modified.

If the leaf is unsafe, the insert restarts in structural mode. Structural mode
uses parent-before-child write coupling only on the unsafe suffix. A safe child
releases all ancestors. Splits on disjoint parents proceed concurrently; two
operations touching the same page serialize on that page only. Siblings are
latched by increasing page number. The root latch is acquired only while
installing a changed root page number.

The existing on-disk B+Tree layout remains unchanged. Concurrent validation
failure causes a restart, never continued traversal through stale raw pointers.

Sorted index batches locate each target leaf once, insert all fitting keys
under one write guard, and use structural mode only for the leaves that need a
split.

### 5. BufferPool ownership and replacement

Page guards remain mandatory: dereferencing an unpinned frame is unsafe. The
change is one pin per page batch, not removal of pinning.

Heap and Index batch interfaces own their guards internally. No caller may
fetch, return a raw page pointer, unpin, and fetch the same page again for one
logical operation. Mapping/frame latches are released before disk I/O, WAL
durability waits, or content-latch waits. Waiting for the same page's single
in-flight disk read is permitted; waiting behind an unrelated transaction or
unrelated page is not.

Victim selection reserves one frame atomically and performs dirty eviction
outside page-table shards. Replacement metadata may retain exact LRU semantics,
but its latch covers only intrusive-list operations, never I/O or page content.

### 6. Typed batch execution

`EXEC_BATCH` remains ordered and produces the same Wire v3 result ordinals.
Prepared plans are reused. Each operation executes against the current
snapshot plus the transaction overlay; every write is staged in the same
`TransactionWriteBatch` regardless of whether adjacent operations reference
the same statement.

This makes a complete NewOrder one transaction-level storage batch while still
supporting arbitrary prepared statements and operation interleavings. Failure
at operation N discards all staged work before the failure response.

## Verification

### Architecture regressions

- no SI-path condition-variable wait on another transaction;
- disjoint record writers proceed while one writer is paused;
- disjoint B+Tree leaves split concurrently;
- a leaf fast insert never owns an ancestor write guard;
- one typed NewOrder-shaped batch groups Heap and Index work by page/leaf;
- pre-commit abort performs zero `flush_all_pages` and zero database-file
  fsyncs;
- buffer guards restore pin counts and never expose a reused frame.

### Frozen finals gates

- all CTest and `unit_test` tests;
- functional 25 cases and all aggregate/FLOAT32 rules;
- SI and SERIALIZABLE matrices;
- AUTO_ABORT complete rollback before response;
- durable COMMIT audit;
- SIGKILL injection before/after Heap, Index, COMMIT write, fsync,
  publication, and ACK;
- restart readiness and post-crash relational checks;
- peak process-tree RSS below 8 GiB.

### Performance feedback loop

Repair the local harness before accepting any optimization result. It must use
session SI, PREPARE_SET, typed EXEC_BATCH, 32 clients, 50 warehouses, the
45/43/4/4/4 mix, dynamic 5--15 order lines, and three equal measurement
windows. Shortened data/windows are explicitly exploratory.

Record NewOrder/min median and spread, completion, abandoned, p50/p99,
warehouse coverage, CPU, RSS, page-content latch waits, B+Tree restart/split
counts, MVCC intent conflicts, WAL group size, fsync count, and bytes written.

No stage is retained unless paired identical runs show a repeatable gain above
noise without regressing correctness, recovery, completion, abandoned, p99, or
RSS. The first architecture target is above 20,000 NewOrder/min; 200,000 is an
optimization objective, not a promised result without matching hardware
evidence.

## Implementation Order

1. repair the PREPARE_SET/EXEC_BATCH multi-warehouse feedback harness under
   `src/test`;
2. add `TransactionWriteBatch` and own-write overlay tests;
3. replace eager executor writes and remove normal-abort checkpoint flush;
4. replace global MVCC coordination with sharded fail-fast intents;
5. remove B+Tree `split_latch_` and implement optimistic fast insert plus local
   structural restart;
6. deepen Heap/Index batch interfaces so each page is pinned once;
7. batch transaction WAL and verify group commit;
8. run the full frozen finals and performance gates after every stage.

Each stage is a separate commit. A failing correctness, recovery, completion,
abandoned, memory, or paired-performance gate stops the next stage.
