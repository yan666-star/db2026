# `ast.h` 详细讲解

源码入口：[ast.h](../../src/parser/ast.h:20)

这个文件定义 **抽象语法树（AST）节点**。lex.l 切出 token，yacc.y 的动作代码把这些 token 组装成 AST 对象，Analyze 再读这棵 AST 做语义分析。ast.h 只描述"SQL 长什么样"，不查询表，也不执行。

## 〇、AST 树长什么样

以 `select a.id from a join b on a.id = b.id` 为例，yacc.y 归约完成后得到一棵对象树：

```text
SelectStmt
 ├─ tabs        = [TableRef(a), TableRef(b)]     ← 表引用数组
 ├─ conds       = [BinaryExpr(a.id = b.id)]      ← ON+WHERE 条件数组
 └─ select_items= [SelectItem(Col(a.id))]
```

## 一、`JoinType` 枚举（连接类型）

位置：[ast.h](../../src/parser/ast.h:19)

```cpp
enum JoinType { INNER_JOIN, LEFT_JOIN, RIGHT_JOIN, FULL_JOIN };
```

**当前语法与运行链路只使用 `INNER_JOIN`**；LEFT/RIGHT/FULL 仅在枚举中保留，当前没有产生式能构造它们。

枚举是 `enum` 不是 `enum class`，所以写代码时直接用 `INNER_JOIN` 不带作用域前缀。

## 二、`SetOpType` 枚举（集合算子）

位置：[ast.h](../../src/parser/ast.h:53)

```cpp
enum class SetOpType {
    UNION, UNION_ALL, INTERSECT, INTERSECT_ALL, EXCEPT, EXCEPT_ALL
};
```

注意它是 `enum class`，使用时要写 `SetOpType::UNION`。`*_ALL` 表示不去重。

## 三、`ast` 命名空间内的基础枚举

### 3.1 `SvType`（AST 层数据类型）

```cpp
enum SvType { SV_TYPE_INT, SV_TYPE_FLOAT, SV_TYPE_STRING, SV_TYPE_BOOL };
```

`SvType` 是 **AST 层**的字面量类型，建表语句 `int`/`char`/`float` 用它。后面 Analyze 会把它转成 common 层的 `ColType`（`TYPE_INT` 等）。

### 3.2 `SvCompOp`（AST 层比较符）

```cpp
enum SvCompOp { SV_OP_EQ, SV_OP_NE, SV_OP_LT, SV_OP_GT, SV_OP_LE, SV_OP_GE };
```

`SV_` 前缀是 **S**emantic **V**alue 的意思，强调它存在 yacc 语义值里。Analyze 的 `convert_sv_comp_op` 把它一对一映射成 common 层的 `CompOp`。

### 3.3 `OrderByDir` / `SetKnobType` / `TransactionIsolationLevel`

```cpp
enum OrderByDir { OrderBy_DEFAULT, OrderBy_ASC, OrderBy_DESC };
enum SetKnobType { EnableNestLoop, EnableSortMerge };
enum TransactionIsolationLevel { SnapshotIsolation, Serializable };
```

## 四、`TreeNode` 基类

```cpp
struct TreeNode {
    virtual ~TreeNode() = default;  // enable polymorphism
};
```

**所有 AST 节点都继承 `TreeNode`**。它唯一的作用是提供虚析构，让多态 `dynamic_pointer_cast<SelectStmt>(root)` 能安全向下转型。Analyze::do_analyze 的参数就是 `shared_ptr<ast::TreeNode>`。

## 五、DDL 节点逐个解释

