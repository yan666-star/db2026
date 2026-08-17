# 资格赛模拟题 2：yan2 的 CROSS JOIN 最小实现

> 适用基线：`yan2@d42abe6`。yan2 的 `NestedLoopJoinExecutor` 已把空条件视为全部匹配，因此本题只需增加词法和语法，不应修改 Analyze、Planner、Plan、Portal 或 Executor。

## 1. 目标语义

```sql
select * from colors cross join sizes;
```

左表 M 行、右表 N 行时输出 M×N 行；任一侧为空时结果为空。`CROSS JOIN` 后面不能写 `ON`。

## 2. 为什么执行层无需修改

yan2 的 `NestedLoopJoinExecutor::find_match()` 使用：

```cpp
if (fed_conds_.empty() || eval_conditions(*joined, fed_conds_, cols_)) {
    // 输出左右拼接记录
}
```

Planner 对 FROM 中的每张表建立扫描，并按顺序构造左深 JoinPlan。只要 parser 把右表加入 `FromClause::tables`，且不向 `FromClause::conds` 添加条件，现有执行器自然生成笛卡尔积。

## 3. 词法修改

在 `src/parser/lex.l` 的 `JOIN` 附近增加行首规则：

```lex
"CROSS" { return CROSS; }
```

规则必须位于标识符通用规则之前，否则 CROSS 可能被识别成普通表名或别名。

## 4. 语法修改

在 `src/parser/yacc.y` 的关键字 token 中加入：

```bison
%token CROSS
```

在 `tableList` 中增加：

```bison
| tableList CROSS JOIN tableRef {
    $1.tables.push_back($4);
    $$ = $1;
}
```

符号位置：`$1` 是已有 FROM 链，`$2` 是 CROSS，`$3` 是 JOIN，`$4` 是右表。不要读取不存在的 ON 条件，也不要向 `$1.conds` 添加恒真表达式。

如果同一次练习已经启用了 JOIN 扩展的 `join_types` 字段，该分支还必须补：

```cpp
$1.join_types.push_back(INNER_JOIN);
```

否则 `join_types.size()` 会比 `tables.size() - 1` 少一项，后续混合 SEMI/LEFT 等连接时边类型错位。若只独立实现 CROSS，yan2 默认 `join_types` 仍被 `#if 0` 关闭，则不需要为本题打开整套扩展链。

## 5. 不应修改的层

- `ast.h`：`FromClause` 已能保存多张表和条件数组。
- `analyze.cpp`：已有多表列绑定和条件分析。
- `planner.cpp`：已有左深树构造；空 `join_conds` 合法。
- `plan.h`：普通 `JoinPlan` 已足够。
- `executor_nestedloop_join.h`：空条件已经实现全组合。
- `portal.h`：仍装配普通 `NestedLoopJoinExecutor`。

如果为了 CROSS 新增 `CrossJoinPlan` 或 `CrossJoinExecutor`，会扩大现场代码量并增加 Portal/CMake 改动，反而偏离最小实现。

## 6. Parser 测试

在 `src/parser/test_parser.cpp` 增加至少这些输入：

```sql
select * from colors cross join sizes;
select colors.id from colors cross join sizes;
```

非法输入：

```sql
select * from colors cross sizes;
select * from colors cross join;
select * from colors cross join sizes on colors.id = sizes.id;
```

第三条是否失败取决于完整 grammar 是否可能把尾部 ON 当成其他结构；本题目标是明确拒绝，而不是悄悄忽略。

## 7. SQL 语义测试

```sql
create table colors(id int, name char(8));
create table sizes(id int, name char(8));
create table empty_sizes(id int);

insert into colors values(1, 'red');
insert into colors values(2, 'blue');
insert into sizes values(10, 'small');
insert into sizes values(20, 'large');
insert into sizes values(30, 'wide');

select * from colors cross join sizes;
select * from colors cross join empty_sizes;
```

预期第一条 SELECT 为 6 行，第二条为 0 行。再回归普通 `JOIN ... ON` 和逗号连接，确认新增 token 没有破坏既有语法。

## 8. 编译与现场顺序

修改 `lex.l`/`yacc.y` 后必须重新运行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)" --target rmdb test_parser
ctest --test-dir build --output-on-failure -R test_parser
```

手写顺序：`lex.l` 认识 CROSS → `yacc.y` 声明 token → `tableList` 加无 ON 分支 → parser 负例 → 2×3 与空表 SQL 测试。
