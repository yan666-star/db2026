# execution 目录详解

## 0. Executor 像一个可暂停的结果生成器

它不是一次返回所有结果，而是每次准备一行：

```cpp
exec->beginTuple();
while (!exec->is_end()) {
    auto row = exec->Next();
    // 使用当前 row
    exec->nextTuple();
}
```

四个函数顺序不能混：begin 找第一行；Next 读取当前行；next 才移动；is_end 判断有没有当前行。

### 0.1 `NestedLoopJoinExecutor` 构造函数每个参数

```cpp
NestedLoopJoinExecutor(
    std::unique_ptr<AbstractExecutor> left,
    std::unique_ptr<AbstractExecutor> right,
    std::vector<Condition> conds,
    JoinPlan *plan = nullptr);
```

| 参数 | 为什么这样传 | 例子 |
|---|---|---|
| `left` | unique_ptr，Join 从此唯一拥有左执行器 | departments Scan |
| `right` | unique_ptr，唯一拥有右执行器 | employees Scan |
| `conds` | 值传递后 move 到成员 | departments.id=employees.dept_id |
| `plan` | 借用裸指针，不释放 | 读取 JoinType、回填 rows_ |

构造函数把右侧列 offset 全部加 `left_->tupleLen()`。例如左记录 24 字节，右侧 emp_id 原 offset0，拼接后 offset 必须变24。

### 0.2 `join_records(left_rec,right_rec)`

```cpp
auto joined = std::make_unique<RmRecord>(len_);
memcpy(joined->data, left_rec.data, left_->tupleLen());
memcpy(joined->data + left_->tupleLen(),
       right_rec.data, right_->tupleLen());
```

第一个 memcpy 把左记录放 `[0,left_len)`；第二个从 `left_len` 开始放右记录。`len_=left_len+right_len`。

### 0.3 `find_match()` 为什么是两个 while

外层遍历每个左行，内层为当前左行遍历所有右行。INNER JOIN 每发现一个匹配就暂停并返回，下一次 nextTuple 从当前右侧下一行继续。

### 0.4 `ProjectionExecutor(prev,sel_cols,plan)` 参数

| 参数 | 含义 |
|---|---|
| `prev` | 下层记录来源，可能 Scan/Join/Filter |
| `sel_cols` | 用户最终选择的列及顺序 |
| `plan` | limit、rows 等配置 |

构造时 `get_col(prev_cols,sel_col)` 找列位置，把下标放 `sel_idxs_`，再创建新的 cols_ offset。执行 Next 时按下标复制字节。

### 0.5 `SeqScanExecutor` 中 `rid_` 和 `current_rec_`

`rid_` 是记录位置；`current_rec_` 是记录内容，两者必须指向同一行。UPDATE/DELETE 需要 rid，SELECT 需要 record。fetch_current 每找到满足条件的一行同时更新二者。

### 0.6 为什么 Executor 保存 Plan 裸指针是借用

Plan 树由 shared_ptr 在 PortalStmt 中存活，Executor 不拥有它，只读取配置/增加 rows_。Executor 析构时不能 delete plan_，否则 shared_ptr 后续会再次释放。

## 1. 归属和迭代协议

Executor 是真正运行计划的对象。统一火山模型接口位于 [executor_abstract.h](../../src/execution/executor_abstract.h:45)：

```text
beginTuple()  定位第一条
is_end()      是否结束
Next()        返回当前记录
nextTuple()   推进到下一条
cols()        输出列布局
tupleLen()    输出记录字节数
```

`Next()` 通常不推进；调用者按 `Next -> nextTuple` 使用。

## 2. 所有权

父 Executor 用 `unique_ptr<AbstractExecutor>` 独占子执行器。`Context*`、`SmManager*`、Plan 裸指针均为借用，不负责释放。

通用变量：

| 变量 | 作用 |
|---|---|
| `prev_` | 一元算子子执行器 |
| `left_/right_` | 二元算子输入 |
| `cols_` | 当前输出 schema |
| `len_` | 当前输出记录长度 |
| `current_rec_` | 当前已定位记录 |
| `is_end_` | 当前迭代状态 |
| `plan_` | 回填 rows_ 或读取配置 |
| `context_` | 当前事务、日志、锁、输出环境 |

## 2.1 文件级地图

| 文件 | 主要类/函数 |
|---|---|
| `execution_defs.h` | 执行层公共定义、RecScan |
| `execution_common.h` | 执行公共辅助 |
| `execution_eval.h` | 条件比较 |
| `executor_abstract.h` | 火山模型基类 |
| `executor_seq_scan.h` | SeqScanExecutor |
| `executor_index_scan.h` | IndexScanExecutor |
| `executor_filter.h` | FilterExecutor |
| `executor_projection.h` | ProjectionExecutor |
| `executor_nestedloop_join.h` | 内连接 |
| `execution_sort.h` | SortExecutor |
| `executor_aggregation.h` | AggregationExecutor |
| `executor_union.h` | UnionExecutor |
| `executor_insert.h` | InsertExecutor |
| `executor_update.h` | UpdateExecutor |
| `executor_delete.h` | DeleteExecutor |
| `execution_manager.h/.cpp` | QlManager |

`execution.h` 多为聚合 include；当前大量 Executor 是 header-only。

## 3. 扫描执行器

### SeqScanExecutor

[源码](../../src/execution/executor_seq_scan.h:41)

