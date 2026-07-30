# Finals Wire Protocol and SSI Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the legacy text server with RMDB Wire Protocol v3, implement typed prepared/batch execution and exact SSI current-statement abort behavior, then verify the result with local finals-aligned tooling and documentation.

**Architecture:** `rmdb.cpp` becomes a bootstrap-only entry point. A new `network` library owns handshake, framing, session state and protocol responses; `SqlExecutionService` bridges it to the existing Parser/Analyze/Planner/Portal/Executor pipeline through typed result sinks. SERIALIZABLE dependency tracking remains under the MVCC leaf latch but is encapsulated behind a decision API that always aborts the current statement transaction when a new edge forms a dangerous structure.

**Tech Stack:** C++17, CMake, Flex/Bison, GoogleTest, POSIX sockets, Python 3 integration tests, Rust/Tokio TPCC-Tester.

## Global Constraints

- The official finals Markdown is the highest-level behavioral authority.
- The server supports only Wire Protocol v3; no legacy text mode or same-port auto-detection remains.
- All multi-byte Wire integers use big-endian encoding.
- Handshake is exactly `RMDB`, major `3`, minor `0`, echoed byte-for-byte.
- Frame payload is at most 1 MiB; diagnostics are at most 64 KiB.
- One connection has one outstanding request and preserves request/operation order.
- AUTO_ABORT completes rollback before returning the failure `BATCH_RESULT`.
- SSI dangerous structures abort the transaction executing the current SELECT/INSERT/UPDATE/DELETE immediately, never a different victim and never at COMMIT.
- No service-side TPC-C table name, SQL text, statement-id or benchmark-stage special cases.
- A successful COMMIT response waits for an auditable WAL positive-byte write and stable synchronization covering that write.
- Existing user deletions and unrelated changes remain untouched.
- New text files use UTF-8 and Chinese text is re-read after edits.

---

## File Map

### New server files

- `src/network/wire_protocol.h`: constants, tags, type/status enums and size limits.
- `src/network/wire_codec.h/.cpp`: big-endian reader/writer, frame and typed-cell codec.
- `src/network/socket_io.h/.cpp`: exact socket reads/writes.
- `src/network/prepared_dictionary.h/.cpp`: connection-local atomic prepared dictionary.
- `src/network/response_builder.h/.cpp`: bounded batch and stream response encoding.
- `src/network/connection_session.h/.cpp`: handshake and sequential request state machine.
- `src/network/CMakeLists.txt`: network static library.
- `src/execution/execution_result.h`: output schema, typed value and result-sink interfaces.
- `src/execution/sql_execution_service.h/.cpp`: protocol-independent SQL execution facade.

### Modified server files

- `src/CMakeLists.txt`: build/link network and new tests.
- `src/rmdb.cpp`: retain bootstrap/listener, remove legacy request execution.
- `src/common/context.h`: replace character output members with bindings/result sink.
- `src/common/common.h`: parameter metadata in `Value`.
- `src/parser/ast.h`, `src/parser/lex.l`, `src/parser/yacc.y`: `$n` parameter syntax.
- `src/analyze/analyze.cpp`: declared parameter type validation.
- `src/optimizer/plan.h`, `src/optimizer/planner.cpp`: preserve parameter slots and output metadata.
- `src/portal.h`: create fresh executors with bindings and typed schema.
- `src/execution/execution_manager.h/.cpp`: typed command/query execution.
- `src/execution/executor_*.h`: resolve bound values and register SSI reads consistently.
- `src/transaction/transaction_manager.h/.cpp`: SSI decision and synchronous abort state.
- `src/recovery/log_manager.h/.cpp`: durable COMMIT wait API.
- `rmdb_client/main.cpp`: Wire v3 `EXEC_STREAM` client.

### New tests and documentation

- `src/test/wire_codec_test.cpp`
- `src/test/wire_session_test.cpp`
- `SQL测试/finals_wire/wire_client.py`
- `SQL测试/finals_wire/test_protocol.py`
- `SQL测试/finals_wire/test_ssi_histories.py`
- `SQL测试/finals_wire/test_auto_abort.py`
- `SQL测试/finals_wire/run_finals_quick.py`
- `docs/2026决赛性能测试要求完整解析.md`
- `docs/2026决赛本地测试与配置方法.md`
- `docs/2026决赛要求实现追踪矩阵.md`

