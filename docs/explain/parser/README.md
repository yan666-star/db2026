# parser：线下资格赛修改百科

## 0. parser 到底在干什么

你输入的是一串字符：

```sql
select a.id from a semi join b on a.id = b.id;
```

计算机不会直接理解 SQL。parser 分两步：

```text
lex.l：把字符切成单词
SELECT / IDENTIFIER(a) / '.' / IDENTIFIER(id) / FROM / ...

yacc.y：检查单词排列是否合法，并创建 C++ AST 对象
SelectStmt{表、列、条件、连接类型...}
```

AST 可以理解为“SQL 的结构化表格”，后面的 Analyze 不再处理原字符串，而是读取这张表格。

### 0.1 为什么新增关键字通常改三个地方

以 SEMI 为例：

1. `lex.l`：告诉词法器字符 `SEMI` 是关键字，不是普通表名。
2. `yacc.y`：告诉语法器 `SEMI JOIN` 可以出现在 tableList 中。
3. `ast.h`：给 AST 一个位置保存“这是 SEMI JOIN”。

如果只改 lex，语法器收到 SEMI 却不知道怎么排列；只改 yacc 而 lex 把 SEMI 当 IDENTIFIER，也匹配不到；两者都改但 AST 不保存类型，出了 parser 后又变回普通 JOIN。

### 0.2 `tableList` 产生式逐字符解释

```yacc
tableList JOIN tableRef ON whereClause
```

它不是函数调用，而是一条语法模板：

| 位置 | 符号 | C++ 语义值 |
|---|---|---|
| `$1` | tableList | 已解析的 FromClause，例如 tables=[A] |
| `$2` | JOIN | 只有 token 身份，通常没有业务值 |
| `$3` | tableRef | 新右表，例如 B |
| `$4` | ON | token |
| `$5` | whereClause | 条件 vector |
| `$$` | 左侧 tableList | 这条规则最终返回的新 FromClause |

动作：

```cpp
$1.tables.push_back($3);
```

翻译成人话：把 B 放到已有 `[A]` 末尾，得到 `[A,B]`。

```cpp
$1.conds.insert($1.conds.end(), $5.begin(), $5.end());
```

翻译：将 ON 后面的所有条件追加到已有条件末尾。三个参数依次是“插到哪里、从哪里开始复制、复制到哪里结束”。

```cpp
$$ = $1;
```

翻译：把修改完成的 FromClause 作为这轮 tableList 的结果。

### 0.3 SEMI JOIN 产生式逐参数解释

```yacc
tableList SEMI JOIN tableRef ON whereClause
```

位置发生变化：

```text
$1=旧tableList
$2=SEMI
$3=JOIN
$4=右表
$5=ON
$6=条件数组
```

所以代码必须是：

```cpp
$1.tables.push_back($4);       // 不是 $3，$3 是 JOIN token
$1.join_types.push_back(SEMI_JOIN);
$1.conds.insert($1.conds.end(), $6.begin(), $6.end());
$$ = $1;
```

例子：

```sql
from departments semi join employees
on departments.id = employees.dept_id
```

产生：

```text
tables=[departments, employees]
join_types=[SEMI_JOIN]
conds=[departments.id = employees.dept_id]
```

这里 `join_types` 不能按条件数保存，因为一个 JOIN 后可能有两个 ON 条件，但连接类型仍只有一个。

## 1. 目录职责

parser 把 SQL 字符串转换成 AST，只回答“用户写了什么”，不访问表目录，也不执行查询。

```text
lex.l：字符 -> token
yacc.y：token -> 语法结构
ast.h：语法树数据结构
ast.cpp：全局 parse_tree
ast_printer.h：打印 AST
```

## 2. 哪些文件手改

| 文件 | 是否手改 | 作用 |
|---|---|---|
| [lex.l](../../src/parser/lex.l:1) | 是 | 关键字、标识符、字面量词法规则 |
| [yacc.y](../../src/parser/yacc.y:1) | 是 | SQL 文法和 AST 构造动作 |
| [ast.h](../../src/parser/ast.h:44) | 是 | AST 节点、枚举、SemValue |
| [ast.cpp](../../src/parser/ast.cpp:1) | 偶尔 | `parse_tree` 定义 |
| [ast_printer.h](../../src/parser/ast_printer.h:1) | 新节点需要 | 调试输出 |
| `lex.yy.*` | 否 | flex 自动生成 |
| `yacc.tab.*` | 否 | bison 自动生成 |
| `*~origin_master` | 否 | 历史备份，不属于运行入口 |

## 3. lex.l

关键规则：

```lex
%option caseless
"SELECT" { return SELECT; }
```

`caseless` 表示大小写不敏感。新增关键字一般同时做两件事：在 lex.l 返回 token，在 yacc.y `%token` 声明。

