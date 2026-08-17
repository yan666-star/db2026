# `planner.cpp` 详细讲解

源码入口：[planner.cpp](../../src/optimizer/planner.cpp:23)

这个文件是 Planner 层的**实现**。它决定"用什么算子、怎么拼成一棵树"。主路径：

```text
do_planner → (SELECT) generate_select_plan → logical_optimization
           → physical_optimization → make_one_rel → [AggregatePlan | ProjectionPlan]
```

## 〇、核心思想：一棵不断"包外层"的树

`make_one_rel` 从下往上把计划包起来：

```text
原：SeqScan(student)
包 Filter：Filter(SeqScan(student))
包 Projection：Projection(Filter(SeqScan(student)))
```

变量 `plannerRoot` 每包一层就"接住"新的父节点。`std::move(plannerRoot)` 把旧根交给新父节点，随后变量再指向新父节点。**move 之后旧 shared_ptr 变空，必须用新返回值继续**。

## 一、`get_index_cols`（最左前缀索引选型）

位置：[planner.cpp](../../src/optimizer/planner.cpp:41)

```cpp
bool Planner::get_index_cols(std::string tab_name, std::vector<Condition> curr_conds,
                             std::vector<std::string>& index_col_names);
```

逐个变量：

| 变量 | 类型 | 作用 |
|---|---|---|
| `tab_name` | string | 为哪张表选索引 |
| `curr_conds` | vector\<Condition> | 该表单表条件副本 |
| `index_col_names` | vector\<string>& | **输出**：被选中的索引列顺序 |
| 返回值 | bool | true=找到可用索引 |

算法：

1. 收集 `available_cols`：条件是"右端是常量（`is_rhs_val`）、不是 `!=`、左列属于本表"的列名。**只有等值/范围条件能喂索引**，`!=` 不能。
2. 遍历表上所有索引，从 `index.cols[0]` 开始连续累加"前缀命中数"，取命中数最高的索引。
3. 按 IndexMeta 原始列序输出 `index_col_names`。

要点：
- **第一列未命中则索引不可定位**（最左前缀失效）。
- BETWEEN 被展开成 GE+LE 后，`salary` 自动进入 `available_cols`，索引能合成范围——**Planner 不需要认识 BETWEEN**。

## 二、`pop_conds`（抽出单表条件）

位置：[planner.cpp](../../src/optimizer/planner.cpp:91)

```cpp
std::vector<Condition> pop_conds(std::vector<Condition> &conds, std::string tab_names);
```

从 `conds` 里抽出只涉及 `tab_names` 一方的谓词，**并从原 conds 中 erase 掉**，返回抽出的数组。

判定逻辑：

```cpp
bool lhs_matches = (it->lhs_col.tab_name == tab_names);
bool rhs_matches = (!it->is_rhs_val && it->rhs_col.tab_name == tab_names);
bool single_table_value_cond = lhs_matches && it->is_rhs_val;  // 单表列 vs 常量
bool single_table_col_cond = lhs_matches && rhs_matches;       // 单表列 vs 同表列
```

**副作用**：`conds` 被修改。所以 make_one_rel 之后，`query->conds` 不再含原始全部条件——被 pop 走的进各表 Scan，剩下的（跨表）留给 Join。调试时别假设 conds 原样保留。

## 三、`push_conds`（跨表条件挂到 Join）

位置：[planner.cpp](../../src/optimizer/planner.cpp:113)

```cpp
int push_conds(Condition *cond, std::shared_ptr<Plan> plan);
```

返回值语义：

| 返回值 | 含义 |
|---|---|
| 0 | 该条件与当前节点无关 |
| 1 | 条件左列匹配到某侧 Scan |
| 2 | 条件右列匹配到某侧 Scan |
| 3 | 条件已挂入某个 Join 的 `conds_` |

递归规则：Filter/Projection 透传给子计划；Scan 按表名比对；Union 遍历所有分支；Join 先下探左、再下探右，若条件一端在左一端在右，会**交换左右列并翻转运算符**（`<` ↔ `>`，`<=` ↔ `>=`），然后挂进 `x->conds_`。