---

### Task 1: Wire constants, endian codec and frame validation

**Files:**
- Create: `src/network/wire_protocol.h`
- Create: `src/network/wire_codec.h`
- Create: `src/network/wire_codec.cpp`
- Create: `src/network/CMakeLists.txt`
- Create: `src/test/wire_codec_test.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Produces: `network::WireReader`, `network::WireWriter`, `network::FrameHeader`, `network::decode_header`, `network::encode_frame`.
- Consumes: no database components.

- [ ] **Step 1: Write failing byte-level tests**

```cpp
TEST(WireCodec, HeaderIsBigEndian) {
    WireWriter out;
    out.put_u32(0x01020304);
    out.put_u8(0x20);
    out.put_u8(0);
    out.put_u16(0);
    EXPECT_EQ(out.bytes(), (std::vector<uint8_t>{
        0x01,0x02,0x03,0x04,0x20,0x00,0x00,0x00}));
}

TEST(WireCodec, RejectsTrailingPayload) {
    WireReader in({0x00, 0x01});
    EXPECT_EQ(in.get_u8(), 0);
    EXPECT_THROW(in.require_consumed(), ProtocolError);
}
```

- [ ] **Step 2: Run the focused test and observe the missing symbols**

Run:

```bash
cmake -S . -B build
cmake --build build -j --target test_wire_codec
./build/bin/test_wire_codec
```

Expected: compilation fails because `WireWriter`, `WireReader` and `ProtocolError` do not exist.

- [ ] **Step 3: Implement exact bounded codec**

```cpp
constexpr uint32_t kMaxPayloadBytes = 1024U * 1024U;
constexpr uint32_t kMaxDiagnosticBytes = 64U * 1024U;

struct FrameHeader {
    uint32_t payload_bytes;
    uint8_t tag;
    uint8_t flags;
    uint16_t reserved;
};

class WireReader {
 public:
    explicit WireReader(std::span<const uint8_t> bytes);
    uint8_t get_u8();
    uint16_t get_u16();
    uint32_t get_u32();
    uint64_t get_u64();
    std::span<const uint8_t> get_bytes(size_t count);
    void require_consumed() const;
};
```

Every getter checks remaining bytes before advancing. `decode_header` rejects payload lengths over 1 MiB and nonzero `reserved`.

- [ ] **Step 4: Run codec tests**

Expected: all endian, truncation, length, type and trailing-byte cases pass.

- [ ] **Step 5: Commit the focused change**

```bash
git add src/network src/test/wire_codec_test.cpp src/CMakeLists.txt
git commit -m "Add RMDB Wire v3 byte codec."
```

### Task 2: Exact socket I/O and connection handshake

**Files:**
- Create: `src/network/socket_io.h`
- Create: `src/network/socket_io.cpp`
- Create: `src/test/wire_session_test.cpp`
- Modify: `src/network/CMakeLists.txt`

**Interfaces:**
- Consumes: `FrameHeader` from Task 1.
- Produces: `read_exact(int, void *, size_t)`, `write_all(int, const void *, size_t)`, `perform_server_handshake(int)`.

- [ ] **Step 1: Add socketpair tests with fragmented writes**

```cpp
TEST(SocketIo, ReadExactCombinesFragments) {
    int fd[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fd), 0);
    std::thread sender([&] {
        write(fd[1], "RM", 2);
        write(fd[1], "DB\0\3\0\0", 6);
    });
    std::array<uint8_t, 8> got{};
    ASSERT_TRUE(read_exact(fd[0], got.data(), got.size()));
    EXPECT_EQ(got, kHandshakeV3);
    sender.join();
}
```

- [ ] **Step 2: Run and confirm failure before implementation**

Expected: missing `read_exact`, `write_all`, `kHandshakeV3`.

- [ ] **Step 3: Implement EINTR/EOF/partial-I/O loops and handshake**

```cpp
IoResult read_exact(int fd, void *buffer, size_t length);
IoResult write_all(int fd, const void *buffer, size_t length);
HandshakeResult perform_server_handshake(int fd);
```

Handshake returns `UNSUPPORTED` without invoking Parser and closes at the session layer.

- [ ] **Step 4: Run fragmented handshake tests**

Expected: valid fragmented handshake is echoed; bad magic/version produces no SQL execution.

- [ ] **Step 5: Commit**

```bash
git add src/network/socket_io.* src/test/wire_session_test.cpp src/network/CMakeLists.txt
git commit -m "Implement exact Wire socket I/O and handshake."
```

### Task 3: Typed values, output schema and result sinks

**Files:**
- Create: `src/execution/execution_result.h`
- Modify: `src/common/context.h`
- Modify: `src/execution/execution_manager.h`
- Modify: `src/execution/execution_manager.cpp`
- Modify: `src/portal.h`
- Test: `src/test/wire_codec_test.cpp`

**Interfaces:**
- Produces: `SqlType`, `TypedValue`, `OutputColumn`, `ResultSink`.
- Consumes: executor `ColMeta` and `RmRecord`.

- [ ] **Step 1: Add FLOAT32 bit-preservation and CHAR trimming tests**

```cpp
TEST(TypedValue, PreservesFloatBits) {
    uint32_t bits = 0x3f800001;
    float value;
    std::memcpy(&value, &bits, sizeof(bits));
    EXPECT_EQ(TypedValue::Float(value).float_bits(), bits);
}

