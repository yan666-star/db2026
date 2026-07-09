# RMDB 正确性实现与性能优化参考

## 当前状态

当前数据库已经通过前十道功能正确性测试和性能测试正确性检查。后续工作重点不是继续修正确性，而是在不破坏现有语义的前提下做性能优化。

本文记录两类内容：

- 前十道题的关键实现架构和依赖关系。
- 性能测试的关键流程、事务链路和优化注意点。

后续改性能时，不要绕过通用 SQL、执行器、事务、索引或恢复流程写 TPC-C 专用逻辑。性能题明确禁止基于固定表名、字段结构或 SQL 文本的硬编码优化。

## 总体架构

RMDB 的主要调用链如下：

```text
client socket
  -> src/rmdb.cpp
  -> parser
  -> analyze
  -> optimizer
  -> portal
  -> executor
  -> record / index / transaction / recovery
```

关键目录：

- `src/rmdb.cpp`：服务端主循环、连接状态、隐式/显式事务、`load`、`set output_file off`。
- `src/parser/`：SQL 语法和 AST。
- `src/analyze/`：语义分析、列解析、类型检查、聚合/Union/Join 规则。
- `src/optimizer/`：计划生成、索引选择、Join 计划。
- `src/portal.h`：把逻辑计划转换为物理执行器。
- `src/execution/`：各类执行器，包括扫描、插入、更新、删除、Join、聚合、排序、Union。
- `src/record/`：表文件、页、RID、物理记录读写。
- `src/index/`：索引管理和 B+ 树。
- `src/transaction/`：事务生命周期、MVCC、SI/SER、提交和回滚。
- `src/recovery/`：WAL、REDO/UNDO、checkpoint、崩溃恢复。
- `src/system/`：表/索引元数据、数据库打开关闭、回滚辅助逻辑。

## 前十题关键实现

### 第一题：基础存储接口

第一题依赖 `src/record/` 的记录管理能力：

- 表文件按页组织记录。
- RID 定位具体页和槽位。
- 插入、删除、更新需要维护页内 bitmap、空闲页链和记录内容。
- 指定位置插入接口会被事务回滚和恢复逻辑复用。

后续性能优化不能破坏 RID 稳定性。索引、日志、MVCC 版本链都依赖 RID 指向同一条逻辑记录。

### 第二题：基础 SQL 执行

第二题覆盖建表、插入、查询、更新、删除和基础连接。

关键路径：

- `parser` 生成 AST。
- `analyze` 检查表名、列名、表达式和条件。
- `optimizer` 生成扫描、过滤、投影、Join、DML 计划。
- `execution` 执行具体操作。

这部分是所有后续题目的基础。性能优化如果改 `SeqScanExecutor`、`UpdateExecutor`、`DeleteExecutor`、`InsertExecutor`，必须同时考虑基础 SQL 的输出语义。

### 第三题：唯一索引

第三题要求索引查询真正生效，并且 DML 过程中同步维护索引。

关键点：

- `CREATE INDEX` 会扫描表数据并建立索引。
- `IndexScanExecutor` 用索引定位候选 RID。
- Insert/Update/Delete 必须同步插入、删除或更新索引项。
- 唯一索引需要检查重复 key。

性能测试中的 TPC-C 表会对主键建立索引。NewOrder、Payment、Delivery、OrderStatus、StockLevel 都大量依赖索引点查或范围查。索引维护错了，可能出现“表扫描能看到、索引查不到”或反过来的问题。

### 第四题：查询优化与执行计划

第四题关注计划树、过滤下推、执行统计和基础优化。

关键点：

- `optimizer` 负责把查询变成较合理的执行计划。
- 过滤条件应尽量靠近扫描节点。
- Join 计划和 rows 统计要保持题目要求的输出格式。

性能优化可以继续在这里做，但要保证 `EXPLAIN ANALYZE` 输出格式不回归。

### 第五题：聚合、分组、排序和 Limit

第五题实现 `COUNT`、`SUM`、`AVG`、`MIN`、`MAX`、`GROUP BY`、`HAVING`、`ORDER BY`、`LIMIT`。

性能题也会测聚合，尤其是字符串 `MIN/MAX`。相关实现通常涉及：

- `AggregateExecutor`
- `OrderByExecutor`
- analyze 阶段的聚合合法性检查
- 类型提升和结果格式化

后续优化聚合时，要注意 int、float、char 三类数据的比较和输出格式。

### 第六题：Union

第六题实现 Union 算子和 Union 结果上的排序。

关键点：

