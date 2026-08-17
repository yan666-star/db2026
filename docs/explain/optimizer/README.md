# optimizer 目录详解

## 0. Plan 是“施工图”，不是数据

Query 说“我要查 student 中 score>80 的 name”。Planner 决定：

```text
Projection(name)
  └── Filter(score>80)
       └── SeqScan(student)
```

每个框是 Plan。它只保存参数，不保存扫描游标或真实记录。Portal 后面按这张图创建 Executor。

### 0.1 `Planner::do_planner(query,context)`

| 参数 | 含义 |
|---|---|
| `query` | Analyze 完成的 Query；列、条件已绑定 |
| `context` | 当前事务环境，某些优化/模式可能读取 |
| 返回值 | 整棵 Plan 树的根 shared_ptr |

为什么返回根就够：根通过 `subplan_` 或 `left_/right_` 能找到所有子节点。

### 0.2 `make_one_rel(query)`

名字可以理解为“把 FROM 中所有关系合成一棵关系计划”。

```cpp
std::shared_ptr<Plan> make_one_rel(
    std::shared_ptr<Query> query);
```

输入 Query.tables 和 Query.conds，输出 Scan 或 Join 根。

例子 `A JOIN B JOIN C`：

```text
第一次：table_scan_executors=[ScanA,ScanB,ScanC]
第二步：Join(ScanA,ScanB)
第三步：Join(上一步结果,ScanC)
```

这叫左深树，因为每次新的右孩子都是单表计划。

### 0.3 `get_index_cols(tab_name,curr_conds,index_col_names)`

| 参数 | 传递方式 | 含义 |
|---|---|---|
| `tab_name` | 值 | 为哪张表选索引 |
| `curr_conds` | 值 | 该表单表条件副本 |
| `index_col_names` | 引用输出 | 写入选中的索引列顺序 |
| 返回 bool | 返回值 | true 找到可用索引 |

为什么 index_col_names 是引用：函数既要返回“有没有”，又要返回“是哪一个”。

例子索引 `(dept,id)`，条件 `dept=10 and id>=2`，输出：

```text
return true
index_col_names=[dept,id]
```

只有 `id=2` 时第一列 dept 未约束，最左前缀不成立，通常返回 false。

### 0.4 `generate_select_plan(query,context)`

先调用逻辑/物理优化得到扫描与 Join 树，再根据 has_agg 选择 AggregatePlan 或 ProjectionPlan。

```text
plannerRoot 是不断被“包外层”的变量
原：Scan
包 Filter：Filter(Scan)
包 Projection：Projection(Filter(Scan))
```

`std::move(plannerRoot)` 表示把旧根交给新父节点；随后变量再接住新父节点。

### 0.5 SEMI JOIN 参数怎样到 JoinPlan

```cpp
JoinType type = query->join_types[i - 1];
auto plan = std::make_shared<JoinPlan>(
    T_NestLoop,             // 使用嵌套循环执行器
    left_plan,              // 已经连接好的左树
    right_plan,             // 新右表
    join_conds,             // 本层 ON 条件
    type);                  // SEMI_JOIN
```

每个参数少了都无法执行：无 left/right 没输入，无 conds 不知匹配规则，无 type 会退回 INNER_JOIN。

## 1. 归属和作用

optimizer 不执行记录操作，它决定“用哪些算子、按什么顺序执行”。

```text
Query
 -> Optimizer::plan_query：按语句类型分发
 -> Planner::do_planner：SELECT/DML 计划
 -> Plan 子类组成树
 -> Portal 转为 Executor 树
```

| 文件 | 作用 |
|---|---|
| [optimizer.h](../../src/optimizer/optimizer.h:39) | 总计划入口，DDL/事务/工具命令快速转 Plan |
| [planner.h](../../src/optimizer/planner.h:44) | Planner 接口 |
| [planner.cpp](../../src/optimizer/planner.cpp:27) | 索引选择、谓词下推、Join 树、SELECT 计划 |
| [plan.h](../../src/optimizer/plan.h:35) | 所有 Plan 数据结构 |