| 节点 | 关键字段 | 对应 SQL |
|---|---|---|
| `Help` / `ShowTables` / `StaticCheckpoint` | 无 | `help` / `show tables` / `static_checkpoint` |
| `TxnBegin/Commit/Abort/Rollback` | 无 | 事务命令 |
| `TypeLen` | `SvType type; int len` | `int`/`char(8)` 的类型+长度 |
| `Field` | 空基类 | 字段抽象 |
| `ColDef` | `col_name` + `shared_ptr<TypeLen>` | 建表的一列 |
| `CreateTable` | `tab_name` + `fields` | `create table` |
| `DropTable` | `tab_name` | `drop table` |
| `CreateIndex` / `DropIndex` | `tab_name` + `col_names` | 索引列 |
| `ShowIndex` | `tab_name` | `show index` |

注意 `CreateIndex::col_names` 是 `vector<string>`，支持复合索引多列。DDL 语句的字段通常只有表名和列名，Analyze 不用做复杂处理。

## 六、表达式节点逐个解释

### 6.1 `Value` 及其子类（字面量）

```cpp
struct IntLit    : public Value { int val; };
struct FloatLit  : public Value { float val; };
struct StringLit : public Value { std::string val; };
struct BoolLit   : public Value { bool val; };
```

它们用 `dynamic_pointer_cast` 区分。Analyze 的 `convert_sv_value` 把四种字面量转成 common 层 `Value`。

### 6.2 `Col`（列引用）

```cpp
struct Col : public Expr {
    std::string tab_name;
    std::string col_name;
};
```

`Col` 是用户**写出来的列**，`tab_name` 可能为空（没写表名前缀）。Analyze 会查元数据把它绑定成 `TabCol`（补全真表名）。**AST 层不做存在性检查**。

### 6.3 `AggFunc`（聚合函数）

```cpp
enum AggFuncType { AGG_COUNT, AGG_MAX, AGG_MIN, AGG_SUM, AGG_AVG };

struct AggFunc : public Expr {
    AggFuncType func_type;
    bool is_star = false;              // 是否 COUNT(*)
    std::shared_ptr<Col> col;          // 聚合列（COUNT(*) 时为 null）
};
```

`is_star` 用于 `COUNT(*)`：此时没有 `col`。

### 6.4 `SelectItem`（SELECT 项）

```cpp
struct SelectItem : public TreeNode {
    std::shared_ptr<Expr> expr;    // 是 Col 还是 AggFunc 用 dynamic_cast 区分
    std::string alias;
};
```

### 6.5 `HavingExpr`（HAVING 条件）

```cpp
struct HavingExpr : public TreeNode {
    std::shared_ptr<AggFunc> lhs;   // 左端必须是聚合函数
    SvCompOp op;
    std::shared_ptr<Value> rhs;     // 右端是字面量
};
```

### 6.6 `SetClause`（UPDATE SET 项）

```cpp
struct SetClause : public TreeNode {
    std::string col_name;
    std::string rhs_col_name;       // 列运算时用
    char arithmetic_op = '\0';      // 非 '\0' 表示列间算术
    std::shared_ptr<Value> val;
};
```

`set salary = salary + 10`：`col_name=salary`，`rhs_col_name=salary`，`arithmetic_op='+'`，`val=10`。仅 `set a = 5` 时 `arithmetic_op='\0'`。

### 6.7 `BinaryExpr`（二元比较表达式）

```cpp
struct BinaryExpr : public TreeNode {
    std::shared_ptr<Expr> lhs;   // 通常是 Col
    SvCompOp op;
    std::shared_ptr<Expr> rhs;   // Col 或 Value
};
```

**这是条件的基础单位**。`a.id = b.id` 和 `salary >= 7000` 都是 `BinaryExpr`。右端是 `Col` 还是 `Value` 由 Analyze 用 `dynamic_pointer_cast` 判断：是 `Value` 则 `is_rhs_val=true`，是 `Col` 则 `is_rhs_val=false`。条件数组 `conds` 就是 `vector<shared_ptr<BinaryExpr>>`。

## 七、`TableRef` 与 `FromClause`

### 7.1 `TableRef`

```cpp
struct TableRef {
    std::string tab_name;
    std::string alias;
    bool is_subquery = false;              // 是否 FROM 子查询
    std::shared_ptr<SelectStmt> subquery;  // 子查询 AST
};
```