| 名称 | 归属 | 作用 |
|---|---|---|
| `yytext` | flex | 当前匹配文本 |
| `yyleng` | flex | 当前文本长度 |
| `yylval` | flex->bison | token 携带的值，如整数、字符串 |
| `yylloc` | flex->bison | token 行列位置 |
| `sql_space` | lex.l 正则片段 | 组合多单词命令中的空白 |

关键字规则必须放在标识符规则之前，否则新关键字可能被识别为 `IDENTIFIER`。

## 4. yacc.y 中 `$1/$3/$$`

产生式：

```yacc
tableList JOIN tableRef ON whereClause
```

编号：

```text
$1=tableList  $2=JOIN  $3=tableRef  $4=ON  $5=whereClause
$$=左侧新生成的 tableList
```

`push_back` 加一个元素：

```cpp
$1.tables.push_back($3);
```

`insert(end, begin, end)` 追加一整个数组：

```cpp
$1.conds.insert($1.conds.end(), $5.begin(), $5.end());
```

`$$ = $1` 把修改后的右侧语义值作为左侧结果返回。

## 5. AST 核心类型

| 类型 | 关键变量 | 作用 |
|---|---|---|
| `TreeNode` | 无 | 所有 AST 多态基类 |
| `Col` | `tab_name/col_name` | 用户书写的列引用，尚未验证存在性 |
| `Value` 子类 | `val` | INT/FLOAT/STRING/BOOL 字面量 |
| `BinaryExpr` | `lhs/op/rhs` | 单个比较表达式 |
| `TableRef` | 表名、别名、子查询 | FROM 的一项 |
| `FromClause` | `tables/conds` | 表序列与 JOIN ON 条件 |
| `SelectItem` | `expr/alias` | SELECT 列、聚合表达式和别名 |
| `SelectStmt` | 见下表 | 完整 SELECT AST |
| `SetClause` | 左列、右值/右列、算术符 | UPDATE SET 项 |

## 6. SelectStmt 变量百科

| 变量 | 类型 | 谁写入 | 后续谁读取 | 作用 |
|---|---|---|---|---|
| `select_items` | `vector<shared_ptr<SelectItem>>` | selector 产生式 | Analyze | SELECT 项 |
| `tabs` | `vector<TableRef>` | tableList | Analyze | FROM 顺序 |
| `conds` | `vector<BinaryExpr>` | ON+WHERE | Analyze | 原始谓词 |
| `group_bys` | 列数组 | GROUP BY | Analyze | 分组列 |
| `havings` | HavingExpr 数组 | HAVING | Analyze | 聚合后条件 |
| `orders` | OrderBy 数组 | ORDER BY | Analyze | 多列排序 |
| `limit_num` | int | LIMIT | Analyze/Planner | -1 为无限制 |
| `is_select_all` | bool | 构造函数 | Analyze | selector 为空代表 `*` |
| `is_union` | bool | 集合语法 | Analyze | 顶层集合查询 |
| `union_branches` | SELECT 数组 | 集合语法 | Analyze | 各分支 |
| `set_ops` | SetOpType 数组 | 集合语法 | Analyze/Union | 相邻分支运算符 |

当前 `JoinType` 已定义 INNER/LEFT/RIGHT/FULL/ANTI，但运行链路不等于全部启用；应检查 `SelectStmt/Query/JoinPlan/Portal` 是否真正传递类型。

## 7. SemValue

[SemValue](../../src/parser/ast.h:448) 是 bison 每个语法符号可能携带的值集合。

| 字段 | 对应内容 |
|---|---|
| `sv_int/sv_float/sv_str/sv_bool` | 字面量/token 数据 |
| `sv_comp_op` | 比较符 |
| `sv_table_ref` | 单个表引用 |
| `sv_from_clause` | 表列表和 JOIN 条件 |
| `sv_expr/sv_cond/sv_conds` | 表达式/条件/条件数组 |
| `sv_select_item(s)` | SELECT 项 |
| `sv_set_op` | 集合运算类型 |

新增非终结符必须在 `%type <字段>` 中选择和产生式 `$$` 类型一致的字段。

## 8. 资格赛最小改法

### 新 WHERE 语法

优先在 parser 重写为已有 `BinaryExpr`。例如 BETWEEN 展开成 `>=` 与 `<=`，后续层无需新增运算符。

### 新 JOIN 关键字

需要：token、文法、FromClause/SelectStmt 保存类型。仅把右表和条件 push_back 不能区分 INNER 与 ANTI。

### 新聚合函数

增加 token、AggFuncType、agg_func 产生式；随后必须同步 common 的 AggType 和执行器分支。

### 新语句

新增 TreeNode 子类，语句产生式构造它；Optimizer 必须能 dynamic_cast 识别。

## 9. parser 修改注意