`CMakeLists.txt` 负责 planner 目标；当前 Plan/Optimizer 多在头文件，不应把执行状态写进 optimizer.h。

## 2. Plan 所有权

Plan 使用 `shared_ptr`。父 Plan 通过 `subplan_` 或 `left_/right_` 引用子计划，Portal 和 Executor 还可能保存 Plan 裸指针用于 `rows_` 统计。

| 基类变量 | 作用 |
|---|---|
| `tag` | PlanTag，标识节点类型 |
| `rows_` | EXPLAIN ANALYZE 等运行时行数 |

不要让 Plan 保存 `unique_ptr<Executor>`；Plan 是描述，Executor 才是运行对象。

## 3. 各 Plan 变量

### ScanPlan

| 变量 | 来源 | 作用 |
|---|---|---|
| `tab_name_` | Query.tables | 扫描表 |
| `cols_` | TabMeta.cols | 输出 schema |
| `conds_` | 下推条件 | 扫描后过滤 |
| `fed_conds_` | conds 副本 | 执行器实际使用 |
| `len_` | 最后列 offset+len | 记录长度 |
| `index_col_names_` | get_index_cols | 索引列顺序；空为顺扫 |

### FilterPlan

`subplan_` 是输入，`conds_` 是本层过滤条件。它不改变 schema。

### JoinPlan

| 变量 | 作用 |
|---|---|
| `left_` | 已连接的左子树 |
| `right_` | 新加入的右表计划 |
| `conds_` | 本层跨左右输入的条件 |
| `type` | INNER/LEFT/RIGHT/FULL/ANTI；当前活动构造默认 INNER |

### ProjectionPlan

| 变量 | 作用 |
|---|---|
| `subplan_` | 输入计划 |
| `sel_cols_` | 保留列和顺序 |
| `display_all_` | SELECT * 显示标志 |
| `limit_num_` | -1 无限制 |

### SortPlan/AggregatePlan/UnionPlan

- Sort 保存 `sort_cols_` 与每列方向。
- Aggregate 保存 SELECT 聚合项、GROUP BY、HAVING、聚合后排序和 LIMIT。
- Union 保存各分支计划、统一输出 ColMeta 和集合算子。

### DMLPlan/DDLPlan

DMLPlan 将 INSERT 值、UPDATE SET、DELETE/UPDATE 子扫描计划等交给 Portal。DDLPlan 保存表名、列定义或索引列名，QlManager 再调用 SmManager。

## 4. Planner 核心函数

| 函数 | 作用 |
|---|---|
| `get_index_cols` | 根据条件列与最左前缀选索引 |
| `pop_conds` | 从总条件取出单表条件 |
| `push_conds` | 将跨表条件推入合适 Join |
| `logical_optimization` | 逻辑层入口 |
| `physical_optimization` | 物理层入口 |
| `make_one_rel` | 生成扫描、局部投影、左深 Join 树 |
| `generate_sort_plan` | 包 SortPlan |
| `generate_select_plan` | 包聚合或最终 Projection |
| `do_planner` | 按 AST 类型生成最终计划 |

## 5. make_one_rel 关键变量

| 变量 | 归属 | 作用 |
|---|---|---|
| `tables` | 局部，来自 Query | FROM 顺序 |
| `table_proj_cols` | map<table,cols> | 多表局部投影仍需保留的列 |
| `table_scan_executors` | vector<Plan> | 每张表基础计划 |
| `curr_conds` | 当前循环 | 当前表单表条件 |
| `index_exist` | 当前扫描 | SeqScan/IndexScan 选择 |
| `index_col_names` | 当前扫描 | 被选索引列 |
| `conds` | 剩余条件 | 跨表 Join 条件 |
| `joined_tables` | 左深树状态 | 哪些表已进入左子树 |
| `join_conds` | 当前 Join | 连接已加入表和新表的条件 |
| `table_join_executors` | 当前根 | 左深 Join 计划树 |

## 6. 类似现场改动

### 从顺序扫描改成特定条件走索引