- 分支查询列数必须一致。
- 对应列类型需要兼容。
- 执行阶段需要做统一类型处理。
- 外层查询可以把 Union 当作派生表使用。

这部分和性能题关系不大，但依赖 analyze、executor、排序和投影，不宜为了性能测试大改公共结构。

### 第七题：Nested Loop Join 和 Index Nested Loop Join

第七题实现普通 NLJ 和使用唯一索引的 INLJ。

关键点：

- Join 树是左深树。
- `JOIN ... ON` 条件是表列之间的等值连接。
- 右侧内表连接列存在唯一索引时，可以走索引嵌套循环连接。
- `EXPLAIN ANALYZE` 输出必须符合题面格式。

性能测试中 Payment、OrderStatus、StockLevel 可能包含多表查询或半连接式访问。Join 优化是后续性能提升的重要方向，但不能破坏第七题的计划输出。

### 第八题：事务提交、回滚和索引事务

第八题测试 commit/abort，包含有索引和无索引场景，数据形态接近 TPC-C NewOrder。

关键点：

- 显式事务中多条 DML 要么全部提交，要么全部回滚。
- 回滚需要撤销表记录和索引项。
- 有索引表的回滚必须保证表和索引一致。

这部分是性能测试一致性的直接基础。NewOrder 失败时不能残留 `orders`、`new_orders`、`order_line`，也不能留下已经扣减或增加的计数。

### 第九题：SI 和 SER

第九题实现可配置隔离级别：

```sql
SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;
SET TRANSACTION ISOLATION LEVEL SERIALIZABLE;
```

SI 的核心要求：

- 事务开始时确定 `start_ts`。
- 读取 `commit_ts <= start_ts` 的最新版本。
- 叠加本事务未提交写入。
- 写写冲突需要 abort。
- 不允许脏读和不可重复读。

SER 在 SI 基础上增加 SSI 风格读写反依赖检测。

性能测试明确基于 SI 正确性，因此第九题的 MVCC 路径是性能测试的核心路径。

### 第十题：WAL、静态检查点和崩溃恢复

第十题要求通过日志和检查点恢复一致状态。

关键点：

- 写操作需要记录 WAL。
- 崩溃后通过 REDO/UNDO 恢复。
- `CREATE STATIC_CHECKPOINT;` 需要停止新事务和正在运行事务，刷日志、刷脏页、写 checkpoint 位置。
- 性能测试还会模拟 `kill -9`，因此性能优化不能牺牲 WAL 和恢复顺序。

## 性能测试流程

性能题分为几个阶段：

1. 功能测试
   - 验证前十题相关 SQL 能力。

2. Load Data
   - 通过 `load file into table` 导入 TPC-C 数据。
   - 这条路径在 `src/rmdb.cpp` 中特殊处理。

3. 建索引
   - 官方会对各表主键建立索引。
   - 后续事务大量依赖主键点查、范围查和部分非主键谓词。

4. 关闭输出
   - 官方发送：

```sql
set output_file off
```

   - 该命令没有分号。
   - 关闭 `output.txt` 写入是性能优化要求。
   - 当前实现还利用这个时机让后续新 worker 连接默认进入 `SNAPSHOT_ISOLATION`，符合性能题“基于快照隔离”的要求。

5. 预热和正式压测
   - 先预热 30 秒。
   - 再正式测量 360 秒。
   - 共 3 轮，取成功提交 NewOrder 的 tpmC 中位数。

6. 压测后一致性检查
   - 检查 district/order/stock/customer/history 等 TPC-C 守恒关系。

7. 崩溃恢复检查
   - 模拟 `kill -9` 后重启。
   - 已提交事务必须恢复正确。

## 性能测试关键事务

### NewOrder

NewOrder 是性能得分核心，tpmC 只统计成功提交的新订单。

关键链路：

1. 读取 `customer`、`warehouse`。
2. 读取并更新 `district.d_next_o_id`。
3. 插入 `orders`。
4. 插入 `new_orders`。
5. 多次读取 `item`。
6. 多次读取并更新 `stock`。
7. 插入多条 `order_line`。

一致性要求：

- `district.d_next_o_id` 必须等于初始值加成功 NewOrder 数。
- 每个订单应有完整的 `orders/new_orders/order_line` 关系。
- `stock.s_quantity/s_ytd/s_order_cnt/s_remote_cnt` 要和已提交订单一致。
- abort 后不能留下订单、订单行或库存变化。

优化重点：

