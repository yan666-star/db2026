# `plan.h` 详细讲解

源码入口：[plan.h](../../src/optimizer/plan.h:13)

这个文件定义 **执行计划节点（Plan）**。Planner 决定"用哪些算子、按什么顺序"，Portal 再按 Plan 树创建 Executor。**Plan 只描述做什么，不执行**。它保存参数和配置，不保存扫描游标或运行状态。

## 〇、Plan 和 Executor 的关系

```text
Query → Optimizer::plan_query → Planner::do_planner → Plan 树 → Portal → Executor 树
```

一个 Plan 对应一个（或多个）Executor。例如：

```text
ProjectionPlan          →  ProjectionExecutor
  └─ JoinPlan           →  NestedLoopJoinExecutor / ExtendedJoinExecutor
       ├─ ScanPlan      →  SeqScanExecutor / IndexScanExecutor
       └─ ScanPlan      →  SeqScanExecutor
```

## 一、`PlanTag` 枚举（节点类型标签）

位置：[plan.h](../../src/optimizer/plan.h:35)

```cpp
typedef enum PlanTag {
    T_Invalid = 1, T_Help, T_ShowTable, T_ShowIndex, T_DescTable, T_StaticCheckpoint,
    T_CreateTable, T_DropTable, T_CreateIndex, T_DropIndex, T_SetKnob,
    T_Insert, T_Update, T_Delete, T_select,
    T_Transaction_begin, T_Transaction_commit, T_Transaction_abort, T_Transaction_rollback,
    T_SeqScan, T_IndexScan, T_NestLoop, T_SortMerge,
    T_Sort, T_Aggregate, T_Projection, T_Filter, T_Union, T_SetTransactionIsolation
} PlanTag;
```

Portal 和 EXPLAIN 都按 `tag` 分支。

注意：
- `T_select` 是 DMLPlan 的 tag（SELECT 也包一层 DMLPlan），`T_NestLoop` / `T_SortMerge` 是两种 Join 算法。
- `T_Filter` 是显式 Filter 计划（便于 EXPLAIN 展示）。

## 二、`Plan` 基类

```cpp
class Plan {
public:
    PlanTag tag;            // 节点类型
    size_t rows_ = 0;       // 估计/实测行数（EXPLAIN ANALYZE 回填）
    virtual ~Plan() = default;
};
```

`rows_` 是 **EXPLAIN ANALYZE 用**：Executor 每输出一行就 `plan_->rows_++`。普通查询里它不影响结果，但 EXPLAIN ANALYZE 依赖它回填实际行数，必须保证计数与实际输出一致。

## 三、`ScanPlan` 逐个字段

位置：[plan.h](../../src/optimizer/plan.h:83)

```cpp
ScanPlan(PlanTag tag, SmManager *sm_manager, std::string tab_name,
         std::vector<Condition> conds, std::vector<std::string> index_col_names) {
    Plan::tag = tag;
    tab_name_ = std::move(tab_name);
    conds_ = std::move(conds);
    TabMeta &tab = sm_manager->db_.get_table(tab_name_);   // 立即查表元数据
    cols_ = tab.cols;                                        // 输出 schema = 整表列
    len_ = cols_.back().offset + cols_.back().len;           // 最后列 offset+len = 记录长
    fed_conds_ = conds_;                                     // 谓词副本
    index_col_names_ = index_col_names;
}
```

| 字段 | 类型 | 作用 |
|---|---|---|
| `tab_name_` | string | 扫描目标表 |
| `cols_` | vector\<ColMeta> | 整表物理列布局（输出 schema） |
| `conds_` | vector\<Condition> | 下推到本扫描的谓词 |
| `len_` | size_t | 元组字节长度 = 最后列 offset+len |
| `fed_conds_` | vector\<Condition> | 与 conds_ 同源，供执行器过滤 |
| `index_col_names_` | vector\<string> | IndexScan 的索引列前缀；空表示 SeqScan |

构造函数**立即查表元数据**（`get_table`），所以建 ScanPlan 时表必须存在。

`len_` 的计算方式：`cols_.back().offset + cols_.back().len`。记录布局是列连续拼接，最后列的结束位置就是记录总长。

## 四、`FilterPlan`

```cpp
class FilterPlan : public Plan {
public:
    FilterPlan(std::shared_ptr<Plan> subplan, std::vector<Condition> conds);
    std::shared_ptr<Plan> subplan_;  // 子计划
    std::vector<Condition> conds_;   // 过滤条件
};
```

不改变 schema（输入输出列一样），只过滤行。

## 五、`JoinPlan` 逐个字段

位置：[plan.h](../../src/optimizer/plan.h:128)

```cpp
class JoinPlan : public Plan {
    public:
        // 【考点】现行构造固定 type=INNER_JOIN
        JoinPlan(PlanTag tag, std::shared_ptr<Plan> left, std::shared_ptr<Plan> right, std::vector<Condition> conds)
        {
            ...
            type = INNER_JOIN;
        }
#if 0  // TODO(JOIN扩展): 改为 #if 1
        JoinPlan(PlanTag tag, std::shared_ptr<Plan> left, std::shared_ptr<Plan> right,
                 std::vector<Condition> conds, JoinType join_type)
        {
            ...
            type = join_type;
        }
#endif
        std::shared_ptr<Plan> left_;   // 左子树（左深树中已连接的子计划）
        std::shared_ptr<Plan> right_;  // 右子树（新接入的表扫描/过滤计划）
        std::vector<Condition> conds_; // 本层 Join 条件（两端分属已连接表与新表）
        JoinType type;                 // 当前固定 INNER；扩展后可为 LEFT/RIGHT/FULL/ANTI/SEMI
};
```