1. 关键字、token、产生式、AST 字段必须形成闭环。
2. 产生式增加 token 后重新数 `$1...$n`，标点与关键字也占位置。
3. `push_back` 加单个元素，`insert` 加一个范围。
4. AST 只保存语法，不查询表是否存在。
5. 不直接修改生成的 `lex.yy.cpp/yacc.tab.cpp`。

## 10. 专题一：新增 BETWEEN，为什么只改 parser 就够

目标：

```sql
where salary between 7000 and 9000
```

后续执行层已经认识 `OP_GE` 与 `OP_LE`，因此不要新增 `OP_BETWEEN`。最小数据流：

```text
BETWEEN 语法
 -> 产生两个 BinaryExpr
 -> SelectStmt.conds 中出现两项
 -> Analyze 自动转成两个 Condition
```

第一处，在 lex.l 关键字区加入：

```lex
"BETWEEN" { return BETWEEN; }
```

第二处，在 yacc.y 声明：

```yacc
%token BETWEEN
%type <sv_conds> betweenCondition
```

第三处，产生两个已有条件：

```yacc
betweenCondition:
    col BETWEEN value AND value {
        auto lower = std::make_shared<BinaryExpr>($1, SV_OP_GE, $3);
        auto upper = std::make_shared<BinaryExpr>($1, SV_OP_LE, $5);
        $$ = {lower, upper};
    }
;
```

这里 `$1` 和两个 BinaryExpr 共享同一个 `shared_ptr<Col>` 是安全的，因为 AST 在分析期间只读；`lower/upper` 各自拥有一个 shared_ptr 引用。

第四处，把 `betweenCondition` 接到 whereClause。它返回 vector，因此追加时用 insert：

```yacc
| whereClause AND betweenCondition {
    $$ = $1;
    $$.insert($$.end(), $3.begin(), $3.end());
}
```

如果误用 `push_back($3)`，相当于向 `vector<BinaryExpr指针>` 插入一个 `vector`，类型不匹配。

## 11. 专题二：新增 SELECT DISTINCT 的最小字段传递

parser 只负责留下一个布尔事实：

```cpp
// SelectStmt
bool is_distinct = false;
```

yacc 分支：

```yacc
SELECT DISTINCT selector FROM tableList optWhereClause {
    auto stmt = std::make_shared<SelectStmt>(/* 原参数 */);
    stmt->is_distinct = true;
    $$ = stmt;
}
```

为什么用默认 false：普通 SELECT 的构造代码完全不改。为什么不在 AST 保存 `unordered_set`：去重属于执行状态，AST 只描述语法。

新增字段后必须顺着查：

```text
SelectStmt.is_distinct
 -> Query.is_distinct
 -> ProjectionPlan.is_distinct_
 -> ProjectionExecutor
```

parser 层完成的判据只是 `ast::parse_tree` 中能看到 true。

## 12. 专题三：新增连接关键字时保存“边类型”

只保存 `tables` 和 `conds` 无法区分：

```sql
A JOIN B ON ...
A ANTI JOIN B ON ...
```

两者都有 `[A,B]` 和相同条件。因此 FromClause 要增加：

```cpp
std::vector<JoinType> join_types;
```

约定必须写清：

```text
tables       = [A, B, C]
join_types   = [A与B的类型, (A连接B)与C的类型]
数组长度     = tables.size() - 1
```

第一张表产生式初始化空数组。每新增右表，普通 JOIN push INNER_JOIN，ANTI JOIN push ANTI_JOIN。不要按 ON 条件数 push，因为一个 JOIN 可以有多个 ON 条件。

```yacc
| tableList ANTI JOIN tableRef ON whereClause {
    $1.tables.push_back($4);
    $1.join_types.push_back(ANTI_JOIN);
    $1.conds.insert($1.conds.end(), $6.begin(), $6.end());
    $$ = $1;
}
```

## 13. 专题四：新增比较操作符 LIKE

如果题目不允许简化重写，就要新增真正的操作符：

```text
lex.l 返回 LIKE
yacc.y condition 分支构造专用 SvCompOp
ast.h::SvCompOp 加 SV_OP_LIKE
common::CompOp 加 OP_LIKE
Analyze::convert_sv_comp_op 转换
execution_eval 实现字符串匹配
```

不能只在 yacc 把 LIKE 映射成 EQ，因为 `%`/`_` 通配语义不同。若赛题只要求 `LIKE 'abc'` 且不含通配符，题目明确允许时才可重写为 EQ。

## 14. 修改 yacc 的逐行检查法

每写一条产生式，在纸上列：

```text
1. 右侧一共有几个符号
2. 每个 $n 的 C++ 类型
3. $$ 的 C++ 类型
4. 新对象由 shared_ptr 还是值保存
5. 返回结果是否把旧列表内容复制/移动丢失
```

`$$ = $1` 之后再修改 `$1` 不会自动反映到 `$$` 的值副本；推荐先修改 `$1` 再赋给 `$$`，或直接先 `$$=$1` 后只改 `$$`。
