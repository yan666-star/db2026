# RMDB2025 高吞吐实现审查与实现思路

> 说明：本次审查基于 `D:\DMS-DESIGN\RMDB2025-main\RMDB2025-main` 当前代码。用户未给出明确单文件路径，因此这里按吞吐率相关核心实现做整体审查，重点覆盖查询计划、扫描执行、批量写入、索引、事务可见性、日志和输出路径。

## 结论概览

这个 RMDB 实现吞吐率高，核心不是某一个“神奇函数”，而是几类优化叠加：

1. **计划阶段减少数据访问量**：尽量选择可用索引、做 Filter/Projection 下推，把不必要的行和列挡在执行树下层。
2. **执行阶段批量化**：顺序扫描、索引扫描、批量 load 都尽量以页或批次为单位处理，减少逐条 `fetch/unpin` 和函数调用开销。
3. **索引路径替代全表扫描**：单表查询用最佳前缀索引，Join 可从 Nested Loop 转成 Index Join。
4. **聚合走快路径**：`COUNT` 优先读文件头/页头统计，部分 `MIN/MAX` 可直接从 B+ 树首尾叶子取值。
5. **I/O 写入合并**：load 时一批 1000 行插入，文件头周期性落盘，索引句柄和 key buffer 复用。
6. **MVCC 读写分离思路**：普通读路径尽量直接返回当前记录，只有存在版本链时才进入 undo 链回溯。
7. **可关闭额外输出**：查询结果写 `output.txt` 可关闭，避免评测时文件输出拖慢吞吐。

## 关键代码路径

| 模块 | 文件 | 作用 |
| --- | --- | --- |
| 优化器 | `src/optimizer/planner.cpp` | 选择索引、Filter 下推、Projection 下推 |
| Portal 转执行器 | `src/portal.h` | 将计划节点转为具体 Executor，启用 FastCount、IndexJoin |
| 顺序扫描 | `src/execution/executor_seq_scan.h` | 批量扫描同页 RID，再批量取记录 |
| 索引扫描 | `src/execution/executor_index_scan.h` | 构造索引上下界，按页聚合 RID，批量取记录 |
| 索引 Join | `src/execution/executor_index_join.h` | 用外表记录构造右表索引条件，替代全量 Nested Loop |
| 记录文件 | `src/record/rm_file_handle.cpp` | 批量插入、批量取记录、快速 count |
| 表 load | `src/system/sm_manager.cpp` | CSV 流式解析、1000 行批量写入、批量维护索引 |
| B+ 树 | `src/index/ix_index_handle.cpp/.h`、`src/index/ix_scan.cpp` | lower/upper bound、叶子链扫描、首尾值读取 |
| 输出 | `src/execution/execution_manager.cpp` | 可关闭 `output.txt` 输出，减少非核心 I/O |

## 吞吐率高的主要原因

### 1. 索引选择使用“最佳连续前缀”

`Planner::get_index_cols` 会收集当前表上所有可用于索引的条件：右值是常量、操作符不是 `<>`，并且列属于目标表。随后遍历表上所有索引，按复合索引的连续前缀匹配数量打分，选择匹配列最多的索引。

这带来两个收益：

- 对复合索引 `(c1, c2, c3)`，只有 `c1`、`c1+c2` 这样的连续前缀才用于索引范围，避免错误使用后缀列导致扫描范围失真。
- 计划阶段就把 `SeqScan` 换成 `IndexScan`，后续执行器不用再做昂贵判断。

实现思路：

1. 从条件中筛选可索引列。
2. 遍历表元数据中的所有索引。
3. 对每个索引从第一列开始连续匹配。
4. 选择匹配数最多的索引列集。
5. 构造 `ScanPlan(T_IndexScan, ..., index_col_names)`。

### 2. 索引扫描把范围裁剪和剩余过滤拆开

`IndexScanExecutor` 不是简单地拿每个条件去查索引，而是先区分：

- `use_index_conds_`：能参与索引上下界构造的条件。
- `unuse_index_conds_`：不能安全进入索引范围的条件，留给记录层过滤。

它对复合索引采用常见规则：

1. 前缀列连续等值匹配。
2. 第一处非等值条件用于范围上下界。
3. 后续列条件不再进入 B+ 树范围，而是回表后过滤。

这样既保证正确性，又把 B+ 树扫描范围压到尽量小。

代码中 `build_lower_key` / `build_upper_key` 负责构造范围 key，`lower_bound` / `upper_bound` 得到 B+ 树叶子区间，随后 `IxScan` 顺着叶子链扫描。

额外优化：`check_contradictory_conditions` 会提前识别恒假条件，例如 `x > 10 AND x < 5`、`x = 1 AND x = 2`。这类查询直接标记结束，避免进入存储层。