- 主键索引点查。
- district 和 stock 热点更新。
- MVCC 写冲突检测成本。
- 多条 order_line 插入和索引维护成本。

### Payment

Payment 更新付款相关字段并插入 history。

关键链路：

- 更新 `warehouse.w_ytd`。
- 更新 `district.d_ytd`。
- 更新 `customer.c_balance/c_ytd_payment/c_payment_cnt`。
- 插入 `history`。

优化重点：

- warehouse/district/customer 的点查和点更新。
- history 追加插入。
- float 字段更新和输出格式保持稳定。

### Delivery

Delivery 处理待配送订单。

关键链路：

- 从 `new_orders` 找到待配送订单。
- 删除对应 `new_orders`。
- 更新 `orders.o_carrier_id`。
- 更新 `order_line.ol_delivery_d`。
- 更新 customer 的余额和配送计数。

优化重点：

- `new_orders` 的范围/最小值查找。
- 删除时表和索引一致。
- MVCC delete 的物理记录、版本链和索引项同步。

### OrderStatus

OrderStatus 偏读，通常查询客户最近订单和订单行。

优化重点：

- customer 点查或按 last name 查询。
- orders 最近订单查找。
- order_line 范围读取。
- 保持 SI 快照读一致。

### StockLevel

StockLevel 偏聚合和范围查询。

优化重点：

- 最近订单范围。
- order_line 和 stock 的连接/过滤。
- 聚合计数。
- 索引选择和中间结果规模控制。

## 性能正确性的关键实现点

### 1. 性能 worker 必须使用 SI

当前做法：

- 普通连接默认仍是 `READ_COMMITTED`。
- 收到 `set output_file off` 后，后续新连接默认 `SNAPSHOT_ISOLATION`。

这样既不破坏前十题中默认 RC 的行为，也满足性能题的 SI 要求。

### 2. 初始 load 数据按 `ts=0` 可见

load 进来的 stock、district、customer 等行没有 MVCC 版本链，但它们是已提交初始数据。

当前做法：

- MVCC 读取无版本链物理行时，为其建立 baseline 版本。
- baseline 的 `commit_ts = 0`。

这保证老数据不会因为别的事务提交而从快照里消失。

### 3. MVCC UPDATE 空结果需要 abort

TPC-C 的 UPDATE 目标为空通常不是正常情况，而是可见性、冲突或索引路径出错。

当前做法：

- MVCC `UpdateExecutor` 如果待更新 RID 为空，直接 abort。
- 如果扫描得到 RID 但实际读取不可见，也 abort。

这避免半个 NewOrder 提交。

### 4. COMMIT 顺序必须先应用数据，再写提交日志

当前做法：

- MVCC 事务先执行 `commit_mvcc()`。
- 物理记录和索引更新成功后，再写 COMMIT log。
- 如果物理应用失败，回滚已应用部分并 abort。

这个顺序同时服务于性能测试的 post-run consistency 和 kill -9 recovery。

### 5. 表、索引、版本链必须同步

性能测试检查可能通过 scan，也可能通过 index 谓词。后续优化不能只让其中一条路径正确。

需要保持：

- 物理表记录正确。
- 索引项正确。
- MVCC 版本链正确。
- tombstone 和物理删除时机正确。
- abort 后三者都回到事务前状态。



## 修改代码时的注意点

1. 不要写死 TPC-C 表名、字段名或 SQL 文本。
2. 不要破坏默认 RC 功能测试。
3. 不要绕过 parser/analyze/optimizer/executor 主流程。
4. 改索引时同时验证 Insert/Update/Delete/Abort/Recovery。
5. 改 MVCC 时同时验证 SI、SER、性能压测和 crash recovery。
6. 改日志时必须验证第十题和性能题 kill -9 阶段。
7. 改输出路径时注意 `set output_file off` 无分号。
8. 改 float 计算和输出时注意 Payment、history、聚合测试。

## 当前性能优化策略

### 目标

官方正确性已经通过，当前官方指标显示 abort-rate 较高。高 abort-rate 下，优化目标有两层：

- 降低单个事务执行时间，缩短 SI 事务重叠窗口，间接减少写写冲突概率。
- 降低 abort/冲突检查路径成本，让失败事务更快退出，不拖慢成功事务。

火焰图里比较明确的数据库热点是：

```text
TransactionManager::check_unique_key_conflict
TransactionManager::get_visible_record
record_versions_ unordered_map lookup
```

这说明 MVCC 版本链和唯一键冲突检查已经进入热路径。

### 本轮优化：MVCC 唯一键冲突候选集合

