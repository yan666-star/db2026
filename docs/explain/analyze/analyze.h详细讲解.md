# `analyze.h` 详细讲解

源码入口：[analyze.h](../../src/analyze/analyze.h:13)

这个文件声明 **Analyze 语义分析层**。它把 parser 生成的 AST 变成 Planner 可用的 `Query`。核心是理解 **`Query` 这个类的每个字段**，以及 Analyze 对外暴露的入口。

## 〇、为什么 parser 之后还要 Analyze

Parser 只知道用户写了 `id`，不知道 `id` 是否存在、属于哪张表。Analyze 查 `SmManager` 的表目录，把模糊写法变成确定信息：

```text
Parser:  Col{tab_name="", col_name="id"}
Analyze: TabCol{tab_name="student", col_name="id"}   ← 列绑定
```

Planner 和 Executor 只认绑定后的真表名。

## 一、`DerivedTableInfo`（派生表分析结果）逐个字段

位置：[analyze.h](../../src/analyze/analyze.h:42)

```cpp
struct DerivedTableInfo {
    bool is_union_table = false;                              // 是否由集合查询构成
    std::vector<ColMeta> cols;                                // 派生表对外可见 schema
    std::vector<std::shared_ptr<Query>> branch_queries;       // 每个分支的分析结果
    std::vector<SetOpType> set_ops;                           // 分支间算子
};
```

| 字段 | 类型 | 含义 |
|---|---|---|
| `is_union_table` | bool | true=来自 `from (select... union select...) t`，false=普通子查询 |
| `cols` | vector\<ColMeta> | 派生表当作虚拟表时，对外暴露哪些列、什么布局 |
| `branch_queries` | vector\<Query> | 每个分支 `SELECT` 单独 `analyze_select` 的结果 |
| `set_ops` | vector\<SetOpType> | 相邻分支间算子；空视为全 UNION |

Planner 看到 `query->tables` 里是派生表别名时，查 `derived_tables[别名]` 决定建普通子查询计划还是 UnionPlan。

## 二、`Query`（语义分析产物）逐个字段

位置：[analyze.h](../../src/analyze/analyze.h:58)

`Query` 是 **Analyze 的输出、Planner 的主要输入**。所有字段要么由 `analyze_select` 写入，要么由 `do_analyze` 写入。

```cpp
class Query{
    public:
    std::shared_ptr<ast::TreeNode> parse;          // 原始 AST 根
    std::vector<Condition> conds;                  // WHERE+ON 合并谓词
    std::vector<TabCol> cols;                      // 投影列
    std::vector<SelectItem> select_items;          // 投影项（含聚合/别名）
    std::vector<TabCol> group_bys;                 // 分组列
    std::vector<HavingCond> havings;               // HAVING 条件
    std::vector<OrderByItem> order_bys;            // 排序项
    int limit_num = -1;                            // LIMIT；-1 无限制
    bool has_agg = false;                          // 是否含聚合
    std::vector<std::string> tables;               // FROM 顺序（真表名或派生表别名）
    std::vector<SetClause> set_clauses;            // UPDATE SET
    std::vector<Value> values;                     // INSERT 值
    bool is_explain_analyze = false;
    bool is_select_all = false;
    std::map<std::string, std::string> alias_to_table;   // 别名 → 真表名
    std::map<std::string, std::string> table_to_alias;   // 真表名 → 别名
    std::map<std::string, DerivedTableInfo> derived_tables; // 派生表
    Query(){}
};
```

逐个解释（写谁、读谁、作用）：

| 字段 | 类型 | 谁写 | 谁读 | 作用 |
|---|---|---|---|---|
| `parse` | shared_ptr\<TreeNode> | do_analyze | Planner do_planner | 判断语句类型（dynamic_cast） |
| `conds` | vector\<Condition> | get_clause+check_clause | Planner 下推/JOIN 条件 | 语义检查后的谓词，列名已绑定 |
| `cols` | vector\<TabCol> | analyze_select | Planner 根 Projection | SELECT 最终列 |
| `select_items` | vector\<SelectItem> | analyze_select | Planner Aggregate | 投影项（含聚合、别名） |
| `group_bys` | vector\<TabCol> | analyze_select | Planner Aggregate | 分组列 |
| `havings` | vector\<HavingCond> | analyze_select | Planner Aggregate | 聚合后条件 |
| `order_bys` | vector\<OrderByItem> | analyze_select | Planner Sort/Aggregate | 排序 |
| `limit_num` | int | analyze_select | Planner ProjectionPlan/AggregatePlan | `-1` 无限制 |
| `has_agg` | bool | analyze_select | Planner | true 走 AggregatePlan |
| `tables` | vector\<string> | analyze_select | Planner make_one_rel | FROM 顺序 |
| `set_clauses` | vector\<SetClause> | do_analyze(Update) | Planner DMLPlan | UPDATE |
| `values` | vector\<Value> | do_analyze(Insert) | Planner DMLPlan | INSERT |
| `is_explain_analyze` | bool | analyze_select | Planner DMLPlan | EXPLAIN ANALYZE |
| `is_select_all` | bool | analyze_select | Planner | `SELECT *` |
| `alias_to_table` | map | analyze_select | check_column | 别名消解 |
| `table_to_alias` | map | analyze_select | Portal/EXPLAIN | 输出展示 |
| `derived_tables` | map | analyze_select | Planner | 派生表/UNION |