TEST(TypedValue, CharDropsStoragePadding) {
    EXPECT_EQ(logical_char_bytes("abc\0\0", 5), "abc");
}
```

- [ ] **Step 2: Run and confirm missing typed-result APIs**

- [ ] **Step 3: Implement the result interfaces**

```cpp
enum class SqlType : uint8_t { INT32 = 0x01, FLOAT32 = 0x02, CHAR = 0x03 };

struct OutputColumn {
    std::string name;
    SqlType type;
};

class ResultSink {
 public:
    virtual void begin_query(const std::vector<OutputColumn> &) = 0;
    virtual void push_row(const std::vector<TypedValue> &) = 0;
    virtual void end_query(uint64_t row_count) = 0;
    virtual void command_ok() = 0;
    virtual ~ResultSink() = default;
};
```

Replace `Context::data_send_` and `offset_` with `ResultSink *result_sink_` and `ExecutionBindings *bindings_`.

- [ ] **Step 4: Convert SELECT and utility output to the sink**

`select_from` reads each executor column by its `ColType`; SHOW/DESC produce typed CHAR/INT rows rather than preformatted tables.

- [ ] **Step 5: Run existing SQL tests plus typed tests**

Expected: queries produce the same logical rows through the sink, FLOAT bits remain unchanged.

- [ ] **Step 6: Commit**

```bash
git add src/common/context.h src/execution src/portal.h src/test/wire_codec_test.cpp
git commit -m "Decouple SQL execution from text output."
```

### Task 4: Parser parameter markers and execution bindings

**Files:**
- Modify: `src/parser/ast.h`
- Modify: `src/parser/lex.l`
- Modify: `src/parser/yacc.y`
- Modify: `src/parser/test_parser.cpp`
- Modify: `src/common/common.h`
- Modify: `src/analyze/analyze.cpp`
- Modify: `src/optimizer/plan.h`
- Modify: affected executor headers

**Interfaces:**
- Produces: `ast::ParamRef`, `Value::is_param`, `Value::param_index`, `ExecutionBindings::resolve`.
- Consumes: declared PREPARE parameter types.

- [ ] **Step 1: Add parser tests**

```cpp
TEST(PrepareSyntax, ParsesDenseMarkers) {
    EXPECT_TRUE(parse_ok("SELECT a FROM t WHERE b=$1 AND c=$2;"));
}