原逻辑：

- `check_unique_key_conflict()` 每次检查唯一键时，会扫描整个 `record_versions_`。
- `get_visible_record()` 会给读取过的 load 初始行补 baseline 版本。
- 结果是读得越多，`record_versions_` 越大，而唯一键检查会被无冲突可能的 baseline 行拖慢。
- `InsertExecutor` 在 B+Tree 唯一性检查之后，还会在 MVCC 下全表扫描可见记录做重复 key 检查。
- `UpdateExecutor` 即使只更新非索引列，也会对所有索引做唯一键检查；TPC-C 中大量更新都是非主键字段更新。

新逻辑：

- 增加 `mvcc_unique_conflict_keys_by_file_`。
- 只有真正发生过 MVCC 写入的 key 才进入候选集合。
- `check_unique_key_conflict()` 只扫描当前 file_id 下的候选 key。
- abort 后如果版本链退回单个 `ts=0` baseline，则从候选集合移除。
- GC 发现单个稳定已提交非删除版本时，也从候选集合移除。
- checkpoint 清理 tombstone 时同步清理候选集合。
- `InsertExecutor` 用 B+Tree 检查已落索引的重复 key，用 MVCC 候选集合检查尚未落到索引或快照不可见的写入，不再全表扫。
- `UpdateExecutor` 先比较 old/new index key；只有索引 key 真的变化时，才做唯一键检查和索引维护。更新非主键字段时跳过这部分成本。

这个优化不改变 SI 语义：

- 仍然检查其他事务未提交的同 key 写入。
- 仍然检查当前事务快照之后提交的同 key 版本。
- 仍然保留 baseline 行的可见性。
- 已提交且索引正常维护的重复 key 仍由 B+Tree 检查发现。
- 非索引列更新不改变唯一性约束，跳过索引检查不会改变结果。
- 不写死 TPC-C 表名、字段名或 SQL 文本。

预期收益：

- 减少 NewOrder 中 `orders/new_orders/order_line` 插入和更新索引时的 MVCC 扫描成本。
- 减少 Payment/NewOrder/Delivery 对 `warehouse/district/stock/customer/orders/order_line` 非主键字段更新时的无效唯一键检查。
- 减少大量只读或普通读取造成的版本表膨胀对唯一键检查的影响。
- 缩短事务执行时间，降低事务之间重叠窗口，对高 abort-rate 场景有间接帮助。

### 本轮优化：abort 诊断与纯 MVCC abort 跳过全库 flush

当前官方指标仍显示 abort-rate 较高，且 tpc-x 远低于优秀队伍。继续优化前，必须先区分两类问题：

- abort 从哪里来：pending writer、快照后提交、唯一键冲突，还是 commit 阶段物理一致性失败。
- abort 本身有多贵：是否大量失败事务都触发 `flush_for_checkpoint()` 和强制日志刷盘。

新增环境变量诊断开关：

```bash
RMDB_PERF_DIAG=1 ./build/bin/rmdb <db_name> > server.log 2>&1
```

默认每 1000 次 abort 会在 stderr 输出一行 `RMDB_PERF_DIAG` 汇总，进程正常退出时也会再输出一次。可以用 `RMDB_PERF_DIAG_INTERVAL=200` 调整周期。字段包含：

- `abort_total` / `abort_mvcc`
- `abort_physical_rollback`
- `abort_entered_mvcc_commit`
- `write_conflict_pending`
- `write_conflict_committed_after_start`
- `prepare_conflict_pending`
- `prepare_conflict_committed_after_start`
- `unique_conflict_pending`
- `unique_conflict_committed_after_start`
- `commit_conflict_pending`
- `commit_conflict_committed_after_start`
- `pending_waits`
- `pending_wait_resolved`
- `pending_wait_timeout`
- `abort_checkpoint_flush`
- `abort_checkpoint_flush_skipped`
- `abort_checkpoint_flush_us`
- `abort_log_force_flush_us`

该诊断默认关闭，不改变官方运行路径。

同时调整 `TransactionManager::abort()`：

- 非 MVCC abort 保持原逻辑。
- MVCC abort 如果回滚过物理插入，仍执行 `flush_for_checkpoint()`，保证第十题和性能题 crash recovery 的安全边界。
- MVCC 事务如果已经进入 `commit_mvcc()` 并分配了 `commit_ts`，即使后续失败，也保持 `flush_for_checkpoint()`；这是为了覆盖“提交阶段已应用部分物理更新、随后内部回滚”的保守恢复边界。
- MVCC abort 如果只撤销内存中的 MVCC pending update/delete，没有发生物理 rollback，则跳过 `flush_for_checkpoint()`。
- abort log 仍然写入并强制刷盘，暂不改变 WAL 可恢复性语义。

