# `yacc.y` 详细讲解

源码入口：[yacc.y](../../src/parser/yacc.y:1)

这个文件把词法分析器返回的 token 组合成 AST 对象。它不检查表是否存在，也不执行查询。

## 一、token 声明区

位置：[yacc.y](../../src/parser/yacc.y:27)

```yacc
%token SHOW TABLES CREATE TABLE DROP DESC INSERT INTO VALUES DELETE FROM ASC
%token WHERE UPDATE SET SELECT INT CHAR FLOAT INDEX AND JOIN ...
```

`%token JOIN` 告诉 Bison 存在一种名为 JOIN 的终结符。`lex.l` 中还必须有返回 JOIN 的规则。只改一边会出现两类问题：

```text
lex 返回了未声明 token -> 生成/编译失败
yacc 声明但 lex 不返回 -> 语法永远匹配不到
```

## 二、`%type` 与 `SemValue` 成员

位置：[yacc.y](../../src/parser/yacc.y:45)

例如：

```yacc
%type <sv_from_clause> tableList
%type <sv_table_ref> tableRef
%type <sv_conds> whereClause
```

尖括号内容不是 C++ 类型名，而是 `ast::SemValue` 的成员名。

因此：

```text
tableList 的 $n/$$ 读写 SemValue::sv_from_clause
tableRef  的 $n/$$ 读写 SemValue::sv_table_ref
whereClause 读写 SemValue::sv_conds
```

增加一种复杂非终结符时，先在 `SemValue` 增加能装下它的成员，再在 `%type` 绑定。

## 三、`$1`、`$3`、`$$` 的固定含义

产生式：

```yacc
tableList ',' tableRef
```

从左向右编号：

```text
$1 = tableList
$2 = ','
$3 = tableRef
$$ = 整条产生式归约后的 tableList
```

动作：

```cpp
$1.tables.push_back($3);
$$ = $1;
```

执行前：

```text
$1.tables = [departments]
$3        = employees
```

执行后：

```text
$1.tables = [departments, employees]
$$.tables = [departments, employees]
```

`push_back` 只改了动作中的 `$1` 对象。`$$=$1` 把它作为本次语法结果交给更高层。漏掉第二句，上层不会自动取得修改后的值。

## 四、`tableList` 基础分支

位置：[yacc.y](../../src/parser/yacc.y:606)

```yacc
tableList:
    tableRef
    {
        $$.tables = {$1};
        $$.conds = {};
    }
```

这是一张表时的初始状态。

`{$1}` 使用初始化列表创建只含一个 TableRef 的 vector。`{}` 创建空条件数组。

如果启用 `join_types`，这里也应初始化为空：

```cpp
$$.join_types = {};
```

一张表还没有 JOIN 边，所以不是放一个 INNER_JOIN。

## 五、逗号连接分支

```yacc
| tableList ',' tableRef
```

逗号 FROM 在当前 Planner 中通常会形成没有显式 ON 条件的连接。动作只追加表，不追加条件。

若系统用 `join_types.size()==tables.size()-1` 作为硬约束，逗号连接也要定义一个类型，通常按 CROSS/INNER 的既有语义处理。是否追加不能只看语法名称，要看 Planner 的数组对齐规则。

## 六、普通 `JOIN tableRef`

位置：[yacc.y](../../src/parser/yacc.y:617)

```cpp
$1.tables.push_back($3);
$$ = $1;
```

这里 `$2` 是 JOIN，`$3` 是右表。没有 ON，所以没有 `$5` 条件。

启用连接类型数组后应追加：

```cpp
$1.join_types.push_back(INNER_JOIN);
```

追加发生在 `$$=$1` 之前。

## 七、`JOIN tableRef ON whereClause`

位置：[yacc.y](../../src/parser/yacc.y:626)

符号编号：

```text
$1 tableList
$2 JOIN
$3 tableRef
$4 ON
$5 whereClause
```

动作中的两种容器操作：

```cpp
$1.tables.push_back($3);
$1.conds.insert($1.conds.end(), $5.begin(), $5.end());
```