TEST(PrepareSyntax, StringDollarIsLiteral) {
    EXPECT_TRUE(parse_ok("INSERT INTO t VALUES('$1', $1);"));
}
```

- [ ] **Step 2: Run parser test and observe lexer failure on `$`**

- [ ] **Step 3: Add PARAMETER token and AST node**

```cpp
struct ParamRef : public Value {
    uint16_t ordinal;
    explicit ParamRef(uint16_t ordinal_) : ordinal(ordinal_) {}
};
```

Lexer pattern is `"$"{digit}+`; the grammar accepts it wherever a SQL value is valid.

- [ ] **Step 4: Preserve parameter metadata through Analyze and Plan**

```cpp
struct Value {
    bool is_param = false;
    uint16_t param_index = 0;  // zero-based after analysis
    ColType type;
    // existing literal storage follows
};
```

Analyze validates the declared type against the compared/assigned/inserted column and checks that ordinals form exactly `1..parameter_count`.

- [ ] **Step 5: Resolve parameters when constructing runtime executors**

```cpp
Value resolve_value(const Value &value, const ExecutionBindings &bindings);
```

The plan remains a template; each Portal invocation creates new executors and resolved runtime conditions.

- [ ] **Step 6: Run parser and focused DML binding tests**

Expected: INT32/FLOAT32/CHAR values execute without rebuilding SQL text.

- [ ] **Step 7: Commit**

```bash
git add src/parser src/common/common.h src/analyze src/optimizer src/execution
git commit -m "Add typed prepared-statement parameter bindings."
```

### Task 5: Prepared dictionary and PREPARE_SET

**Files:**
- Create: `src/network/prepared_dictionary.h`
- Create: `src/network/prepared_dictionary.cpp`
- Modify: `src/network/wire_codec.*`
- Modify: `src/test/wire_session_test.cpp`

**Interfaces:**
- Produces: `PreparedStatement`, `PreparedDictionary::install_atomically`.
- Consumes: Parser/Analyze/Planner template pipeline and `OutputColumn`.

- [ ] **Step 1: Add atomic replacement tests**

```cpp
TEST(PreparedDictionary, FailedInstallKeepsOldDictionary) {
    PreparedDictionary dict;
    dict.install_atomically(valid_set());
    EXPECT_THROW(dict.install_atomically(set_with_duplicate_id()), ProtocolError);
    EXPECT_NE(dict.find(1), nullptr);
}
```

- [ ] **Step 2: Run and confirm missing implementation**

- [ ] **Step 3: Decode and validate PREPARE_SET**

Validate statement count, nonzero unique IDs, result kind, parameter types, dense markers, SQL length and full payload consumption.

- [ ] **Step 4: Prepare every entry in a temporary dictionary**

```cpp
struct PreparedStatement {
    uint16_t id;
    ResultKind result_kind;
    std::vector<SqlType> parameter_types;
    std::shared_ptr<Plan> plan;
    std::vector<OutputColumn> schema;
    uint64_t schema_generation;
};
```

Only swap the temporary dictionary into the session after every entry succeeds.

- [ ] **Step 5: Encode exact PREPARE_OK order and schema**

- [ ] **Step 6: Run atomicity/schema tests**

- [ ] **Step 7: Commit**

```bash
git add src/network/prepared_dictionary.* src/network/wire_codec.* src/test/wire_session_test.cpp
git commit -m "Implement atomic PREPARE_SET dictionaries."
```

### Task 6: SQL execution service and protocol response builders

**Files:**
- Create: `src/execution/sql_execution_service.h`
- Create: `src/execution/sql_execution_service.cpp`
- Create: `src/network/response_builder.h`
- Create: `src/network/response_builder.cpp`
- Modify: `src/execution/CMakeLists.txt`
- Modify: `src/network/CMakeLists.txt`

**Interfaces:**
- Produces: `SqlExecutionService::execute_stream`, `execute_prepared`, `StreamFrameSink`, `BatchResultBuilder`.
- Consumes: Tasks 3–5.

- [ ] **Step 1: Add response-order tests**

```cpp
TEST(StreamResponse, QueryHasSingleMetaAndEnd) {
    FakeSink sink;
    service.execute_stream("SELECT k FROM t;", session, sink);
    EXPECT_EQ(sink.tags(), (std::vector<uint8_t>{0x01, 0x02, 0x11}));
    EXPECT_EQ(sink.row_count(), 1);
}
```

- [ ] **Step 2: Run and confirm missing facade**

- [ ] **Step 3: Move one-statement Parser/Analyze/Planner/Portal execution out of `rmdb.cpp`**

```cpp
ExecutionTerminal execute_stream(std::string_view sql,
                                 SessionExecutionState &session,
                                 ResultSink &sink);
