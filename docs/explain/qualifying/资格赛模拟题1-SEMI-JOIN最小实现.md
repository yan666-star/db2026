# 资格赛模拟题 1：yan2 的 SEMI JOIN 最小实现

> 适用基线：`yan2@d42abe6`。yan2 已预留完整 JOIN 扩展骨架，但默认用 `#if 0` 关闭。SEMI JOIN 应接入 `ExtendedJoinExecutor`，不要照旧文档直接改正式 INNER 执行器。

## 1. 最小题目范围

支持：

```sql
select departments.id
from departments semi join employees
on departments.id = employees.dept_id;
```

语义：右侧存在至少一条匹配记录时，左记录只输出一次；右侧匹配零条时不输出。第一版限定两张基础表、一个 SEMI JOIN、显式选择左表列；拒绝 `SELECT *`、右表投影、聚合、集合算子和派生表组合。

## 2. yan2 实际链路

```text
lex.l
  → yacc.y::tableList
  → ast::FromClause::join_types
  → ast::SelectStmt::join_types
  → Query::join_types
  → Planner 左深树第 i 条边
  → JoinPlan::type
  → Portal 分流
  → ExtendedJoinExecutor
```

涉及文件：

1. `src/parser/lex.l`
2. `src/parser/yacc.y`
3. `src/parser/ast.h`
4. `src/analyze/analyze.h`
5. `src/analyze/analyze.cpp`
6. `src/optimizer/plan.h`
7. `src/optimizer/planner.cpp`
8. `src/execution/executor_extended_join.h`
9. `src/portal.h`

## 3. AST 与边类型

`src/parser/ast.h` 的 `JoinType` 已有 INNER/LEFT/RIGHT/FULL/ANTI，只需追加：

```cpp
enum JoinType {
    INNER_JOIN,
    LEFT_JOIN,
    RIGHT_JOIN,
    FULL_JOIN,
    ANTI_JOIN,
    SEMI_JOIN
};
```

把同文件 `FromClause::join_types` 和 `SelectStmt::join_types` 外层的 `#if 0` 改为 `#if 1`。约定必须保持：

```text
join_types[i] = 把 tables[i + 1] 接到当前左深树时使用的类型
join_types.size() == tables.size() - 1
```

## 4. 词法与语法

在 `src/parser/lex.l` 的 JOIN 附近加入行首规则：

```lex
"SEMI" { return SEMI; }
```

在 `src/parser/yacc.y` 声明 token：

```bison
%token SEMI
```

`tableList` 每加入一张表都必须同步加入一个边类型。yan2 现有逗号连接、裸 JOIN 和 JOIN ON 分支都要补 INNER：

```bison
| tableList ',' tableRef {
    $1.tables.push_back($3);
    $1.join_types.push_back(INNER_JOIN);
    $$ = $1;
}
| tableList JOIN tableRef {
    $1.tables.push_back($3);
    $1.join_types.push_back(INNER_JOIN);
    $$ = $1;
}
| tableList JOIN tableRef ON whereClause {
    $1.tables.push_back($3);
    $1.conds.insert($1.conds.end(), $5.begin(), $5.end());
    $1.join_types.push_back(INNER_JOIN);
    $$ = $1;
}
| tableList SEMI JOIN tableRef ON whereClause {
    $1.tables.push_back($4);
    $1.conds.insert($1.conds.end(), $6.begin(), $6.end());
    $1.join_types.push_back(SEMI_JOIN);
    $$ = $1;
}
```

注意 SEMI 产生式中 `$4` 才是右表，`$6` 才是 ON 条件。`select_branch` 不能继续直接构造并丢弃边类型，应改为：

```cpp
auto stmt = std::make_shared<SelectStmt>(
    $2, $4.tables, conds, $6, $7, nullptr, -1);
stmt->join_types = $4.join_types;
$$ = stmt;
```

## 5. Analyze 下传并限制输出

把 `src/analyze/analyze.h` 中 `Query::join_types` 的 `#if 0` 改为 `#if 1`；在 `Analyze::analyze_select` 已完成列绑定、`get_clause` 和 `check_clause` 后复制：

