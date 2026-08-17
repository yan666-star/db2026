# 资格赛模拟题 3：yan2 的 BETWEEN 最小实现

> 适用基线：`yan2@d42abe6`。yan2 已支持 `SV_OP_GE/SV_OP_LE → OP_GE/OP_LE`、顺序扫描条件求值和索引范围条件。本题最小方案是在 parser 中把 BETWEEN 展开成两个已有比较条件。

## 1. 最小题目范围

支持：

```sql
select * from employees where salary between 70000 and 85000;
select * from employees where dept_id = 2 and salary between 70000 and 85000;
```

等价于：

```sql
salary >= 70000 and salary <= 85000
```

边界包含在结果中。第一版限定左侧为列、上下界为字面值；不实现 `NOT BETWEEN`、表达式边界或自动交换上下界。

## 2. 为什么只改 parser

yan2 已有：

- `ast::SvCompOp` 中的 `SV_OP_GE`、`SV_OP_LE`；
- `ast::BinaryExpr` 条件节点；
- `Analyze::get_clause()` 对比较运算符的转换；
- `Condition` 中的 `OP_GE`、`OP_LE`；
- Planner 条件下推、SeqScan/IndexScan 条件执行。

因此不要新增 BETWEEN 运算符到 `Condition`，也不要修改执行器。parser 输出两个普通 `BinaryExpr` 后，后续层完全复用。

## 3. 词法修改

在 `src/parser/lex.l` 的关键字区增加行首规则：

```lex
"BETWEEN" { return BETWEEN; }
```

必须放在通用标识符规则之前。

## 4. Bison 类型与产生式

在 `src/parser/yacc.y` 加入：

```bison
%token BETWEEN
%type <sv_conds> betweenCondition
```

yan2 的 `SemValue` 已有 `sv_conds`，因此无需修改 `ast.h` 的语义值结构。

新增：

```bison
betweenCondition:
    col BETWEEN value AND value {
        auto lower = std::make_shared<BinaryExpr>($1, SV_OP_GE, $3);
        auto upper = std::make_shared<BinaryExpr>($1, SV_OP_LE, $5);
        $$ = std::vector<std::shared_ptr<BinaryExpr>>{lower, upper};
    }
    ;
```

这里 `$1` 是左列，`$3` 是下界，`$5` 是上界。两个条件共享同一个 `Col` 节点是安全的，因为后续分析只读取并绑定列信息。

## 5. 接入 whereClause

yan2 当前 `whereClause` 的语义类型是条件数组，原分支为单个 `condition` 或继续 AND 单个条件。增加：

```bison
whereClause:
    condition {
        $$ = std::vector<std::shared_ptr<BinaryExpr>>{$1};
    }
    | betweenCondition {
        $$ = $1;
    }
    | whereClause AND condition {
        $$ = $1;
        $$.push_back($3);
    }
    | whereClause AND betweenCondition {
        $$ = $1;
        $$.insert($$.end(), $3.begin(), $3.end());
    }
    ;
```

不要只加 `betweenCondition` 的首项分支，否则 `id = 1 AND salary BETWEEN ...` 无法解析；也不要把两个条件包成一个不存在的新 AST 节点。

## 6. 下游数据流

```text
salary BETWEEN 70000 AND 85000
  → yacc.y 生成 BinaryExpr(GE) + BinaryExpr(LE)
  → SelectStmt::conds
  → Analyze::get_clause
  → Condition(OP_GE) + Condition(OP_LE)
  → Planner 下推到 ScanPlan
  → SeqScan 或 IndexScan 复用现有范围判断
```

如果 salary 上有可用索引，是否选 IndexScan 由 yan2 当前 Planner 决定；BETWEEN 实现本身不应强行选择索引。

## 7. 必测 SQL

```sql
create table employees(id int, salary int);
insert into employees values(1, 69999);
insert into employees values(2, 70000);
insert into employees values(3, 75000);
insert into employees values(4, 85000);
insert into employees values(5, 85001);

select * from employees where salary between 70000 and 85000;
select * from employees where id >= 3 and salary between 70000 and 85000;
select * from employees where salary between 90000 and 80000;
```

预期：

- 第一条返回 70000、75000、85000，证明双端包含。
- 第二条只返回 id 3、4，证明与普通 AND 能组合。
- 第三条返回 0 行；最小实现不交换上下界。

再建立 salary 索引重复运行，结果必须与无索引一致。

非法输入至少包括：缺少 AND、缺少上界、`NOT BETWEEN`、左侧不是列。

## 8. 编译与现场顺序

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)" --target rmdb test_parser
ctest --test-dir build --output-on-failure -R test_parser
```

现场顺序：lex token → yacc token/type → `betweenCondition` 生成两个条件 → 接入 whereClause 两个位置 → parser 正反例 → 无索引/有索引边界测试。