```

Implicit transactions commit after successful statements and abort synchronously on exceptions.

- [ ] **Step 4: Implement bounded batch result accumulation**

`BatchResultBuilder` rejects any append that would exceed 1 MiB and exposes `discard_results()` for failure paths.

- [ ] **Step 5: Run response and existing SQL tests**

- [ ] **Step 6: Commit**

```bash
git add src/execution src/network/response_builder.* src/network/CMakeLists.txt
git commit -m "Add protocol-independent SQL execution service."
```

### Task 7: EXEC_BATCH, AUTO_ABORT and connection session

**Files:**
- Create: `src/network/connection_session.h`
- Create: `src/network/connection_session.cpp`
- Create: `src/network/request_dispatcher.h`
- Create: `src/network/request_dispatcher.cpp`
- Modify: `src/test/wire_session_test.cpp`

**Interfaces:**
- Produces: `ConnectionSession::run`, exact request handlers.
- Consumes: codec, socket I/O, prepared dictionary and SQL execution service.

- [ ] **Step 1: Add batch failure tests**

```cpp
TEST(ExecBatch, FailureDropsRowsAndRollsBackBeforeReply) {
    auto result = run_batch({begin_op(), query_op(), conflicting_update_op()});
    EXPECT_EQ(result.status, BatchStatus::TRANSACTION_ABORT);
    EXPECT_TRUE(result.rows.empty());
    EXPECT_FALSE(session.has_active_transaction());
    EXPECT_EQ(read_committed_value(), original_value);
}
```

- [ ] **Step 2: Run and confirm failure**

- [ ] **Step 3: Decode the entire batch before execution**

Return top-level ERROR for undecodable payload and clear an existing transaction under AUTO_ABORT.

- [ ] **Step 4: Execute operations in strict order**

On success set `executed_operations == operation_count`, `failed_operation == 0xffff`.
On failure set `failed_operation == executed_operations`, discard results and synchronously abort.

- [ ] **Step 5: Implement the request loop**

```cpp
while (read_frame(fd, &request)) {
    dispatch_one(request, session);
    write_exactly_one_terminal_response(fd, session);
}
```

`EXEC_STREAM` queries are the only multi-frame response.

- [ ] **Step 6: Run fragmented, malformed and AUTO_ABORT tests**

- [ ] **Step 7: Commit**

```bash
git add src/network/connection_session.* src/network/request_dispatcher.* src/test/wire_session_test.cpp
git commit -m "Implement Wire request sessions and AUTO_ABORT."
```

### Task 8: Replace server entry and migrate C++ client

**Files:**
- Modify: `src/rmdb.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `rmdb_client/main.cpp`
- Modify: `rmdb_client/CMakeLists.txt`
- Remove code from: output-file command and text response paths

**Interfaces:**
- Produces: Wire-only server on port 8765 and Wire `EXEC_STREAM` CLI.
- Consumes: `ConnectionSession`.

- [ ] **Step 1: Add an integration smoke test expecting the handshake**

Run a client that sends `RMDB/3.0`, executes `show tables;`, and requires `META...RESULT_END`.

- [ ] **Step 2: Confirm the current server fails the handshake test**

- [ ] **Step 3: Reduce `client_handler` to session construction**

```cpp
void *client_handler(void *arg) {
    std::unique_ptr<ClientConnection> connection(
        static_cast<ClientConnection *>(arg));
    ConnectionSession session(connection->fd, make_execution_dependencies());
    session.run();
    return nullptr;
}
```

Remove legacy one-shot reads/writes, text response buffer, output-file parsing and parser dispatch from `rmdb.cpp`.

- [ ] **Step 4: Rewrite CLI request/response path**

The CLI performs handshake, sends `EXEC_STREAM`, prints typed values for humans, and reads until the unique terminal frame.

- [ ] **Step 5: Build and run Wire `show tables;`**

- [ ] **Step 6: Search for forbidden legacy paths**

Run:

```bash
rg -n "output\\.txt|SET OUTPUT_FILE|enable_output_file|read\\(.*BUFFER_LENGTH" src rmdb_client
```

Expected: no live network/ranking implementation matches.

- [ ] **Step 7: Commit**

```bash
git add src/rmdb.cpp src/CMakeLists.txt rmdb_client
git commit -m "Replace the legacy server with Wire v3."
```

### Task 9: SSI read coverage and prospective write checks

**Files:**
- Modify: `src/transaction/transaction_manager.h`
- Modify: `src/transaction/transaction_manager.cpp`
- Modify: `src/portal.h`
- Modify: `src/execution/executor_seq_scan.h`
- Modify: `src/execution/executor_index_scan.h`
- Modify: `src/execution/executor_insert.h`
- Modify: `src/execution/executor_update.h`
- Modify: `src/execution/executor_delete.h`