这个函数在 yan2 里当前没有被主路径直接大量调用（Join 条件抽取在 make_one_rel 内联做了），理解它的返回值约定即可。

## 四、辅助函数

### 4.1 `get_plan_table_name` / `get_base_scan`

```cpp
std::string get_plan_table_name(std::shared_ptr<Plan> plan);   // 沿 Filter/Project 下探取叶子 Scan 表名
static ScanPlan *get_base_scan(const std::shared_ptr<Plan> &plan);  // 取最底层 ScanPlan
```

两者都递归穿过 Filter/Project 包装，Union 返回空/空。

### 4.2 `select_inner_join_index`

内连接等值条件下，若内表侧列有单列索引，把该 Scan 的 tag 从 `T_SeqScan` 改成 `T_IndexScan`，并填 `index_col_names_`。**这是 INLJ 的加速关键**：它只改 ScanPlan 的 tag，不改执行器类型。

## 五、`logical_optimization` 与 `physical_optimization`

```cpp
std::shared_ptr<Query> Planner::logical_optimization(std::shared_ptr<Query> query, Context *context);
std::shared_ptr<Plan> Planner::physical_optimization(std::shared_ptr<Query> query, Context *context);
```

- `logical_optimization`：`normalize_and_deduplicate` 谓词规范化去重 → `propagate_equal_literals` 等值常量传播 → `reorder_inner_joins` 内连接重排，改写 Query 后返回。
- `physical_optimization`：`make_one_rel` 建连接树，然后**非聚合查询**挂 SortPlan（`generate_sort_plan`）；聚合查询的排序留给 AggregatePlan（避免"先排序再聚合"语义错误）。

## 六、`make_one_rel`（核心：建 Scan + 左深 Join 树）

位置：[planner.cpp](../../src/optimizer/planner.cpp:314)

逐段解释：

### 6.1 收集每表投影列

```cpp
std::map<std::string, std::vector<TabCol>> table_proj_cols;
if (!query->is_select_all) {
    for (auto &col : query->cols) {
        table_proj_cols[col.tab_name].push_back(col);   // SELECT 列
    }
}
for (auto &cond : query->conds) {
    if (!cond.is_rhs_val && cond.lhs_col.tab_name != cond.rhs_col.tab_name) {
        table_proj_cols[cond.lhs_col.tab_name].push_back(cond.lhs_col);  // Join 条件列
        table_proj_cols[cond.rhs_col.tab_name].push_back(cond.rhs_col);
    }
}
// dedup_cols 去重
```

多表查询会给每张表包一个**局部投影**（只保留 SELECT 列 + 跨表 Join 条件列），减少中间结果宽度。单表 Filter 条件列不用加，因为 Filter 在 Project 下面已执行完。

### 6.2 建每张表的 Scan

```cpp
auto curr_conds = pop_conds(query->conds, tables[i]);
// 派生表：generate_subquery_plan 或 UnionPlan + Filter
// 基表：
bool index_exist = get_index_cols(tables[i], curr_conds, index_col_names);
if (index_exist) scan = ScanPlan(T_IndexScan, ...);
else            scan = ScanPlan(T_SeqScan, ...);
std::shared_ptr<Plan> node = scan;
if (!curr_conds.empty()) node = FilterPlan(node, curr_conds);   // 包显式 Filter
if (tables.size() > 1 && !query->is_select_all) {
    // 多表非 SELECT *：包局部 Projection
    node = ProjectionPlan(T_Projection, node, proj_cols, false);
}
table_scan_executors[i] = node;
```

逐个变量：

| 变量 | 作用 |
|---|---|
| `curr_conds` | 当前表单表条件（从 query->conds pop 出来） |
| `index_col_names` | get_index_cols 的输出 |
| `index_exist` | 是否走索引 |
| `scan` | 基础 ScanPlan |
| `node` | 逐层包 Filter/局部 Project 后的节点 |
| `table_scan_executors` | 每张表的基础计划数组 |

