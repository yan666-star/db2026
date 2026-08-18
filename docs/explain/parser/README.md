# parser 模块讲解

## 0. parser 到底在干什么

parser 的输入是一串字符：

```sql
select a.id from a join b on a.id = b.id;
```

计算机不会直接理解 SQL。parser 分两步：

```text
lex.l：把字符切成单词
SELECT / IDENTIFIER(a) / '.' / IDENTIFIER(id) / FROM / ...

yacc.y：检查单词排列是否合法，并创建 C++ AST 对象
SelectStmt{表、列、条件、连接类型...}
```

AST 可以理解为“SQL 的结构化表格”，后面的 Analyze 不再处理原字符串，而是读取这张表格。

### 0.1 `tableList` 产生式逐字符解释

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

即把 B 放到已有 `[A]` 末尾，得到 `[A,B]`。

```cpp
$1.conds.insert($1.conds.end(), $5.begin(), $5.end());
```

即：将 ON 后面的所有条件追加到已有条件末尾。三个参数依次是“插到哪里、从哪里开始复制、复制到哪里结束”。

```cpp
$$ = $1;
```

即：把修改完成的 FromClause 作为这轮 tableList 的结果。

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

`caseless` 表示大小写不敏感。

| 名称 | 归属 | 作用 |
|---|---|---|
| `yytext` | flex | 当前匹配文本 |
| `yyleng` | flex | 当前文本长度 |
| `yylval` | flex->bison | token 携带的值，如整数、字符串 |
| `yylloc` | flex->bison | token 行列位置 |
| `sql_space` | lex.l 正则片段 | 组合多单词命令中的空白 |

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

`JoinType` 枚举定义了 INNER/LEFT/RIGHT/FULL；当前语法与运行链路只使用 `INNER_JOIN`。

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

## 8. parser 实现注意

1. `push_back` 加单个元素，`insert` 加一个范围。
2. AST 只保存语法，不查询表是否存在。

## 9. 编写 yacc 产生式的检查法

每写一条产生式，逐项确认：

```text
1. 右侧一共有几个符号
2. 每个 $n 的 C++ 类型
3. $$ 的 C++ 类型
4. 新对象由 shared_ptr 还是值保存
5. 返回结果是否把旧列表内容复制/移动丢失
```

`$$ = $1` 之后再修改 `$1` 不会自动反映到 `$$` 的值副本；推荐先修改 `$1` 再赋给 `$$`，或直接先 `$$=$1` 后只改 `$$`。