**Interfaces:**
- Produces: `SsiDecision register_*`, `SsiDecision check_prospective_write`.
- Consumes: existing MVCC state and logical scan conditions.

- [ ] **Step 1: Add deterministic empty-predicate and UPDATE/DELETE history tests**

The failing expectations require the current modifying statement to abort when it closes the graph.

- [ ] **Step 2: Run and confirm missing UPDATE/DELETE read edges**

- [ ] **Step 3: Register predicates before every SERIALIZABLE scan**

Remove `track_serializable_reads=false` for UPDATE/DELETE. Prevent self-edges by transaction ID rather than by disabling reads.

- [ ] **Step 4: Add prospective write representation**

```cpp
struct ProspectiveWrite {
    RecordKey key;
    const RmRecord *before;
    const RmRecord *after;
    bool deleted;
};
```

Calculate affected readers and decide before installing the new pending version.

- [ ] **Step 5: Run scan-path equivalence tests**

Expected: seq scan and index scan create the same logical SSI outcome.

- [ ] **Step 6: Commit**

```bash
git add src/transaction src/portal.h src/execution/executor_*.h
git commit -m "Track all serializable reads before writes."
```

### Task 10: Fixed SSI victim and synchronous abort

**Files:**
- Modify: `src/transaction/transaction_manager.h`
- Modify: `src/transaction/transaction_manager.cpp`
- Modify: `src/execution/sql_execution_service.cpp`
- Modify: `src/network/request_dispatcher.cpp`
- Test: `SQL测试/finals_wire/test_ssi_histories.py`

**Interfaces:**
- Produces: `SsiDecision::ABORT_CURRENT_TRANSACTION`.
- Consumes: Task 9 dependency candidates.

- [ ] **Step 1: Add two- and three-transaction expected-terminal tests**

```python
assert step(session=2, sql="UPDATE s SET v=1 WHERE k=1") == "TRANSACTION_ABORT"
assert query_committed("SELECT v FROM s WHERE k=1") == original_value
assert execute(session=2, sql="BEGIN;") == "COMMAND_OK"
```

- [ ] **Step 2: Run and capture current wrong victim/timing**

- [ ] **Step 3: Encapsulate edge insertion and danger evaluation**

```cpp
SsiDecision add_edge_and_check(txn_id_t reader,
                               txn_id_t writer,
                               txn_id_t current_statement_txn);
```

Use the two-transaction cycle or committed-`T_out` rule from the design. The function never chooses another victim.

- [ ] **Step 4: Abort outside the MVCC leaf latch**

Mark `ABORTING`, release the latch, call the unified transaction abort, clear session transaction state, then create the protocol terminal.

- [ ] **Step 5: Run all deterministic SSI histories**

Expected: operation, victim, terminal frame and rollback verification match every scenario.

- [ ] **Step 6: Commit**

```bash
git add src/transaction src/execution/sql_execution_service.cpp src/network/request_dispatcher.cpp SQL测试/finals_wire/test_ssi_histories.py
git commit -m "Enforce immediate current-statement SSI aborts."
```

### Task 11: COUNT(DISTINCT) and FLOAT32 finals gates

**Files:**
- Modify: `src/parser/ast.h`
- Modify: `src/parser/lex.l`
- Modify: `src/parser/yacc.y`
- Modify: `src/common/common.h`
- Modify: `src/analyze/analyze.cpp`
- Modify: `src/optimizer/plan.h`
- Modify: `src/execution/executor_aggregation.h`
- Test: `SQL测试/finals_wire/test_protocol.py`

**Interfaces:**
- Produces: `AggExpr::is_distinct`, exact FLOAT32 aggregate result.
- Consumes: typed output path.

- [ ] **Step 1: Add failing SQL tests**

```sql
SELECT COUNT(DISTINCT s_i_id) FROM stock;
SELECT COUNT(DISTINCT(s_i_id)) FROM stock;
SELECT SUM(amount) FROM float_probe;
```

Expected values include join fan-out deduplication and raw FLOAT32 result bits.

- [ ] **Step 2: Run and confirm DISTINCT parse failure**

- [ ] **Step 3: Add DISTINCT grammar and plan flag**

Only COUNT(DISTINCT column) is required. Reject DISTINCT `*` and multiple arguments.