`from student s` → `tab_name=student, alias=s`。`from (select ...) t` → `is_subquery=true, alias=t`。

### 7.2 `FromClause`

位置：[ast.h](../../src/parser/ast.h:310)

```cpp
struct FromClause {
    std::vector<ast::TableRef> tables;                          // 表顺序
    std::vector<std::shared_ptr<ast::BinaryExpr>> conds;        // ON+WHERE 展平
};
```

**`FromClause` 是 `tableList` 产生式的语义值类型**。yacc 动作不断向它 `push_back` 表和条件。

## 八、`SelectStmt`（查询主节点）逐个字段

位置：[ast.h](../../src/parser/ast.h:377)

```cpp
struct SelectStmt : public TreeNode {
    std::vector<std::shared_ptr<SelectItem>> select_items;  // SELECT 项
    std::vector<TableRef> tabs;                             // FROM 表
    bool is_explain_analyze = false;                        // EXPLAIN ANALYZE
    bool is_select_all = false;                             // 是否 SELECT *
    bool is_union = false;                                  // 是否集合查询
    std::vector<std::shared_ptr<SelectStmt>> union_branches; // 集合分支
    std::vector<SetOpType> set_ops;                          // 分支间算子
    std::vector<std::shared_ptr<BinaryExpr>> conds;          // ON+WHERE
    std::vector<std::shared_ptr<JoinExpr>> jointree;         // 框架字段，未用
    std::vector<std::shared_ptr<Col>> group_bys;             // GROUP BY 列
    std::vector<std::shared_ptr<HavingExpr>> havings;        // HAVING
    bool has_limit = false;                                  // 是否有 LIMIT
    int limit_num = -1;                                      // LIMIT n；-1 无限制
    bool has_sort;                                           // 是否有 ORDER BY
    std::shared_ptr<OrderBy> order;                          // 单列排序（旧）
    std::vector<std::shared_ptr<OrderBy>> orders;            // 多列排序
    ...
};
```

逐个解释：

| 字段 | 类型 | 谁写 | 谁读 | 作用 |
|---|---|---|---|---|
| `select_items` | vector | yacc selector 产生式 | Analyze | SELECT 每个项 |
| `tabs` | vector\<TableRef> | yacc tableList | Analyze | FROM 表序 |
| `is_explain_analyze` | bool | yacc | Analyze | EXPLAIN ANALYZE 标记 |
| `is_select_all` | bool | 构造函数 | Analyze | `select_items.empty()` 时为 true，代表 `*` |
| `is_union` | bool | yacc 集合语法 | Analyze | 顶层是集合查询 |
| `union_branches` | vector\<SelectStmt> | yacc | Analyze | 各分支 |
| `set_ops` | vector\<SetOpType> | yacc | Analyze | 相邻分支算子，长度=branches-1 |
| `conds` | vector\<BinaryExpr> | yacc ON+WHERE | Analyze | 原始谓词（ON 和 WHERE 合在一起） |
| `group_bys` | vector\<Col> | yacc | Analyze | 分组列 |
| `havings` | vector\<HavingExpr> | yacc | Analyze | 聚合后条件 |
| `has_limit` | bool | 构造函数 | Analyze | `limit_num_ >= 0` |
| `limit_num` | int | 构造函数 | Analyze | `-1` 无限制，`>=0` 截断 |
| `orders` | vector\<OrderBy> | yacc | Analyze | 多列排序 |
| `order` | OrderBy | yacc（旧路径） | Analyze | 单列排序兼容 |

构造函数注意：

```cpp
is_select_all = select_items.empty();   // SELECT * 时 select_items 为空
has_limit = limit_num_ >= 0;            // limit -1 表示无限制
has_sort = (bool)order;
```

## 九、`SemValue`（bison 语义值）逐个字段

位置：[ast.h](../../src/parser/ast.h:448)