只修改 `get_index_cols` 的可用条件和最佳索引评分；保持输出 `index_col_names` 接口不变。Executor 不应重新决定选哪个索引。

### 新 Join 类型

最小链路：Query 保存 `join_types`；JoinPlan 构造函数接收类型；make_one_rel 按第 i 条边传类型；Portal/Executor读取 `plan->type`。当前源码中的 `#if 0` 是提示模板，不是活动代码。

### 改 Join 顺序

当前按 FROM 顺序建左深树。若题目要求“小表先连接”，需要一个表顺序/估计行数策略，但必须同步条件两侧表名和每张表局部计划。不要直接交换 `table_scan_executors` 而不交换 tables。

### 新一元算子

若能附着在 Projection/Filter 上，加字段最小；如果具有独立迭代语义，再加 PlanTag、Plan、Portal 分支、Executor。

### LIMIT 执行位置改变

LIMIT 必须在语义要求的算子之后。例如 DISTINCT 后 LIMIT，不能由下层 Projection 先截断。应明确计划树父子顺序。

## 7. 修改注意

1. Plan 树顺序就是关系代数顺序，父节点后执行。
2. 条件从 `query->conds` 移走后，后续不能再次使用被 move 的内容。
3. 局部 Projection 必须保留 Join 条件涉及列。
4. IndexScan 仍要保存完整条件回表过滤，索引边界只是减少候选。
5. 新 Plan 要同步 Portal 和 EXPLAIN 输出。

## 8. `get_index_cols()` 的逐步判断

它不是“看见条件就选索引”，而是：

1. 从 curr_conds 收集可用于索引的 `lhs_col.col_name`。
2. 遍历 TabMeta.indexes。
3. 对每个索引从 `index.cols[0]` 开始计算连续命中前缀。
4. 第一列未命中则索引不可定位。
5. 选择连续前缀更长的索引。
6. 按 IndexMeta 原始列序输出 index_col_names。

新增 BETWEEN 被展开为 GE+LE 后，salary 会进入可用列集合，IndexScan 再合成上下界，因此 Planner 不需要知道 BETWEEN。

新增 LIKE 时第一版不要将其纳入 available_cols，除非实现了前缀范围到 B+树 key 的转换。

## 9. `pop_conds()` 为什么会修改原数组

输入是 `vector<Condition>& conds`。函数将属于某单表的条件 move 到 `solved_conds` 并从原 vector erase。返回后：

```text
curr_conds = 当前表 WHERE 条件
query->conds 剩余 = 跨表条件或未处理条件
```

因此调试时不能在 make_one_rel 后假设 query->conds 仍含原始全部条件。若新功能还需要完整条件，应在 pop 前复制或用不破坏原数组的新划分结构。

## 10. 专题：ANTI JOIN 的 Planner 精确改法

前提：Query.join_types 已启用，约定 `join_types[i-1]` 表示加入 tables[i] 的连接类型。

原构造：

```cpp
table_join_executors = std::make_shared<JoinPlan>(
    T_NestLoop,
    std::move(table_join_executors),
    std::move(table_scan_executors[i]),
    join_conds);
```

改为：

```cpp
JoinType type = INNER_JOIN;
if (i - 1 < query->join_types.size()) {
    type = query->join_types[i - 1];
}

table_join_executors = std::make_shared<JoinPlan>(
    T_NestLoop,
    std::move(table_join_executors),
    std::move(table_scan_executors[i]),
    join_conds,
    type);
```

JoinPlan 的带类型构造当前在 `#if 0` 示例区，需真正启用或替换现行构造。不能同时保留两个参数完全相同的重载。

ANTI JOIN 仍需要右表 ON 条件列，所以 `table_proj_cols` 收集跨表条件列的逻辑保留；最终根 Projection 才删除右列。

## 11. 专题：CROSS JOIN 为什么 Planner 不改

CROSS JOIN parser 产生两张表、零 ON 条件。make_one_rel 对第二张表得到 `join_conds=[]`，仍创建 JoinPlan。NestedLoopJoinExecutor 的空条件表示所有组合成立。