- [ ] **Step 4: Implement per-group distinct sets**

Use a typed key preserving INT32, FLOAT32 bit identity and logical CHAR bytes. COUNT returns INT32.

- [ ] **Step 5: Audit FLOAT arithmetic**

Each relative update rounds once to `float`; SUM converts stored float inputs to `double`, accumulates in double and casts once to float.

- [ ] **Step 6: Run typed bit-pattern tests**

- [ ] **Step 7: Commit**

```bash
git add src/parser src/common/common.h src/analyze src/optimizer src/execution/executor_aggregation.h SQL测试/finals_wire/test_protocol.py
git commit -m "Support finals DISTINCT and FLOAT32 semantics."
```

### Task 12: Auditable durable COMMIT

**Files:**
- Modify: `src/recovery/log_manager.h`
- Modify: `src/recovery/log_manager.cpp`
- Modify: `src/transaction/transaction_manager.cpp`
- Test: `SQL测试/finals_wire/test_auto_abort.py`

**Interfaces:**
- Produces: `LogManager::flush_until_durable(lsn_t)`.
- Consumes: commit record LSN.

- [ ] **Step 1: Add crash/ACK ordering probe**

The probe commits a unique row, waits for `COMMAND_OK`, sends SIGKILL, restarts and requires the row to exist.

- [ ] **Step 2: Confirm current non-forced flush is insufficient**

Inspect syscall/log evidence and record the failure or missing force-sync proof.

- [ ] **Step 3: Implement a correctness-first durable wait**

```cpp
lsn_t commit_lsn = log_manager->add_log_to_buffer(&commit_record);
log_manager->flush_until_durable(commit_lsn);
```

`flush_until_durable` guarantees a positive-byte WAL write and fsync/fdatasync covering the target LSN before return.

- [ ] **Step 4: Run kill-after-ACK recovery test**

- [ ] **Step 5: Add group-commit coalescing without changing ACK boundary**

Concurrent waiters share a leader flush; every waiter resumes only when `durable_lsn >= target_lsn`.

- [ ] **Step 6: Re-run durability and concurrent commit tests**

- [ ] **Step 7: Commit**

```bash
git add src/recovery/log_manager.* src/transaction/transaction_manager.cpp SQL测试/finals_wire/test_auto_abort.py
git commit -m "Make COMMIT ACK wait for durable WAL."
```

### Task 13: Python Wire conformance and quick-finals runner

**Files:**
- Create: `SQL测试/finals_wire/wire_client.py`
- Create: `SQL测试/finals_wire/test_protocol.py`
- Create: `SQL测试/finals_wire/test_auto_abort.py`
- Create: `SQL测试/finals_wire/test_ssi_histories.py`
- Create: `SQL测试/finals_wire/run_finals_quick.py`

**Interfaces:**
- Produces: independent Wire v3 client and deterministic runners.
- Consumes: public finals byte contract only.

- [ ] **Step 1: Implement independent frame fixtures**

```python
HANDSHAKE = b"RMDB" + struct.pack(">HH", 3, 0)
HEADER = struct.Struct(">IBBH")
```

The client reads exact bytes, decodes typed rows and rejects ordering/count violations.

- [ ] **Step 2: Add protocol-negative cases**

Send fragmented headers, bad reserved, unknown tags, truncated typed cells and 1 MiB boundary payloads.

- [ ] **Step 3: Add quick end-to-end sequence**

Run handshake, `show tables;`, isolation configuration, CRUD, aggregates, prepare, batch, AUTO_ABORT and SSI histories.

- [ ] **Step 4: Run against a local server**

Expected: every case prints PASS and the process exits 0.

- [ ] **Step 5: Commit**

```bash
git add SQL测试/finals_wire
git commit -m "Add independent finals Wire integration tests."
```

### Task 14: Migrate TPCC-Tester as an external Wire v3 driver

**Files in `D:\DMS-DESIGN\TPCC-Tester-main`:**
- Create: `src/protocol/*`
- Create: `src/workload/*`
- Create: `src/engine/*`
- Create: `src/txn/*`
- Modify: `src/main.rs`
- Modify: `src/config.rs`
- Modify: `src/loader.rs`
- Modify: `src/checker.rs`
- Remove usage of: `src/connection/*`, text cursor substitution.