### 6.3 左深 Join 树

```cpp
if (tables.size() == 1) return table_scan_executors[0];   // 单表直接返回

auto conds = std::move(query->conds);   // 剩余跨表条件
std::shared_ptr<Plan> table_join_executors = table_scan_executors[0];
std::vector<std::string> joined_tables = {tables[0]};

for (size_t i = 1; i < tables.size(); i++) {
    std::vector<Condition> join_conds;
    // 从 conds 抽「一端在已连接集合、另一端是新表」的谓词 → join_conds
    ...
    select_inner_join_index(sm_manager_, tables[i], join_conds, table_scan_executors[i]);
    table_join_executors = std::make_shared<JoinPlan>(
        T_NestLoop, std::move(table_join_executors),
        std::move(table_scan_executors[i]), join_conds);
    joined_tables.push_back(tables[i]);
}
return table_join_executors;
```

**第 i 条边的 Join 条件判定**：谓词一端在 `joined_tables`（已连接集合）、另一端是 `tables[i]`（新表）。

## 七、`generate_sort_plan`

```cpp
std::shared_ptr<Plan> Planner::generate_sort_plan(std::shared_ptr<Query> query, std::shared_ptr<Plan> plan);
```

`query->order_bys` 为空直接返回原 plan；否则收集 `sort_cols` / `is_desc`，包 `SortPlan`。

## 八、`generate_select_plan`（SELECT 计划入口）

位置：[planner.cpp](../../src/optimizer/planner.cpp:528)

```cpp
std::shared_ptr<Plan> Planner::generate_select_plan(std::shared_ptr<Query> query, Context *context) {
    query = logical_optimization(std::move(query), context);
    std::shared_ptr<Plan> plannerRoot = physical_optimization(query, context);

    if (query->has_agg || !query->group_bys.empty() || !query->havings.empty()) {
        plannerRoot = std::make_shared<AggregatePlan>(
            std::move(plannerRoot), query->select_items, query->group_bys,
            query->havings, query->order_bys, query->limit_num);
    } else {
        auto sel_cols = query->cols;
        plannerRoot = std::make_shared<ProjectionPlan>(
            T_Projection, std::move(plannerRoot), std::move(sel_cols),
            query->is_select_all, query->limit_num);
    }
    return plannerRoot;
}
```

## 九、`do_planner`（规划总入口）

位置：[planner.cpp](../../src/optimizer/planner.cpp:562)

用 `dynamic_pointer_cast<ast::...>(query->parse)` 判断语句类型：

| AST 类型 | 生成 | 说明 |
|---|---|---|
| CreateTable | DDLPlan(T_CreateTable) | 转 ColDef 数组 |
| DropTable / CreateIndex / DropIndex | DDLPlan | |
| InsertStmt | DMLPlan(T_Insert) | 直接带 query->values，无子计划 |
| DeleteStmt | DMLPlan(T_Delete, scan) | 先选索引建 ScanPlan |
| UpdateStmt | DMLPlan(T_Update, scan) | 同上，带 set_clauses |
| SelectStmt | DMLPlan(T_select, projection) | generate_select_plan 后再包外壳 |
| 其它 | throw InternalError | |

## 十、易错点总结

1. **`pop_conds` 修改 `query->conds`**，make_one_rel 后它只剩跨表条件。调试别假设原样。
2. `std::move(plannerRoot)` 后必须用返回值继续；`join_conds` move 给 Plan 后不能再用。
3. 局部 Projection 必须保留 Join 条件列，否则条件列被投影掉，Join 求值找不到列。
4. IndexScan 仍要保存完整 `conds_` 做最终过滤，索引边界只是减少候选。
5. Join 条件判定依赖 `joined_tables` 集合，表顺序不能乱交换（交换表顺序要同步条件）。
6. CROSS JOIN 不能"跳过 JoinPlan"，否则结果只剩一个输入。