`SemValue` 是 `YYSTYPE`（通过文件末尾 `#define YYSTYPE ast::SemValue`）。**每个 `%token`/`%type` 声明的 C++ 类型，都对应 `SemValue` 的一个字段**。flex 动作写 `yylval->sv_int`，yacc 动作读 `$1.sv_int`，读写的是同一个字段。

```cpp
struct SemValue {
    int sv_int;
    float sv_float;
    std::string sv_str;
    bool sv_bool;
    OrderByDir sv_orderby_dir;
    std::vector<std::string> sv_strs;
    TableRef sv_table_ref;
    FromClause sv_from_clause;
    std::shared_ptr<TreeNode> sv_node;

    SvCompOp sv_comp_op;
    std::shared_ptr<TypeLen> sv_type_len;
    std::shared_ptr<Field> sv_field;
    std::vector<std::shared_ptr<Field>> sv_fields;

    std::shared_ptr<Expr> sv_expr;
    std::shared_ptr<AggFunc> sv_agg_func;
    std::shared_ptr<SelectItem> sv_select_item;
    std::vector<std::shared_ptr<SelectItem>> sv_select_items;
    std::shared_ptr<HavingExpr> sv_having_expr;
    std::vector<std::shared_ptr<HavingExpr>> sv_having_exprs;

    std::shared_ptr<Value> sv_val;
    std::vector<std::shared_ptr<Value>> sv_vals;

    std::shared_ptr<Col> sv_col;
    std::vector<std::shared_ptr<Col>> sv_cols;

    std::shared_ptr<SetClause> sv_set_clause;
    std::vector<std::shared_ptr<SetClause>> sv_set_clauses;

    std::shared_ptr<BinaryExpr> sv_cond;
    std::vector<std::shared_ptr<BinaryExpr>> sv_conds;

    std::shared_ptr<OrderBy> sv_orderby;
    std::vector<std::shared_ptr<OrderBy>> sv_orderbys;
    std::vector<std::shared_ptr<Col>> sv_group_bys;

    SetKnobType sv_setKnobType;
    SetOpType sv_set_op;
};
```

| 字段 | 类型 | 常见绑定的非终结符 |
|---|---|---|
| `sv_int` | int | VALUE_INT、LIMIT 数值 |
| `sv_float` | float | VALUE_FLOAT |
| `sv_str` | string | IDENTIFIER、VALUE_STRING |
| `sv_bool` | bool | VALUE_BOOL、opt_distinct |
| `sv_comp_op` | SvCompOp | condition 的比较符 |
| `sv_table_ref` | TableRef | tableRef |
| `sv_from_clause` | FromClause | **tableList**（最重要） |
| `sv_node` | TreeNode\* | 语句节点 |
| `sv_expr` | Expr\* | col/value/aggfunc |
| `sv_select_item` / `sv_select_items` | SelectItem | selector 单项/多项 |
| `sv_cond` / `sv_conds` | BinaryExpr | condition / whereClause |
| `sv_set_op` | SetOpType | 集合算子 |

语义值字段必须和产生式 `$$` 的 C++ 类型一致，否则编译/运行错乱。

## 十、全局变量与宏

```cpp
extern std::shared_ptr<ast::TreeNode> parse_tree;  // 最近一次成功解析的 AST 根
#define YYSTYPE ast::SemValue
```

- `parse_tree`：定义在 `ast.cpp`，由 yacc 的 `start` 产生式写入。Analyze 拿它做语义分析。
- `YYSTYPE`：让 bison 生成的解析器把语义值栈的类型定为 `ast::SemValue`。

## 十一、易错点总结

1. `SelectStmt::conds` 是 **ON + WHERE 合并**后的扁平数组，没有来源标记。
2. `SemValue` 字段与 `%type` 绑定必须类型一致。
3. AST 节点用 `shared_ptr`，多个上层可以共享子表达式；语义值复制 shared_ptr 只增引用计数，不深拷贝。
4. AST 只保存语法事实，不查询表是否存在——那是 Analyze 的活。