## 三、`Analyze` 类接口逐个解释

```cpp
class Analyze {
private:
    SmManager *sm_manager_;   // 借用指针，不释放；查表元数据用
public:
    Analyze(SmManager *sm_manager);
    std::shared_ptr<Query> do_analyze(std::shared_ptr<ast::TreeNode> root);
    std::shared_ptr<Query> analyze_select(std::shared_ptr<ast::SelectStmt> x, bool allow_derived);
private:
    TabCol check_column(const std::vector<ColMeta> &all_cols, TabCol target,
                        const std::map<std::string, std::string> &alias_to_table);
    void get_all_cols(const std::vector<std::string> &tab_names, std::vector<ColMeta> &all_cols);
    void get_query_cols(const std::shared_ptr<Query> &query, std::vector<ColMeta> &all_cols);
    std::vector<ColMeta> get_branch_output_cols(const std::shared_ptr<Query> &query);
    DerivedTableInfo analyze_union_branches(...);
    DerivedTableInfo analyze_derived_subquery(...);
    std::shared_ptr<Query> analyze_top_level_union(...);
    static bool union_compatible(ColType a, ColType b);
    static ColMeta promote_union_col(const ColMeta &a, const ColMeta &b);
    void get_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds, std::vector<Condition> &conds);
    void check_clause(const std::shared_ptr<Query> &query, std::vector<Condition> &conds,
                      const std::map<std::string, std::string> &alias_to_table);
    Value convert_sv_value(const std::shared_ptr<ast::Value> &sv_val);
    CompOp convert_sv_comp_op(ast::SvCompOp op);
    AggType convert_agg_type(ast::AggFuncType func_type);
};
```

### 3.1 入口 `do_analyze(root)`

```cpp
std::shared_ptr<Query> do_analyze(std::shared_ptr<ast::TreeNode> root);
```

| 参数 | 含义 |
|---|---|
| `root` | parser 生成的 AST 根，实际是 SelectStmt/InsertStmt 等子类 |
| 返回 | 完成的 Query，交给 Planner |

内部用 `dynamic_pointer_cast<ast::SelectStmt>(root)` 判断真实语句类型：
- SelectStmt → `analyze_select(x, true)`
- UpdateStmt → 填 `set_clauses`、转 `conds`
- DeleteStmt → 填 `tables`、转 `conds`
- InsertStmt → 填 `values`
- 其它（DDL/事务）→ 空 Query 只挂 `parse`，由 Optimizer::plan_query 再认 AST

### 3.2 主流程 `analyze_select(x, allow_derived)`

```cpp
std::shared_ptr<Query> analyze_select(std::shared_ptr<ast::SelectStmt> x, bool allow_derived);
```

| 参数 | 含义 |
|---|---|
| `x` | 当前 SELECT AST；tables/conds/select_items 都从这里读 |
| `allow_derived` | 是否允许本层处理派生表/顶层 UNION；顶层 true，分支递归时 false |
| 返回 | 填好的 Query |

### 3.3 `check_column(all_cols, target, alias_to_table)`

```cpp
TabCol check_column(const std::vector<ColMeta> &all_cols, TabCol target,
                    const std::map<std::string, std::string> &alias_to_table);
```

列消解三步：

1. `target.tab_name` 非空且是别名 → 换成真表名。
2. `tab_name` 为空 → 按 `col_name` 在所有可见列里找唯一匹配；多个匹配抛 `AmbiguousColumnError`，没有抛 `ColumnNotFoundError`。
3. `tab_name` 非空 → 精确匹配 `(表名,列名)`。

返回绑定后的 `TabCol`。`all_cols` 用 const 引用（不复制、不修改），`target` 按值传（函数可以改写它，不影响调用者）。

### 3.4 `get_clause(sv_conds, conds)` 与 `check_clause(query, conds, alias_to_table)`

```cpp
void get_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds, std::vector<Condition> &conds);
void check_clause(const std::shared_ptr<Query> &query, std::vector<Condition> &conds,
                  const std::map<std::string, std::string> &alias_to_table);
```

`get_clause` 只做 **结构转换**：BinaryExpr → Condition（区分右端是值还是列）。`check_clause` 做 **语义加工**：列绑定、常量 cast + init_raw、左右类型一致。两者都必须执行，缺一个条件就不完整。

### 3.5 类型转换三件套

```cpp
Value  convert_sv_value(const std::shared_ptr<ast::Value> &sv_val);   // AST 字面量 → common Value
CompOp convert_sv_comp_op(ast::SvCompOp op);                           // SV_OP_* → OP_*
AggType convert_agg_type(ast::AggFuncType func_type);                  // AGG_* → AGG_*
```

## 四、易错点总结

1. 列绑定必须在 Analyze 做，别把 `ColumnNotFound` 留给执行器。
2. `id` 在多表都出现时抛 `AmbiguousColumnError`；`t.id` 用别名映射到真表。
3. 常量写进记录前必须 `init_raw(len)`，长度按列 len，否则字节布局错。
4. INT→FLOAT 允许提升，反向不能乱截断。
5. `sm_manager_` 是借用指针，不负责 delete。