### 3. RID 按页聚合，回表批量取记录

索引扫描拿到的是 RID。如果每个 RID 单独 `fetch_page -> get_slot -> unpin_page`，吞吐会被缓冲池锁、页查找和函数调用打碎。

当前实现会把 RID 按 `page_no` 聚合：

```cpp
batch_rids_map_[r.page_no].push_back(r);
```

然后对同一页调用一次 `batch_get_records(page_no, rids, context, is_parent)`，在一个 page handle 内取出多个 slot。这样可以显著减少：

- buffer pool 查表次数；
- page pin/unpin 次数；
- 重复页加载；
- MVCC 版本链查询的外层开销。

这也是吞吐率提升最直接的实现点之一。

### 4. 顺序扫描也做了批处理

`SeqScanExecutor` 不再逐条 `get_record`。它通过 `RmScan::get_batch_num()` 或批量 RID 逻辑，一次拿一批同页记录，再统一调用 `batch_get_records`。

顺序扫描高吞吐的原因：

- 位图扫描一次得到多个已占用 slot。
- 同页记录共用一次 page fetch。
- 条件过滤在内存批量记录上完成。
- `Next()` 返回当前缓存记录的拷贝，扫描游标和记录读取解耦。

这让即使没有索引的场景，也能尽量接近顺序页扫描的吞吐。

### 5. Load 路径是专门优化过的批量写入

`SmManager::load_table` 是典型吞吐优化代码：

- 使用 `io::LineReader` 流式读 CSV。
- 每批 `BATCH = 1000` 行。
- 为 1000 行预分配 `char*` record buffer。
- 用 `std::from_chars` / `strtof` 直接解析数值，避免构造大量临时字符串。
- 调用 `fh->batch_insert_record(buf_array, count, nullptr)` 批量写入。
- 索引句柄提前缓存到 `ihs`，每行只构造 key 并插入。
- 最后统一写文件头。

`batch_insert_record` 内部也按页批量找空 slot：

- 用 `Bitmap::next_multi_bit` 一次找多个可用 slot。
- 在一个 page handle 中连续 `memcpy` 多条记录。
- 一次更新页头和文件头计数。
- 页满时才调整 free page 链表并写文件头。

这条链路减少了每行插入时的页查找、元数据落盘、分配和释放成本。

### 6. COUNT 走文件头/页头，不扫全表

`RmFileHandle::fast_count` 是 `COUNT(*)` 高吞吐的关键。

如果事务读时间戳不早于文件最后修改时间，并且文件头的 `last_modify_ts` 有效，直接返回：

```cpp
file_hdr_.num_records
```

否则按 256 页批量预取，并先做页级可见性判断：

- 页面整体对当前事务可见：直接累加 `page_hdr->num_records`。
- 页面不可整体判断：才逐条调用 `get_record` 做 MVCC 可见性检查。

这让常见的无过滤 `COUNT(*)` 从 O(N records) 降到接近 O(1)，或者 O(N pages)。

Portal 中还会识别只有 `COUNT`、无分组、无过滤的聚合，将其包装成 `FastCountExecutor`，再交给 `AggregationExecutor`。

### 7. Index Join 避免全量嵌套循环

普通 Nested Loop Join 是外表每条记录乘以内表扫描。如果右表 Join 列有索引，当前实现会尝试转换为 `IndexJoinExecutor`。

核心思路：

1. 判断左右输入是否是扫描执行器。
2. 检查 Join 条件能否匹配某一侧表的索引。
3. 必要时交换左右执行器，保证右表可用索引。
4. 对外表当前记录提取 Join key。
5. 构造右表索引扫描条件。
6. 只扫描右表匹配范围，再检查剩余 Join 条件。

这样复杂度从近似 `O(N*M)` 降为 `O(N*logM + matches)`，对大表连接吞吐提升很明显。

### 8. Projection 下推减少每条记录搬运字节数

`Planner::apply_projection_pushdown` 会沿着执行树计算每个子树真正需要的列，包括：

- select 输出列；
- Join 条件列；
- Filter 条件列；
- Sort 条件列。

如果扫描节点实际需要的列少于表的全部列，就在扫描上方构造更早的投影节点。`ProjectionExecutor` 对“全字段原顺序”还有快路径，直接返回原记录；只有列数或顺序变化时才分配新记录并 `memcpy` 所需字段。

这对宽表尤其重要：减少内存复制、减少 Join 中间记录宽度、减少输出格式化成本。

### 9. 输出文件开关减少评测 I/O

`execution_manager.cpp` 中 `select_from` 会把结果写入客户端 buffer，也可能写入 `output.txt`。`SetOutputFile` 会调用：

```cpp
planner_->set_write_output_file(false);
```

当评测关注吞吐时，关闭 `output.txt` 可以减少同步文件写入，避免输出成为瓶颈。