关键点：
- **现行四参数构造把 `type` 硬编码成 `INNER_JOIN`**，所以 Portal 创建 JoinPlan 时 type 恒为 INNER。
- `left_` 可能是已经 Join 出来的子树（左深树），`right_` 是新的单表计划。
- `conds_` 是"本层"的 Join 条件：一端属于已连接集合，一端属于新表。

## 六、`ProjectionPlan` 逐个字段

位置：[plan.h](../../src/optimizer/plan.h:168)

```cpp
ProjectionPlan(PlanTag tag, std::shared_ptr<Plan> subplan, std::vector<TabCol> sel_cols,
               bool display_all = false, int limit_num = -1) { ... }

std::shared_ptr<Plan> subplan_;   // 子计划
std::vector<TabCol> sel_cols_;    // 投影列列表
bool display_all_ = false;        // SELECT * 展示标记
int limit_num_ = -1;              // LIMIT；-1 表示不限制
```

**参数顺序是 `sel_cols, display_all, limit_num`**。

## 七、`SortPlan` / `UnionPlan` / `AggregatePlan`

### SortPlan

```cpp
std::shared_ptr<Plan> subplan_;
TabCol sel_col_;                 // 兼容单列排序的首列
bool is_desc_;                   // 首列是否降序
std::vector<TabCol> sort_cols_;  // 多列排序键
std::vector<bool> is_descs_;     // 各列升降序
```

单列构造会自动填 `sort_cols_`/`is_descs_`，多列构造填首列到 `sel_col_`。

### UnionPlan

```cpp
std::vector<std::shared_ptr<Plan>> branches_;  // 各分支子计划
std::vector<ColMeta> out_cols_;                // 统一输出列（构造时已重排 offset）
std::vector<SetOpType> set_ops_;               // 相邻分支间算子
size_t len_ = 0;                               // 输出元组长度
```

构造函数里会**重排 out_cols 的 offset**（从 0 累加 len），并默认 `set_ops_` 为全 UNION。

### AggregatePlan

```cpp
std::shared_ptr<Plan> subplan_;
std::vector<SelectItem> select_items_;  // SELECT 列表（含聚合）
std::vector<TabCol> group_bys_;         // GROUP BY 列
std::vector<HavingCond> havings_;       // HAVING 条件
std::vector<OrderByItem> order_bys_;    // 分组结果排序
int limit_num_;                         // LIMIT
```

聚合查询的 ORDER BY 在聚合算子内部处理，不另挂 SortPlan。

## 八、`DMLPlan` / `DDLPlan` / `OtherPlan` / `SetKnobPlan`

### DMLPlan（INSERT/DELETE/UPDATE/SELECT 统一外壳）

```cpp
std::shared_ptr<Plan> subplan_;              // 子计划（扫描树；Insert 可为空）
std::string tab_name_;                       // 目标表（SELECT 可为空串）
std::vector<Value> values_;                  // INSERT 值列表
std::vector<Condition> conds_;               // DELETE/UPDATE 条件
std::vector<SetClause> set_clauses_;         // UPDATE SET 子句
bool is_explain_analyze_ = false;            // EXPLAIN ANALYZE
std::map<std::string, std::string> table_to_alias_;  // 表名→别名（EXPLAIN 展示）
```

SELECT 也包一层 `DMLPlan(T_select, projection, ...)`，方便 Portal 统一调度。

### DDLPlan

```cpp
std::string tab_name_;                    // 表名
std::vector<std::string> tab_col_names_;  // 索引列名等
std::vector<ColDef> cols_;                // 建表列定义
```

### OtherPlan / SetKnobPlan

- `OtherPlan(tag, tab_name)`：help/show/desc/begin 等元命令。
- `SetKnobPlan(knob_type, bool_value)`：Join 算法开关等。

## 九、`plannerInfo`（历史辅助结构）

```cpp
class plannerInfo {
    std::shared_ptr<ast::SelectStmt> parse;
    std::vector<Condition> where_conds;
    std::vector<TabCol> sel_cols;
    std::shared_ptr<Plan> plan;
    std::vector<std::shared_ptr<Plan>> table_scan_executors;
    std::vector<SetClause> set_clauses;
};
```

主路径现在多用 `Query` + 局部变量，`plannerInfo` 是历史遗留，一般不改。

## 十、易错点总结

1. Plan 树顺序 = 关系代数顺序，**父节点后执行**。
2. Plan 用 `shared_ptr`，Executor 用 `unique_ptr`；Executor 里保存 Plan 裸指针是借用，不 delete。
3. 状态（seen_、cursor、临时记录）放 Executor，**不放 Plan**。Plan 只描述。
4. `JoinPlan::type` 当前固定 INNER，Portal 走 NestedLoopJoinExecutor。
5. `ProjectionPlan` 参数顺序是 `sel_cols, display_all, limit_num`。
