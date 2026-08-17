# 资格赛线下新增功能演练：yan2 最小实现 SELECT DISTINCT

> 适用基线：`yan2@d42abe6`。yan2 的 Projection 同时负责最终列布局、LIMIT 与 EXPLAIN ANALYZE 的 `rows_` 计数，因此旧版“只在 Next() 里塞 unordered_set”的方案不够安全。必须让去重发生在 LIMIT 和输出计数之前。

## 1. 最小功能范围

支持：

```sql
select distinct dept from employee;
select distinct dept, name from employee;
select distinct * from employee;
select distinct dept from employee where id >= 2 limit 2;
```

DISTINCT 针对最终投影记录的全部字节去重。第一版拒绝与聚合、GROUP BY、HAVING、集合算子组合；ORDER BY 是否支持取决于题面，若未明确，现场最稳妥做法也是先拒绝组合。

## 2. yan2 的正确落点

```text
lex.l
  → yacc.y::select_branch
  → ast::SelectStmt::is_distinct
  → Query::is_distinct
  → ProjectionPlan::is_distinct_
  → Portal 仍创建 ProjectionExecutor
  → ProjectionExecutor 在投影后、LIMIT 前去重
```

不新增 PlanTag、DistinctPlan、DistinctExecutor，也不修改 Portal 的节点分派。涉及：

1. `src/parser/lex.l`
2. `src/parser/yacc.y`
3. `src/parser/ast.h`
4. `src/analyze/analyze.h`
5. `src/analyze/analyze.cpp`
6. `src/optimizer/plan.h`
7. `src/optimizer/planner.cpp`
8. `src/execution/executor_projection.h`

## 3. Lexer 与可选语法

在 `src/parser/lex.l` 的 SELECT 附近增加：

```lex
"DISTINCT" { return DISTINCT; }
```

在 `src/parser/yacc.y` 增加：

```bison
%token DISTINCT
%type <sv_bool> opt_distinct
```

yan2 的 `SemValue` 已有 `bool sv_bool`，可直接复用：

```bison
opt_distinct:
    /* epsilon */ { $$ = false; }
    | DISTINCT    { $$ = true; }
    ;
```

把当前 `select_branch` 改为：

```bison
select_branch:
    SELECT opt_distinct selector FROM tableList
           optWhereClause opt_group_by_clause opt_having_clause {
        auto conds = $5.conds;
        conds.insert(conds.end(), $6.begin(), $6.end());
        auto stmt = std::make_shared<SelectStmt>(
            $3, $5.tables, conds, $7, $8, nullptr, -1);
        stmt->is_distinct = $2;
        $$ = stmt;
    }
    ;
```

加入 `opt_distinct` 后，原产生式 `$2/$4/$5/$6/$7` 全部右移一位；这是最容易写错的地方。若同时启用了 JOIN 扩展，还要在构造后执行：

```cpp
stmt->join_types = $5.join_types;
```

## 4. AST 与 Query 传递

在 `ast::SelectStmt` 加：

```cpp
bool is_distinct = false;
```

在 `Query` 加：

```cpp
bool is_distinct = false;
```

`Analyze::analyze_select` 中，在确认当前是普通查询分支后复制：

```cpp
query->is_distinct = x->is_distinct;
```

完成 `has_agg`、`group_bys`、`havings` 等信息构造后执行最小组合限制：

```cpp
if (query->is_distinct &&
    (query->has_agg || !query->group_bys.empty() || !query->havings.empty())) {
    throw RMDBError("failure");
}
```

集合算子在 yan2 中由 `SelectStmt::is_union/union_branches/set_ops` 表示。若任一分支 DISTINCT 与集合算子组合不在题目范围，应在分析集合链时统一拒绝，不能只检查外层空壳节点。

## 5. ProjectionPlan 保存标志

在 `ProjectionPlan` 构造函数最后追加带默认值的参数，避免破坏其他调用：

```cpp
ProjectionPlan(PlanTag tag,
               std::shared_ptr<Plan> subplan,
               std::vector<TabCol> sel_cols,
               bool display_all = false,
               int limit_num = -1,
               bool is_distinct = false) {
    Plan::tag = tag;
    subplan_ = std::move(subplan);
    sel_cols_ = std::move(sel_cols);
    display_all_ = display_all;
    limit_num_ = limit_num;
    is_distinct_ = is_distinct;
}

bool is_distinct_ = false;
```

yan2 的参数顺序是 `sel_cols, display_all, limit_num`，不能照旧文档把 DISTINCT 插到 LIMIT 前面。

