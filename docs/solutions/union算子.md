# Union 算子题解

## 可跳转目录

- [一、题目要求](#一题目要求)
- [二、总体设计](#二总体设计)
- [三、执行链路示意](#三执行链路示意)
- [四、关键改动](#四关键改动)
  - [1) Parser / AST 层](#1-parser--ast-层)
  - [2) Analyze 层](#2-analyze-层)
  - [3) Planner 层](#3-planner-层)
  - [4) Executor 层](#4-executor-层)
  - [5) Portal / 输出层](#5-portal--输出层)
- [五、平台评测要点](#五平台评测要点)
- [六、测试与结果](#六测试与结果)
- [七、常见错误与排查](#七常见错误与排查)
- [八、经验总结](#八经验总结)

## 按文件跳转

| 文件 | 作用 |
|---|---|
| [src/parser/lex.l](../src/parser/lex.l) | 识别 `UNION` 关键字 |
| [src/parser/yacc.y](../src/parser/yacc.y) | `union_query`、派生表、顶层 UNION、多列 `ORDER BY` |
| [src/parser/ast.h](../src/parser/ast.h) | `UnionStmt`、`TableRef.is_subquery` |
| [src/analyze/analyze.h](../src/analyze/analyze.h) | `DerivedTableInfo`、`analyze_union()` |
| [src/analyze/analyze.cpp](../src/analyze/analyze.cpp) | 列数/类型校验、类型提升、派生表列注册 |
| [src/optimizer/plan.h](../src/optimizer/plan.h) | `UnionPlan`、多键 `SortPlan` |
| [src/optimizer/planner.cpp](../src/optimizer/planner.cpp) | 派生表生成 Union 子计划 |
| [src/execution/executor_union.h](../src/execution/executor_union.h) | Union 执行：收集、转换、语义去重 |
| [src/execution/execution_sort.h](../src/execution/execution_sort.h) | 多列排序（字符串按实际内容比较） |
| [src/execution/execution_manager.cpp](../src/execution/execution_manager.cpp) | `output.txt` 格式化输出 |
| [src/execution/executor_projection.h](../src/execution/executor_projection.h) | `SELECT *` 透传 |
| [src/execution/executor_abstract.h](../src/execution/executor_abstract.h) | 列查找（支持无表名前缀） |
| [src/portal.h](../src/portal.h) | Plan → Executor 挂载 |
| [sql_tests/test_union.sql](../sql_tests/test_union.sql) | 6.1 回归脚本 |
| [sql_tests/test_union_62.sql](../sql_tests/test_union_62.sql) | 6.2 回归脚本 |
| [sql_tests/test_union_extra.sql](../sql_tests/test_union_extra.sql) | 去重 / 顶层 UNION 补充用例 |

---

## 一、题目要求

本题分两个测试点：

### 6.1 Union 算子 + ORDER BY

- 支持 `n ≥ 2` 个分支的 `UNION`，**去除所有对应列均相等的重复元组**
- Union 结果作为 **派生表** 出现在 `FROM` 子句，也支持 **顶层 UNION**：

```sql
-- 派生表形式（题面主测）
SELECT * FROM (SELECT ... UNION SELECT ...) AS alias ORDER BY ...;

-- 顶层 UNION（平台扩展测）
SELECT * FROM t1 UNION SELECT * FROM t2 ORDER BY ...;
```

- 外层 `ORDER BY` 作用于 Union 整体输出，支持 **多列 ASC/DESC**
- 输出列名继承 **第一个分支 R1** 的列名

### 6.2 类型兼容与异常处理

Analyze 阶段必须检查：

| 检查项 | 失败输出 |
|--------|----------|
| 各分支返回列数不一致 | `failure` |
| 对应位置列类型不兼容 | `failure` |
| `ORDER BY` 引用 Union 输出中不存在的列 | `failure` |

类型提升规则（Analyze 确定公共 schema，Executor 执行转换）：

| 分支类型组合 | 公共超类型 |
|-------------|-----------|
| `INT` + `FLOAT` | `FLOAT` |
| `CHAR(n)` + `CHAR(m)` | `CHAR(max(n,m))` |

---

## 二、总体设计

Union 以 **虚拟表 + 多路合并** 的方式接入查询计划：

```
SQL 文本
  → Parser：解析 UNION 分支 + 派生表/顶层 UNION + 多列 ORDER BY
  → Analyze：逐分支语义分析 → 校验兼容性 → 注册虚拟表 schema
  → Planner：派生表位置生成 UnionPlan → 外层套 SortPlan / ProjectionPlan
  → Executor：UnionExecutor 物化去重 → SortExecutor 排序 → 输出
```

与聚合查询的关键区别：

- Union 的 **ORDER BY 在外层**，由 `SortExecutor` 处理
- Union 分支 **不允许再嵌套派生表**（`allow_derived=false`）
- 派生表列不在 `SmManager` 中，Analyze 需维护 **虚拟 `ColMeta` 列表**
- 平台比对的是数据库目录下的 **`output.txt`**，不是客户端缓冲区的对齐格式

---

## 三、执行链路示意

以题面 6.1 查询为例：

```sql
SELECT * FROM
    (SELECT * FROM orders1
     UNION SELECT * FROM orders2
     UNION SELECT * FROM orders3) AS all_orders
ORDER BY amount DESC;
```

生成的计划树：

```text
Projection(columns=[*], rows=6)
  Sort(column=amount, order=DESC, rows=6)
    Union(branches=3, rows=6)
      Projection(...) → Scan(orders1)
      Projection(...) → Scan(orders2)
      Projection(...) → Scan(orders3)
```

执行顺序：

1. 三个分支各自 Scan + Project，产生原始 schema 元组
2. `UnionExecutor` 逐分支读取，**统一 schema + 哈希去重**，物化 6 条
3. `SortExecutor` 对 Union 输出按 `amount DESC` 排序
4. 外层 `ProjectionExecutor` 透传输出

---

## 四、关键改动

### 1) Parser / AST 层

**新增 AST 结构**（`src/parser/ast.h`）：

```cpp
struct UnionStmt : public TreeNode {
    std::vector<std::shared_ptr<SelectStmt>> branches;
};

struct TableRef {
    std::string tab_name;
    std::string alias;
    bool is_subquery = false;
    std::shared_ptr<UnionStmt> union_subquery;
};
```

**语法规则**（`src/parser/yacc.y`）：

```yacc
union_branch:
    SELECT selector FROM tableList optWhereClause opt_group_by_clause opt_having_clause

union_query:
    union_branch
    | union_query UNION union_branch

tableRef:
    '(' union_query ')' AS tbName    /* 派生表 */
    | '(' union_query ')' tbName

dml:
    union_query opt_order_clause opt_limit_clause   /* 顶层 UNION */
```

顶层 UNION 在语法层包装为 `SELECT * FROM (_union_r) AS _union_r` 的等价结构，Analyze 侧与普通派生表走同一套路径。

**多列 ORDER BY**：`order_clause` 返回 `vector<OrderBy>`，写入 `SelectStmt.orders`。

---

### 2) Analyze 层

**核心数据结构**（`src/analyze/analyze.h`）：

```cpp
struct DerivedTableInfo {
    std::shared_ptr<ast::UnionStmt> union_stmt;
    std::vector<ColMeta> cols;                         // 提升后的虚拟列
    std::vector<std::shared_ptr<Query>> branch_queries; // 各分支已分析 Query
};
```

**`analyze_union()` 流程**：

1. 对每个分支调用 `analyze_select(branch, allow_derived=false)`
2. 用 `get_branch_output_cols()` 获取各分支输出列（按位置对齐）
3. 列数不一致 → `throw RMDBError("failure")`
4. 逐列调用 `promote_union_col()` 计算公共类型
5. 列名取 **R1**，`tab_name` 设为派生表别名

**ORDER BY 错误处理**：`check_column()` 抛出 `ColumnNotFoundError` 时，Analyze 捕获并转为 `RMDBError("failure")`，保证 `output.txt` 写入 `failure`。

**派生表列获取**：`get_query_cols()` 遇到派生表别名时，直接从 `derived_tables[alias].cols` 取列，而非查 `SmManager`。

---

### 3) Planner 层

**UnionPlan**（`src/optimizer/plan.h`）：

```cpp
class UnionPlan : public Plan {
    std::vector<std::shared_ptr<Plan>> branches_;
    std::vector<ColMeta> out_cols_;  // 统一输出 schema
    size_t len_;
};
```

**make_one_rel()** 中，若 `tables[i]` 是派生表别名：

```cpp
if (query->derived_tables.count(tables[i])) {
    for (auto &bq : info.branch_queries)
        branch_plans.push_back(generate_subquery_plan(bq));
    table_scan_executors[i] = std::make_shared<UnionPlan>(branch_plans, info.cols);
}
```

`generate_subquery_plan()` 对每个分支：`make_one_rel` + `ProjectionPlan`，保证分支输出列与 Analyze 阶段一致。

**多列 SortPlan**：`generate_sort_plan()` 读取 `query->order_bys`，构造带 `sort_cols_` / `is_descs_` 的 `SortPlan`。

---

### 4) Executor 层

**UnionExecutor**（`src/execution/executor_union.h`）：

```text
beginTuple():
  for each branch executor:
    for each tuple:
      unified = unify_row(rec, branch_cols)   // 按位置做类型转换
      key = build_dedup_key(unified)          // 按列语义构造去重键
      if hash_set.insert(key): tuples_.push_back(unified)
```

`convert_value()` 负责执行期转换：

- `INT → FLOAT`：`static_cast<float>(*(int*)src)`
- `CHAR(n) → CHAR(m)`：`memset` 后 `memcpy`，目标列零填充
- 同类型：直接 `memcpy`

**去重键 `build_dedup_key()`**：按列类型取语义值，而非原始 padding 字节：

- `INT` / `FLOAT`：取定长二进制
- `STRING`：取 `\0` 前的实际字符串，列间用 `\x1f` 分隔

这样 `(1, 150 INT, 'Beijing')` 与 `(1, 150.0 FLOAT, 'Beijing')` 提升后可正确去重。

**SortExecutor 多列排序**（`src/execution/execution_sort.h`）：

- 数值列：`compare_col_value()`
- 字符串列：截断 `\0` 后按字典序比较（对齐 RMDB2025，避免 CHAR padding 影响排序）

**ProjectionExecutor**：当 `SELECT *` 且列顺序一致时设置 `is_sel_all_`，直接透传子算子记录，避免多余拷贝。

---

### 5) Portal / 输出层

`portal.h` 中新增：

```cpp
// UnionPlan → UnionExecutor（每个分支递归 convert_plan_executor）
// collect_output_cols：UnionPlan 返回 out_cols_ 的列名
// SortPlan → SortExecutor(prev, SortPlan*)  // 单列/多列统一入口
```

**`output.txt` 格式化**（`execution_manager.cpp`）：

| 场景 | FLOAT 输出格式 |
|------|---------------|
| 普通 SELECT / Union | 自然格式：`560`、`230.5`、`199.99` |
| 聚合 SELECT | 固定 6 位小数：`74.500000` |

字符串输出使用 `find_last_not_of('\0')` 截断（对齐 RMDB2025），按实际长度写入，不输出 CHAR 尾部填充。

---

## 五、平台评测要点

平台通过比对 **`output.txt`** 判断正误（`test_union3` 测错误路径，其余测查询结果）。

| 测试点 | 典型场景 | 关键对齐项 |
|--------|----------|-----------|
| test_union1 | 6.1 三表 UNION + ORDER BY | FLOAT 自然格式；6 行去重；排序 |
| test_union2 | 6.1 变体 | 同上 |
| test_union3 | 6.2 错误用例 | `output.txt` 写 `failure` |
| test_union4 | 6.2 类型提升 + 多列排序 | INT→FLOAT 提升；CHAR 按实际长度输出 |
| test_union5 | 顶层 UNION / 跨类型去重 | 顶层语法；语义去重键 |

**曾导致 platform mismatch 的根因**：

1. **FLOAT 不用 `setprecision(6)`**：不输出 `560.000000` / `199.990005`，题目没有说明保留几位小数！和第五题一致，保留六位小数！！！
2. **字符串排序/输出未 trim padding**：CHAR 列排序或展示受 `\0` 填充影响
3. **去重键直接用原始字节**：类型提升后 padding 不同导致重复行未合并

---

## 六、测试与结果

本地回归（每次使用**新数据库目录**，避免表已存在）：

```bash
cd /root/db2026
make -C build -j4

# 6.1
rm -rf union_test_db && ./build/bin/rmdb union_test_db &
sleep 2 && python3 sql_tests/run_sql.py sql_tests/test_union.sql
cat union_test_db/output.txt

# 6.2
pkill -f bin/rmdb; rm -rf union_test_db62 && ./build/bin/rmdb union_test_db62 &
sleep 2 && python3 sql_tests/run_sql.py sql_tests/test_union_62.sql
cat union_test_db62/output.txt

# 补充：跨类型去重 + 顶层 UNION
pkill -f bin/rmdb; rm -rf union_extra_db && ./build/bin/rmdb union_extra_db &
sleep 2 && python3 sql_tests/run_sql.py sql_tests/test_union_extra.sql
cat union_extra_db/output.txt
```

> 服务端 `chdir` 到数据库目录后写入 `output.txt`，路径为 `./<db_name>/output.txt`，不是项目根目录。

### 6.1 预期 output.txt

```text
| order_id | amount | region |
| 5 | 560 | Chengdu |
| 2 | 230.5 | Shanghai |
| 6 | 199.99 | Wuhan |
| 1 | 150 | Beijing |
| 4 | 120 | Shenzhen |
| 3 | 89.99 | Guangzhou |
```

- 共 **6 行**（`(1,150,Beijing)` 在三表各出现一次，去重后保留 1 条）
- 按 `amount DESC` 排序正确

### 6.2 预期 output.txt

**错误用例**（每个语句单独一行）：

```text
failure
failure
failure
```

**类型提升 + 多列排序**（4 行）：

```text
| order_id | amount | region |
| 3 | 230.5 | LongRegionNameHere |
| 1 | 150 | Beijing |
| 4 | 120 | Shenzhen |
| 2 | 89 | Shanghai |
```

**跨类型去重**（`(1,150,'Beijing')` INT/FLOAT 合并，共 3 行）：

```text
| order_id | amount | region |
| 3 | 230.5 | Shenzhen |
| 1 | 150 | Beijing |
| 2 | 89 | Shanghai |
```

---

## 七、常见错误与排查

| 现象 | 可能原因 | 修复方向 |
|------|----------|----------|
| platform: answer mismatches | FLOAT 用了 6 位固定小数 | 普通查询用 `ostringstream << f` |
| 去重后行数偏多 | 去重键含 CHAR padding | 用 `build_dedup_key()` 语义比较 |
| 排序结果不对 | CHAR 列 `memcmp` 含 padding | Sort 中 trim 后比较 |
| Parser Error: syntax error | 未添加 `UNION` 或顶层语法 | 检查 `lex.l` / `yacc.y` |
| Table not found: all_orders | 派生表别名当真实表查库 | `get_query_cols()` 区分派生表 |
| INT 列未提升为 FLOAT | Executor 未做 `convert_value` | 检查 `unify_row()` |
| 多列 ORDER BY 只按第一列排 | Parser 丢弃后续排序键 | 使用 `SelectStmt.orders` 向量 |
| ORDER BY 未知列无 failure | 未捕获 `ColumnNotFoundError` | Analyze 转 `RMDBError("failure")` |
| 本地 output.txt 找不到 | 看错路径 | 查看 `<db_name>/output.txt` |

---

## 八、经验总结

1. **Union = 虚拟表 + 多路合并**：Parser 解决语法，Analyze 解决 schema，Executor 解决数据统一。
2. **类型提升分两阶段**：Analyze 决定目标 schema，Executor 按位置转换字节。
3. **去重按语义而非 padding**：先 `unify_row`，再 `build_dedup_key` 哈希。
4. **ORDER BY 在外层**：计划树顺序为 `Union → Sort → Project`。
5. **输出格式分场景**：Union 用自然浮点，聚合用 6 位小数；平台只比对 `output.txt`。
6. **参考 RMDB2025 的通用逻辑**：字符串 trim、投影透传、Sort 字符串比较方式，但 Union 本身需独立实现。
