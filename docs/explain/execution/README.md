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

SEMI JOIN 不同：找到任意一个右匹配就输出左行，并且下一次直接换下一个左行，不能继续右循环，否则一个左部门有三个员工会输出三次。

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

`Next()` 通常不推进；调用者按 `Next -> nextTuple` 使用。新增 Executor 必须遵守该约定。

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

| 文件 | 主要类/函数 | 可能改题 |
|---|---|---|
| `execution_defs.h` | 执行层公共定义、RecScan | 新扫描统一接口 |
| `execution_common.h` | 执行公共辅助 | 公共记录/值处理 |
| `execution_eval.h` | 条件比较 | LIKE、IN、NULL、新类型 |
| `executor_abstract.h` | 火山模型基类 | 新通用接口，谨慎修改所有子类 |
| `executor_seq_scan.h` | SeqScanExecutor | 新单表过滤、扫描缓存、可见性 |
| `executor_index_scan.h` | IndexScanExecutor | 新范围、反向扫描、联合键 |
| `executor_filter.h` | FilterExecutor | OR/复杂谓词 |
| `executor_projection.h` | ProjectionExecutor | DISTINCT、OFFSET、表达式列 |
| `executor_nestedloop_join.h` | 内连接 | SEMI/简单 JOIN 变化 |
| `executor_extended_join.h` | 外连接扩展 | LEFT/RIGHT/FULL/ANTI 接线 |
| `execution_sort.h` | SortExecutor | 多列排序、TopN、NULL 顺序 |
| `executor_aggregation.h` | AggregationExecutor | 新聚合函数、HAVING |
| `executor_union.h` | UnionExecutor | INTERSECT/EXCEPT/ALL |
| `executor_insert.h` | InsertExecutor | 默认值、列列表、约束 |
| `executor_update.h` | UpdateExecutor | 算术 SET、新表达式 |
| `executor_delete.h` | DeleteExecutor | RETURNING、级联行为 |
| `execution_manager.h/.cpp` | QlManager | DDL/事务/输出命令分发 |

`execution.h` 多为聚合 include；CMakeLists 只在新增 `.cpp` 时需要加入，当前大量 Executor 是 header-only。

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

`fetch_current/fetch_cached_current/fetch_mvcc_current` 是三条数据来源路径。增加过滤语义应尽量复用 `eval_conditions`，不要在三条路径各写一份比较。

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

新增 DISTINCT 最小方案可在投影后构造二进制 key，并维护 `seen_`；注意 LIMIT 应统计不重复记录。

### NestedLoopJoinExecutor

| 变量 | 作用 |
|---|---|
| `left_` | 外表，固定一行 |
| `right_` | 内表，对每个左行重置 |
| `fed_conds_` | 拼接记录上的 Join 条件 |
| `cols_` | 左列+右列，右 offset 平移 |
| `current_rec_` | 当前匹配拼接行 |

`prepare_index_lookup()` 会把左侧等值键传给右侧 IndexScan；`find_match()` 是核心双层循环。

### ExtendedJoinExecutor

[源码](../../src/execution/executor_extended_join.h:1) 为外连接/ANTI 等扩展准备。启用前必须确认 Portal 是否按 JoinPlan.type 构造它；文件存在不代表运行路径已接通。

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

[execution_eval.h](../../src/execution/execution_eval.h:1) 是条件求值公共层。新 CompOp、NULL、LIKE 等比较应集中扩展这里，避免 SeqScan/Filter/Join 各自实现。

## 7. 类似现场改动

### INNER JOIN 改 SEMI/ANTI

保留双层循环。SEMI 在发现首个匹配后输出左行并直接推进左侧；ANTI 扫完右侧无匹配才输出左行。输出 `cols_/len_` 只使用左侧，条件求值仍需临时左右联合 schema。

### 新过滤运算符

若 parser/Analyze 产生新 CompOp，修改统一 eval 函数及所有类型比较；不要修改每个扫描器。

### 新投影行为

若只影响最终列，优先修改 ProjectionPlan/Executor；如果会改变行数，重新审视 LIMIT 与排序的相对位置。

### 批处理大小改变

只调整 IndexScan 批次策略，不改变 `Next/nextTuple` 对外协议。current record 必须在下一次推进前有效。

### UPDATE 新算术

扩展 SetClause 枚举和 UpdateExecutor switch；INT/FLOAT 分支都要覆盖，除法需题目定义零除行为。

## 8. 修改注意

1. `cols_` 的 offset 必须对应当前输出记录，不是原表 offset。
2. `unique_ptr` 返回记录时明确复制还是移动。
3. `beginTuple` 必须能重置执行器状态。
4. Join 右侧每换左行必须重新 beginTuple。
5. 表写、索引写、事务写集三者不能漏一个。
6. MVCC 路径和非 MVCC 路径可能不同，资格赛题若只要求基础事务也不能误改另一条路径。

## 9. AbstractExecutor 接口逐个解释

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

新执行器最常见状态模式：

```cpp
void beginTuple() {
    prev_->beginTuple();
    find_next_valid();
}

void nextTuple() {
    if (is_end_) return;
    prev_->nextTuple();
    find_next_valid();
}
```

## 10. 专题：ANTI JOIN 在现有 NestedLoop 基础上改

需要增加：

```cpp
JoinType join_type_;
std::vector<ColMeta> eval_cols_;
bool left_has_match_ = false;
```

为什么有两套 cols：

```text
eval_cols_ = 左+右，用来读取 ON 条件
cols_      = 左，作为 ANTI JOIN 输出 schema
```