`Planner::generate_select_plan` 当前构造 ProjectionPlan 的位置改为：

```cpp
plannerRoot = std::make_shared<ProjectionPlan>(
    T_Projection,
    std::move(plannerRoot),
    std::move(sel_cols),
    query->is_select_all,
    query->limit_num,
    query->is_distinct);
```

Portal 已把 `ProjectionPlan *` 传给 `ProjectionExecutor`，无需修改。

## 6. ProjectionExecutor 的状态机

增加：

```cpp
#include <string>
#include <unordered_set>

bool is_distinct_ = false;
std::unordered_set<std::string> seen_;
std::unique_ptr<RmRecord> current_;
```

构造时读取：

```cpp
is_distinct_ = plan_ != nullptr && plan_->is_distinct_;
```

把 yan2 当前 `Next()` 中的投影逻辑提为只负责“投影当前子记录”的函数：

```cpp
std::unique_ptr<RmRecord> project_current() {
    auto rec = prev_->Next();
    if (rec == nullptr) return nullptr;
    if (is_sel_all_) return rec;

    auto projected = std::make_unique<RmRecord>(len_);
    const auto &prev_cols = prev_->cols();
    size_t offset = 0;
    for (size_t idx : sel_idxs_) {
        const auto &col = prev_cols[idx];
        memcpy(projected->data + offset, rec->data + col.offset, col.len);
        offset += col.len;
    }
    return projected;
}
```

新增定位下一条唯一记录的函数：

```cpp
void seek_unique() {
    current_.reset();
    while (!prev_->is_end()) {
        auto rec = project_current();
        if (rec == nullptr) return;
        if (!is_distinct_) {
            current_ = std::move(rec);
            return;
        }
        std::string key(rec->data, static_cast<size_t>(rec->size));
        if (seen_.insert(std::move(key)).second) {
            current_ = std::move(rec);
            return;
        }
        prev_->nextTuple();
    }
}
```

二进制 key 必须带显式长度，不能使用 `std::string(rec->data)`，因为 INT/FLOAT/定长 CHAR 内部可能含 `\0`。

迭代接口按“先去重，再 LIMIT，再计数”改写：

```cpp
bool is_end() const override {
    return current_ == nullptr || result_idx_ >= limit_;
}

void beginTuple() override {
    seen_.clear();
    current_.reset();
    result_idx_ = 0;
    prev_->beginTuple();
    if (limit_ == 0) return;
    seek_unique();
    if (current_ != nullptr && plan_ != nullptr) plan_->rows_++;
}

void nextTuple() override {
    if (is_end()) return;
    result_idx_++;
    if (result_idx_ >= limit_) {
        current_.reset();
        return;
    }
    prev_->nextTuple();
    seek_unique();
    if (current_ != nullptr && plan_ != nullptr) plan_->rows_++;
}

std::unique_ptr<RmRecord> Next() override {
    if (is_end()) return nullptr;
    return std::make_unique<RmRecord>(*current_);
}
```

普通 SELECT 也走该状态机，但 `is_distinct_ == false` 时 `seek_unique()` 立即接受当前记录，语义不变。

## 7. 必测边界

```sql
create table employee(id int, dept int, name char(8));
insert into employee values(1, 10, 'a');
insert into employee values(2, 10, 'b');
insert into employee values(3, 20, 'c');
insert into employee values(4, 20, 'c');
insert into employee values(5, 30, 'd');

select distinct dept from employee;
select distinct dept, name from employee;
select distinct * from employee;
select distinct dept from employee limit 2;
select dept from employee limit 2;
```

检查：单列重复、多列只有整行相同才去重、DISTINCT *、空表、全重复、LIMIT 小于/大于唯一值数量。`DISTINCT ... LIMIT 2` 必须得到两个不同值，不能先取原始前两行再去重成一个值。

还要回归普通 SELECT、SELECT *、WHERE、JOIN 和 LIMIT；EXPLAIN ANALYZE 的 Projection rows 应为实际输出的唯一行数，而不是扫描行数。

## 8. 编译与手写顺序

修改 parser 后重新配置并构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)" --target rmdb test_parser
ctest --test-dir build --output-on-failure -R test_parser
```

现场顺序：lex → opt_distinct/下标 → SelectStmt → Query/组合限制 → ProjectionPlan 参数尾部 → Planner → ProjectionExecutor 状态机 → DISTINCT+LIMIT 边界 → 普通 Projection 回归。