```cpp
query->join_types = x->join_types;
```

最小版本只允许单个 SEMI JOIN，并拒绝 `SELECT *` 和右表列：

```cpp
if (query->join_types.size() == 1 && query->join_types[0] == SEMI_JOIN) {
    if (query->is_select_all) {
        throw RMDBError("failure");
    }
    const std::string &right_tab = query->tables[1];
    for (const auto &col : query->cols) {
        if (col.tab_name == right_tab) {
            throw RMDBError("failure");
        }
    }
}
```

不要把检查放在列名尚未补全表名之前，否则无表名前缀的列无法可靠判断归属。

## 6. Planner 与 JoinPlan

把 `src/optimizer/plan.h` 中带 `JoinType` 参数的 `JoinPlan` 构造函数启用。保留原四参数构造可兼容旧调用。

在 `Planner::make_one_rel` 的左深树循环中，把 yan2 当前四参数构造改为：

```cpp
JoinType join_type = INNER_JOIN;
if (i - 1 < query->join_types.size()) {
    join_type = query->join_types[i - 1];
}
table_join_executors = std::make_shared<JoinPlan>(
    T_NestLoop,
    std::move(table_join_executors),
    std::move(table_scan_executors[i]),
    join_conds,
    join_type);
```

`join_conds` 仍使用 yan2 已有逻辑：从剩余条件中抽取“一端属于已连接集合、另一端属于新表”的谓词。

## 7. ExtendedJoinExecutor 增加 SEMI

把 `src/execution/executor_extended_join.h` 顶层 `#if 0` 改成 `#if 1`。SEMI 与 ANTI 都只输出左侧 schema：

```cpp
if (join_type_ == ANTI_JOIN || join_type_ == SEMI_JOIN) {
    len_ = left_len_;
    cols_ = left_->cols();
} else {
    // 保留 yan2 原有左右 schema 拼接
}
```

可把 `anti_left_only` 重命名为 `left_only`。在 `find_next()` 扫描当前左行时，首次匹配 SEMI 就输出左行一次，并在返回前推进左游标：

```cpp
if (cond_ok(*joined)) {
    left_has_match_ = true;
    right_matched_[right_idx_] = 1;

    if (join_type_ == ANTI_JOIN) {
        right_idx_ = right_buffer_.size();
        break;
    }
    if (join_type_ == SEMI_JOIN) {
        auto result = left_only(*left_rec);
        left_->nextTuple();
        left_has_match_ = false;
        right_idx_ = 0;
        emit(std::move(result));
        return;
    }
    // 保留 LEFT/RIGHT/FULL/INNER 的 joined 输出
}
```

右表为空时 SEMI 结果为空，因此 `beginTuple()` 的空右表判断应把 SEMI 与 INNER/RIGHT 放在一起。右表非空但当前左行无匹配时，SEMI 直接推进左行，不输出。

## 8. Portal 分流

在 `src/portal.h` 引入 `executor_extended_join.h`，并启用现有分流骨架：

```cpp
if (x->type != INNER_JOIN) {
    return std::make_unique<ExtendedJoinExecutor>(
        std::move(left), std::move(right), x->conds_, x->type, x.get());
}
return std::make_unique<NestedLoopJoinExecutor>(
    std::move(left), std::move(right), x->conds_, x.get());
```

这样正式 `NestedLoopJoinExecutor` 继续只负责 INNER，不引入 SEMI 状态机分支。

## 9. 必测用例

- 一对一匹配：输出对应左行。
- 一对多匹配：左行只输出一次。
- 左行无匹配：不输出。
- 右表为空：结果为空。
- 左表为空：结果为空。
- `SELECT *`：按最小范围返回 `failure`。
- 投影右表列：返回 `failure`。
- 普通 JOIN：结果不能受影响。

编译和运行方式见《资格赛模拟题：yan2 通用编译、测试与 Bash》。现场顺序是：枚举/字段 → lex → yacc → Analyze → JoinPlan/Planner → ExtendedJoinExecutor → Portal → parser 测试 → SQL 边界测试。