| 变量 | 归属/作用 |
|---|---|
| `tab_name_` | 扫描目标 |
| `fh_` | 表 RmFileHandle 借用指针 |
| `cols_` | 整表物理列布局 |
| `fed_conds_` | 实际谓词 |
| `scan_` | 普通 RmScan |
| `rid_` | 当前物理位置 |
| `equality_rids_` | 整数等值缓存候选 |
| `mvcc_rids_` | MVCC 模式全槽位 |
| `current_rec_` | 当前可见且满足条件记录 |

`fetch_current/fetch_cached_current/fetch_mvcc_current` 是三条数据来源路径。

### IndexScanExecutor

[源码](../../src/execution/executor_index_scan.h:45)

| 变量 | 作用 |
|---|---|
| `index_col_names_` | Planner 选择的索引列 |
| `index_meta_` | 联合索引物理布局 |
| `ih_` | B+树句柄 |
| `col2conds_` | 每个索引列的条件集合 |
| `lower_key/upper_key` | beginTuple 局部边界键 |
| `batch_rids_map_` | page_no 到 Rid，按页批量回表 |
| `batch_recs_/batch_rids_` | 已过滤的当前批次 |
| `lookup_key_` | Join 内表等值探测键 |

联合索引按索引列顺序构造键；等值前缀后可接一个范围。无论边界多精确，`conds_` 仍需最终过滤。

## 4. 中间算子

### FilterExecutor

不改变 schema。`fetch_next()` 循环推进 prev_，直到 `eval_conditions` 成立，把记录复制到 `current_`。

### ProjectionExecutor

| 变量 | 作用 |
|---|---|
| `sel_idxs_` | 选择列在 prev_cols 中的下标 |
| `cols_` | offset 从 0 重新排列后的输出布局 |
| `is_sel_all_` | 能否直接返回子记录而无需复制 |
| `limit_` | 最大输出数 |
| `result_idx_` | 已输出数量 |

### NestedLoopJoinExecutor

| 变量 | 作用 |
|---|---|
| `left_` | 外表，固定一行 |
| `right_` | 内表，对每个左行重置 |
| `fed_conds_` | 拼接记录上的 Join 条件 |
| `cols_` | 左列+右列，右 offset 平移 |
| `current_rec_` | 当前匹配拼接行 |

`prepare_index_lookup()` 会把左侧等值键传给右侧 IndexScan；`find_match()` 是核心双层循环。

### Sort/Aggregation/Union

- Sort 物化全部输入到 `tuples_`，stable_sort 后用 cursor 输出。
- Aggregation 以 GroupKey 映射 AggState，完成 HAVING、排序、LIMIT。
- Union 物化各分支、统一类型和记录布局，再执行集合或多重集运算。

## 5. 写执行器

### InsertExecutor

关键变量：`tab_` 元数据、`values_` 输入、`fh_` 表句柄、`rid_` 新位置。顺序是构造记录、唯一性预查、表/MVCC写入、写集登记、索引维护。

### UpdateExecutor

`rids_` 是 Portal 预扫描出的目标；`old_rec` 用于日志/回滚/旧索引键；`rec_new` 是新记录；`set_clauses_` 决定字段更新。更新索引时必须比较旧键和新键。

### DeleteExecutor

先取得 `old_rec`，再做 MVCC 删除或物理删除；写集保存旧记录；物理路径同步删除所有索引项。

## 6. execution_eval

[execution_eval.h](../../src/execution/execution_eval.h:1) 是条件求值公共层。

## 7. AbstractExecutor 接口逐个解释

| 接口 | 调用时机 | 必须保证 |
|---|---|---|
| `beginTuple()` | 一次扫描开始/Join 重置右侧 | 清旧状态并定位首条 |
| `nextTuple()` | 当前行被消费后 | 定位下一条，不返回数据 |
| `Next()` | 读取当前行 | 不擅自推进；返回独立可用记录 |
| `is_end()` | 循环条件 | 与 current 是否存在一致 |
| `cols()` | 构造父执行器时 | offset 对应本层输出 |
| `tupleLen()` | 拼接/分配记录 | 等于输出记录字节数 |
| `rid()` | UPDATE/DELETE 扫描 | 返回当前基础记录位置；中间算子可能不能提供 |
| `set_index_lookup()` | Join 内表探测 | 能下传时继续委托子执行器 |

## 8. 写操作的原子顺序

以 UPDATE 非 MVCC 路径：

```text
读取 old_rec
 -> 计算 rec_new
 -> 预检查所有唯一键
 -> 写日志/写集
 -> 删除旧索引项
 -> 更新表记录
 -> 插入新索引项
```

若在更新表后才发现唯一冲突，当前语句已经部分修改。因此所有可能失败的唯一性检查应尽量放写表之前。

## 9. `cols_` 重排实例

原表：

```text
id offset=0 len=4
name offset=4 len=20
score offset=24 len=4
```

`SELECT score,id` 的 Projection 输出：

```text
score offset=0 len=4
id    offset=4 len=4
len_=8
sel_idxs_=[2,0]
```

父 Sort/Printer 必须使用新 cols_，不能继续用原表 offset。

## 10. QlManager 详细职责

`run_mutli_query` 处理 DDL/多语句管理操作；`run_cmd_utility` 处理事务、help、show、set；`select_from` 消费 Executor 并打印；`run_dml` 调用写 Executor::Next。