若你为了“没有条件”跳过 JoinPlan，计划树只会剩一个输入，结果错误。CROSS 的特殊性在条件为空，不在没有连接算子。

## 12. 专题：DISTINCT 与 LIMIT 的计划顺序

错误计划：

```text
Distinct
  -> Projection(limit=2)
```

输入 `[10,10,20]` 会先截成 `[10,10]` 再去重，只输出 1 行。

正确：

```text
Limit(2)
  -> Distinct
       -> Projection(no limit)
```

若为最小改动将 distinct 放 ProjectionExecutor 内，则 Executor 内循环必须跳过重复后才递增 result_idx_。Planner 把 `query->is_distinct` 传入 ProjectionPlan 即可，但需要确认 limit 逻辑已改成统计不同记录。

## 13. 专题：增加 TopN 而不是完整 Sort

题目若要求 `ORDER BY ... LIMIT N` 优化，可增加 TopNPlan/Executor，也可给 SortPlan 加 limit。最小字段：

```cpp
int limit_num_ = -1;
```

Planner 在同时存在 order_bys 和 limit 时把 limit 交给 Sort。Executor 用大小 N 的堆保留最好 N 条。必须保持多列比较顺序与现有 SortExecutor 完全一致，最后输出仍需按 ORDER BY 排好，而不是堆的内部顺序。

## 14. 专题：增加一种扫描 Hint

例如 `/*+ SEQ_SCAN */`：Query 保存 force_seq_scan；make_one_rel 在选择索引前判断：

```cpp
bool index_exist = false;
if (!query->force_seq_scan) {
    index_exist = get_index_cols(...);
}
```

Hint 只影响物理选择，不改变 Condition。不要删除条件或修改结果语义。

## 15. Plan 新字段设计规则

| 情况 | 推荐 |
|---|---|
| 仅布尔行为变化 | 给最接近 Plan 加 bool，默认旧行为 |
| 一元且改变行数/顺序 | 新一元 Plan 或旧算子明确扩展 |
| 二元新关系代数 | Join/SetOp 类型字段或新二元 Plan |
| 需要执行期状态 | 状态放 Executor，Plan 只放配置 |

Plan 中不要保存 `seen_`、cursor、临时记录，这些属于一次执行，不属于计划描述。

## 16. Planner 变量 move 规则

`std::move(plannerRoot)` 后原 shared_ptr 变空，必须使用新返回的父节点继续。`join_conds` move 给 Plan 后不能再读取。若 EXPLAIN 或另一策略仍需它，传值复制或先保留副本。

局部 Projection 对 `proj_cols` 传值即可；根 Projection 用 move 减少复制，但之后 query->cols 可能仍需用于输出列时要确认 Portal 从 Plan 收集还是从 Query 读取。

## 17. `do_planner()` 语句分发

它通过 `dynamic_pointer_cast<ast::...>(query->parse)` 区分 INSERT/UPDATE/DELETE/SELECT，构造 DMLPlan。新增 AST 节点若没有分支，会进入未识别路径。

UPDATE/DELETE 的 `subplan_` 是筛选目标行的扫描树，Portal 先运行它收集 Rid，再构造写执行器。INSERT 没有子扫描，直接携带 values。

## 18. 专题：增加 DELETE LIMIT

语义若是先筛选再仅删除 N 行，可把 limit 放在 DMLPlan，Portal 收集 Rid 时到 N 停止。不要把 limit 放 ScanPlan 后还让同一个 Scan 用于别的语义。变量：`affect_limit_=-1` 属于 DMLPlan 配置；Portal 局部 `rids` 是实际目标。

## 19. 专题：强制 Join 算法

当前 PlanTag 有 T_NestLoop/T_SortMerge。若题目新增 `ENABLE_SORTMERGE`，Optimizer/Planner 读取开关选择 tag，Portal 根据 tag 创建对应 Executor。只修改 tag 而 Portal 仍无 SortMergeExecutor 分支会失败。

算法开关属于计划选择，不应在 NestedLoopExecutor 内再动态切换另一算法。