## 可复用的实现方案

如果要在其他数据库内核或 RMDB 分支复刻这套高吞吐思路，可以按下面顺序做：

1. **先做批量 record API**
   - 提供 `batch_get_records(page_no, rids)`。
   - 提供 `batch_insert_record(buf_array, count)`。
   - 保证同页只 fetch/unpin 一次。

2. **把 Scan Executor 改成批量游标**
   - `beginTuple` 预取一批记录。
   - `nextTuple` 先消费缓存，缓存空了再取下一批。
   - 条件过滤在批量记录上执行。

3. **实现索引范围裁剪**
   - 选择复合索引最佳连续前缀。
   - 构造 lower/upper key。
   - 非安全条件保留为回表过滤。
   - 恒假条件提前返回空结果。

4. **把索引 RID 按页归并**
   - B+ 树扫描得到 RID 后，不马上回表。
   - 按 `page_no` 分组。
   - 分组后批量回表。

5. **优化 load**
   - CSV 解析避免频繁临时对象。
   - 预分配行 buffer。
   - 每批插入，批后更新索引。
   - 文件头和元数据减少落盘频率。

6. **加入快聚合**
   - `COUNT(*)` 优先读文件头记录数。
   - 页级可见则读页头计数。
   - `MIN/MAX` 在有单列索引时读 B+ 树首/尾叶子。

7. **Join 优先利用索引**
   - 检查 Join 条件是否能转换为右表索引条件。
   - 外表每条记录动态构造右表范围扫描。
   - 保留剩余 Join 条件做最终校验。

## 风险与注意点

1. **批量读取要保持 RID 和 record 对齐**
   - 当前代码多处检查 `tmp_batch_rids_.size() != tmp_batch_recs_.size()`。
   - 如果 MVCC 删除版本导致记录不可见，需要同步移除对应 RID，否则上层 Join/Update/Delete 会错位。

2. **复合索引范围只能用一个非等值列**
   - 后续列的范围条件必须回表过滤。
   - 如果强行放进字典序范围，容易漏结果或多扫大量无效记录。

3. **`fast_count` 依赖时间戳维护正确**
   - `file_hdr_.last_modify_ts`、页时间戳和事务 read_ts 必须一致。
   - 否则直接返回 `file_hdr_.num_records` 可能违反 MVCC 可见性。

4. **批量 load 当前传入 `context = nullptr`**
   - 性能好，但绕过普通事务/MVCC/锁路径。
   - 适合离线导入或评测 load，不适合所有在线并发场景。

5. **索引维护仍是 load 的主要成本**
   - 每条记录仍要对每个索引 `insert_entry`。
   - 如果允许更大改动，可以考虑先建表导入，最后 bulk build index。

6. **关闭输出只能优化评测吞吐，不改变执行引擎本身**
   - 如果客户端必须接收大量结果，网络和格式化仍会成为瓶颈。

## 一句话总结

这份 RMDB 的高吞吐设计可以概括为：**优化器尽量把访问范围变小，执行器把访问方式变批量，存储层减少重复页操作，聚合和 load 走专用快路径；在正确性风险较高的位置，把不安全条件留给回表过滤和 MVCC 可见性检查。**

## 2026 rule-alignment note

This 2025 review is useful as an implementation reference, but it must not be
copied mechanically into the 2026 performance task.

The 2026 clarification says the performance workload uses 16 concurrent
connections running TPC-C transactions, and ranking is gated by correctness:
functional checks, load-data consistency, post-run TPC-C consistency, and
`kill -9` crash recovery for committed transactions. The concurrency-control
bar is essentially ACID with isolation at least as strong as READ COMMITTED:
no dirty reads, durable commits, and correct handling for concurrent writes to
the same row.

Safe ideas to reuse:

- Generic index selection based on continuous composite-index prefixes.
- Index lower/upper bound construction plus residual predicate filtering.
- RID grouping by page and batch record fetches.
- Batch load for offline initial data, as long as initial data remains visible
  to later snapshot reads and indexes/recovery stay consistent.
- String `MIN/MAX` support, but only with the same comparison semantics as the
  normal executor.
- Closing `output.txt` writes through `set output_file off`.

Ideas that require extra proof or should not be copied directly:

- `set output_file off` must not change transaction isolation.
- `fast_count`, page-header count shortcuts, or index `MIN/MAX` shortcuts must
  not bypass MVCC visibility or delete semantics.
- WAL, abort, commit, and checkpoint shortcuts are high risk because 2026
  explicitly checks crash recovery after `kill -9`.
- Offline `load(context=nullptr)` techniques should not be reused for normal
  concurrent DML.
- Any branch based on fixed TPC-C table names, fields, SQL text, or transaction
  names is out of bounds for 2026.