每个左行的算法：

```cpp
while (!left_->is_end()) {
    bool matched = false;
    right_->beginTuple();

    while (!right_->is_end()) {
        joined = join_records(left,right);
        if (eval_conditions(joined, fed_conds_, eval_cols_)) {
            matched = true;
            break;
        }
        right_->nextTuple();
    }

    if (!matched) {
        current_rec_ = left_->Next();
        is_end_ = false;
        return;
    }
    left_->nextTuple();
}
is_end_ = true;
```

输出一个未匹配左行后，nextTuple 必须先推进 left，再继续查；不能从当前 right 位置继续，否则会重复输出。

左表空：begin 后立即 end。右表空：每个左行 matched=false，全部输出。这两个边界自然由循环满足。

## 11. 专题：SEMI JOIN 与 ANTI 的差异只在命中处理

SEMI 在发现第一个匹配时立即输出左行，不继续扫右侧；ANTI 在扫完整个右侧仍未命中时输出左行。两者输出 schema 都仅左侧，且一条左行最多输出一次。

可以共用函数：

```cpp
bool right_has_match_for_current_left();
```

然后按 join_type 判断 `should_emit = matched` 或 `!matched`，减少两套双循环。

## 12. 专题：Projection 中增加 DISTINCT

新增状态：

```cpp
bool is_distinct_ = false;
std::unordered_set<std::string> seen_;
std::unique_ptr<RmRecord> current_;
```

先把原 Next 的列复制提取为 `project_current()`。再写：

```cpp
void advance_to_unique() {
    current_.reset();
    while (!prev_->is_end()) {
        auto candidate = project_current();
        if (!is_distinct_) {
            current_ = std::move(candidate);
            return;
        }
        std::string key(candidate->data, candidate->size);
        if (seen_.insert(key).second) {
            current_ = std::move(candidate);
            return;
        }
        prev_->nextTuple();
    }
}
```

key 构造必须带 size，因为记录中有 `\0`。`beginTuple()` 清 seen_；result_idx_ 只在输出当前唯一记录后加一。

## 13. 专题：增加 OFFSET

SQL `LIMIT 10 OFFSET 5` 可最小附着 Projection：Plan 增加 offset_num_，Executor begin 后先跳过 5 条“最终语义记录”。若与 DISTINCT 组合，OFFSET 应在去重后；若与 ORDER BY 组合，应在排序后。因此更稳妥是独立 LimitExecutor 或放到最上层算子，而不是无条件在 Scan 跳过。

新增变量：

| 变量 | 归属 | 作用 |
|---|---|---|
| `offset_` | Plan/Executor 配置 | 要跳过的结果行数 |
| `skipped_` | Executor 状态 | 已跳过数量 |
| `result_idx_` | Executor 状态 | 已输出数量 |

## 14. 专题：Filter 增加 OR

当前 `vector<Condition>` 默认 AND。仅多加 Condition 无法表达 OR。最小结构可增加条件组：

```text
vector<vector<Condition>> disjunctions
外层任一组满足；内层全部满足
```

或 AST 保留表达式树并递归求值。若题目只要求 `a=1 OR a=2`，可在 Analyze 改写为 IN 列表，但 common/Executor 仍需表达“任一”。不要把 OR 条件 push 到现有 conds 后仍调用全 AND 的 eval_conditions。

## 15. 专题：Aggregation 增加新函数

例如 `COUNT_DISTINCT(col)`：AggState 除 count 外需要每组一个 `unordered_set<规范化值>`。更新阶段把当前列编码成 key，首次出现才 count++。状态属于每个 GroupKey，不能全局共用 seen。

输出类型由函数定义；Analyze 检查参数；AggregatePlan 保存新 AggType；materialize 阶段写入输出记录。

## 16. 专题：写操作的原子顺序

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

## 17. `cols_` 重排实例

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

## 18. 执行器修改自检

```text
begin 能否重复调用并重置
空输入是否直接 end
Next 是否不推进
next 是否恰好推进一次
cols/tupleLen 与返回记录一致
LIMIT/rows_ 统计的是实际输出
右侧 Join 是否对每个左行重置
新分支是否同时考虑 MVCC 与物理路径
```

## 19. QlManager 详细职责

`run_mutli_query` 处理 DDL/多语句管理操作；`run_cmd_utility` 处理事务、help、show、set；`select_from` 消费 Executor 并打印；`run_dml` 调用写 Executor::Next。

如果新增 `SHOW XXX`，通常是 OtherPlan -> run_cmd_utility -> SmManager，没必要建迭代执行器。如果新增返回多行的查询关系代数，才走 Executor。

## 20. 专题：INSERT 指定列列表

语法：`insert into t(a,c) values(1,3)`。Analyze 需要把输入按 TabMeta.cols 重排并补默认值，最终 InsertExecutor 仍收到完整、按表列顺序的 `values_`。这样 Record 编码循环无需修改。

若直接把两个 Value 交给现有 InsertExecutor，它会因 values.size != tab.cols.size 拒绝；即使删检查也会把 c 的值写进 b。

## 21. 专题：DELETE/UPDATE RETURNING

当前写 Executor 通常只执行一次并不输出元组。要 RETURNING，需要在修改前把目标 old/new record 投影并缓存，定义 Next/nextTuple 的多行输出语义，Portal 将语句归类为带结果 DML。它不适合只改 RecordPrinter。

最小限制可规定只返回一列或所有受影响旧行，但必须明确 DELETE 返回旧值、UPDATE 返回新值。