`push_back($3)` 追加一个 TableRef。

`$5` 是 `vector<shared_ptr<BinaryExpr>>`。ON 中可能有多条 AND 条件，所以使用 `insert` 把整个区间追加到 `$1.conds` 末尾。

例子：

```sql
ON a.id=b.id AND b.score>60
```

`$5` 中有两个 BinaryExpr。执行 insert 后，两个元素按原顺序进入 conds。

## 八、`ANTI JOIN` 产生式中的编号

语法：

```yacc
tableList ANTI JOIN tableRef ON whereClause
```

编号：

```text
$1 tableList
$2 ANTI
$3 JOIN
$4 tableRef
$5 ON
$6 whereClause
```

正确动作：

```cpp
$1.tables.push_back($4);
$1.conds.insert($1.conds.end(), $6.begin(), $6.end());
$1.join_types.push_back(ANTI_JOIN);
$$ = $1;
```

不能照抄普通 JOIN 的 `$3/$5`。普通 JOIN 只有一个关键字，ANTI JOIN 有两个关键字，右表和条件都向后移动一位。

执行这个动作时还没有做反连接。这里只保存：

```text
新增的右表是谁
ON 条件有哪些
这一条边是 ANTI_JOIN
```

## 九、`SEMI JOIN` 产生式

SEMI 与 ANTI 的语法结构相同：

```yacc
| tableList SEMI JOIN tableRef ON whereClause
{
    $1.tables.push_back($4);
    $1.conds.insert($1.conds.end(), $6.begin(), $6.end());
    $1.join_types.push_back(SEMI_JOIN);
    $$ = $1;
}
```

Parser 层唯一差异是枚举值。SEMI 的“有匹配输出一次左行”由执行器实现。

## 十、SELECT 主产生式中的数据搬运

位置：[yacc.y](../../src/parser/yacc.y:492)

```yacc
SELECT selector FROM tableList optWhereClause
opt_group_by_clause opt_having_clause
```

对应参数大致为：

```text
$2 selector
$4 tableList
$5 WHERE 条件
$6 GROUP BY
$7 HAVING
```

代码先复制 `$4.conds`，再把 `$5` 追加进去：

```cpp
auto conds = std::move($4.conds);
conds.insert(conds.end(), $5.begin(), $5.end());
```

因此 Analyze 看到的 `SelectStmt::conds` 已经包含 JOIN ON 和 WHERE。若需要保持二者的不同语义，现有扁平 Condition 结构就不够，需要额外字段保存来源。

`std::move($4.conds)` 把 vector 内部缓冲交给局部变量 `conds`，减少复制。此后不要再依赖 `$4.conds` 的原内容。

启用 join_types 后要在构造 SelectStmt 后补：

```cpp
stmt->join_types = std::move($4.join_types);
```

## 十一、条件列表的 AND 语义

`whereClause` 通常把：

```sql
c1 AND c2 AND c3
```

展平成 vector `[c1,c2,c3]`。执行层 `eval_conditions()` 依次判断，任一 false 就返回 false。

增加 OR 时不能仍放进同一个无结构 vector，否则执行层无法知道分组。需要新增表达式树或“条件组”表示。

## 十二、动作代码的所有权

AST 节点大量使用 `shared_ptr`：

```cpp
std::make_shared<SelectStmt>(...)
```

多个上层节点可以共享子表达式，Bison 语义值复制 shared_ptr 时只增加引用计数，不复制整棵 AST。

vector 使用 `std::move` 后，源 vector 仍是合法对象，但内容不再保证。现场手敲时不要在 move 后又从源对象复制同一批条件。

## 十三、新关键字的修改顺序

以 SEMI 为例：

```text
ast.h 增加 SEMI_JOIN
  -> lex.l 返回 SEMI token
  -> yacc.y 声明 %token SEMI
  -> yacc.y 增加产生式并正确计算 $n
  -> FromClause 保存 join_types
  -> SelectStmt 接收 join_types
```

Parser 完成的判定标准不是“SQL 不报 syntax error”，而是 AST 中能够看到正确的表序、条件数组和连接类型数组。