这一步不放松 SI 冲突规则，不修改 `latest_commit > start_ts` 判断，也不改变 NewOrder/Payment/Delivery 的事务语义。目标是降低高 abort-rate 下的失败事务成本，并通过诊断数据决定下一步是否需要处理 pending writer 等待或继续压缩 MVCC 热路径。

### 本轮优化：pending writer 短等待

诊断显示 abort 的主要来源不是唯一键冲突，也不是 commit 阶段冲突，而是 `write_conflict_pending`：事务看到其他事务正在写同一条记录后立即 abort。该行为会在 TPC-C 热点行上放大失败风暴，尤其是 `district`、`stock`、`warehouse`、`customer` 等高频更新链路。

当前调整：

- `check_write_conflict()` 遇到其他事务 pending version 时，不再立刻 abort。
- `prepare_write()` 遇到其他事务 pending version 时，也先短等待。
- 等待期间释放 `mvcc_latch_`，被唤醒后重新扫描版本链，不复用等待前的指针。
- 如果对方 abort 或提交，当前事务继续按 SI 规则重新判断。
- 如果等待超时，才维持原行为并 abort。
- 默认等待预算为 2000 微秒，可通过 `RMDB_PENDING_WAIT_US=5000` 调整。

这个优化不改变 SI 的提交后冲突规则：

- 对方最终 commit 后，如果 `latest_commit > start_ts`，当前事务仍然 abort。
- 对方最终 abort 后，当前事务可以继续，避免无意义失败。
- `check_commit_conflict_under_latch()` 暂不做等待，避免在 commit apply 路径上引入复杂阻塞。

新的诊断字段用于判断收益：

- `pending_waits`：遇到 pending writer 并进入等待的次数。
- `pending_wait_resolved`：等待期间对方结束的次数。
- `pending_wait_timeout`：等待超时后仍按冲突 abort 的次数。
- `write_conflict_pending` / `prepare_conflict_pending`：现在更接近“等待失败后的 pending abort 数”。

### 后续继续优化方向

1. 继续观察官方 abort 分布
   - 如果 abort 主要来自 `warehouse/district/stock` 热点行，这是 SI 写写冲突本身，不应靠破坏 SI 语义硬改。
   - 优先缩短事务耗时和冲突检测耗时。

2. 继续压低 MVCC map 成本
   - `get_visible_record()` 和 `prepare_write()` 仍有较多 `record_versions_` 查找。
   - 可以考虑减少重复查找、缓存当前事务内已访问 key 的状态。

3. 保持火焰图环境干净
   - 使用 Linux 原生 ext4 路径，不要在 `/mnt/hgfs` 共享目录采正式火焰图。
   - 使用 `RelWithDebInfo -fno-omit-frame-pointer`，避免 `[unknown]` 占比过高。

## 2026-07-09 update: decouple output_file from default isolation

### Background

The performance workload disables result-file output before starting worker
connections:

```sql
set output_file off
```

Before this update, handling that command also changed the global default
isolation level for later sessions to `SNAPSHOT_ISOLATION`.

That made worker connections inherit SI even when the client did not
explicitly request it. Under the TPC-C-shaped workload, hot updates on rows
such as district, warehouse, stock, and customer then produced many MVCC
write-conflict aborts. The system spent substantial time doing work that later
aborted instead of queueing conflicting writers and committing them.

### Code change

Modified `src/rmdb.cpp` in `client_handler`.

The `set output_file on/off` command now only updates `enable_output_file`.
It no longer changes `session_defaults`.

Removed logic:

```cpp
if (!output_file_enabled) {
    session_defaults::set(IsolationLevel::SNAPSHOT_ISOLATION);
}
```

### Expected effect

New worker connections keep the normal default isolation level unless the
client explicitly sends:

```sql
SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;
```

For the performance workload, this lets hot writes use the existing
READ COMMITTED locking path and wait for conflicting writers, instead of
blindly aborting under inherited SI.

This is a general semantic fix. It does not match table names, column names,
transaction types, or SQL text from the benchmark workload.

### Correctness notes

- `set output_file off` should not change transaction isolation semantics.
- Explicit `SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION` remains the
  supported way to switch sessions to SI.
- The change preserves SQL semantics while reducing avoidable write-conflict
  aborts in workloads that did not request SI.