**Interfaces:**
- Produces: Rust Wire v3 `Session` and quick/finals profiles.
- Consumes: official public protocol and the server under test.

- [ ] **Step 1: Add Rust protocol unit tests with the same literal byte fixtures**

```rust
assert_eq!(
    encode_header(4, Tag::ExecStream, 0),
    [0,0,0,4,0x20,0,0,0]
);
```

- [ ] **Step 2: Replace text connection with `protocol::Session`**

Expose `exec_stream`, `prepare_set` and `exec_batch`; no `legacy` option remains.

- [ ] **Step 3: Migrate init/stats/check to typed EXEC_STREAM**

Record the exact generated `order_line` count instead of assuming ten lines per order.

- [ ] **Step 4: Migrate the five transactions to prepared batches**

Each transaction consumes immutable `TxnInput`; retries reuse it and do not increment the routing transaction number.

- [ ] **Step 5: Replace fixed-work execution with quick/finals timed windows**

`finals` fixes 50 warehouses, 32 clients, 30-second warmup and three consecutive 150-second windows.

- [ ] **Step 6: Add result classification and coverage gates**

Track committed, expected rollback, retryable abort, fatal and abandoned separately.

- [ ] **Step 7: Run Rust unit tests and quick profile against db2026**

Expected: protocol tests pass, load/check pass, each transaction family commits, and three window results are reported.

The external directory has no `.git`; save command output and file checksums as evidence instead of claiming a Git commit.

### Task 15: Detailed finals documentation and traceability

**Files:**
- Create: `docs/2026决赛性能测试要求完整解析.md`
- Create: `docs/2026决赛本地测试与配置方法.md`
- Create: `docs/2026决赛要求实现追踪矩阵.md`
- Modify: `docs/性能题目与要求.md`

**Interfaces:**
- Produces: teacher-facing explanation, exact commands and final status matrix.
- Consumes: all implementation and test evidence.

- [ ] **Step 1: Write the requirements document**

Cover protocol bytes, prepared/batch state, SI/SER, fixed SSI victim, FLOAT32, DISTINCT, data/load, workload, scoring, WAL audit, recovery, red lines and throughput implications.

- [ ] **Step 2: Write the testing/configuration document**

Give Windows/WSL/Linux build commands, server startup, Wire diagnose, quick profile, finals profile, SSI history, AUTO_ABORT, SIGKILL, recovery and evidence collection.

- [ ] **Step 3: Write the traceability matrix**

Each row contains requirement, source location, test location, current status, latest evidence and remaining risk.

- [ ] **Step 4: Mark the old performance document as initial-round historical material**

State that `output.txt` and `SET OUTPUT_FILE OFF` are not finals ranking paths.

- [ ] **Step 5: Run document checks**

Search for stale claims, output-file recommendations, placeholders and broken local links; re-read representative Chinese lines.

- [ ] **Step 6: Commit**

```bash
git add docs
git commit -m "Document finals requirements and local validation."
```

### Task 16: Final verification and finals gap audit

**Files:**
- Update: `docs/2026决赛要求实现追踪矩阵.md`

**Interfaces:**
- Produces: final verified status.
- Consumes: every previous task.

- [ ] **Step 1: Run source and build checks**

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

- [ ] **Step 2: Run Wire/SSI quick integration**

```bash
python SQL测试/finals_wire/run_finals_quick.py --start-server
```

- [ ] **Step 3: Run generic ACID and recovery checks through Wire**

Require rollback atomicity, dirty-read exclusion, write conflict, phantom semantics and kill-after-ACK durability.

- [ ] **Step 4: Run TPCC-Tester quick profile**

Load a small database, execute all transaction families, run checker and save metrics.

- [ ] **Step 5: Run Linux-only finals checks when the environment supports them**

Run SIGKILL/restart, fsync syscall evidence, and the full 50-warehouse/32-client/30+3×150 profile.

- [ ] **Step 6: Re-audit every public finals requirement**

Set matrix status only to:

```text
PASS
FAIL
NOT IMPLEMENTED
NOT RUN IN CURRENT ENVIRONMENT
```

Never infer PASS from source inspection alone.

- [ ] **Step 7: Commit the final evidence update**

```bash
git add docs/2026决赛要求实现追踪矩阵.md
git commit -m "Record finals validation evidence."
```
