# analyze 模块讲解

## 0. 为什么 parser 后还要 Analyze

Parser 只知道用户写了 `id`，不知道 id 是否存在、属于哪张表。Analyze 查询 SmManager 的元数据，把模糊语法变成确定信息。

例子：

```sql
select id from student;
```

Parser 得到：

```text
Col{tab_name="", col_name="id"}
```

Analyze 查 student 的 TabMeta 后得到：

```text
TabCol{tab_name="student", col_name="id"}
```

这叫“列绑定”。Planner 和 Executor 后面都使用绑定后的表名。

### 0.1 `Analyze::do_analyze(root)` 每个参数

```cpp
std::shared_ptr<Query> do_analyze(
    std::shared_ptr<ast::TreeNode> root);
```

| 参数/返回值 | 含义 |
|---|---|
| `root` | parser 生成的 AST 根；可能实际是 SelectStmt、InsertStmt 等子类 |
| 返回 `shared_ptr<Query>` | 完成语义分析的查询对象，交给 Planner |

为什么参数写 TreeNode：所有 SQL AST 都继承它，一个入口可接受所有语句。函数内部用 `dynamic_pointer_cast<SelectStmt>(root)` 判断真实类型。

### 0.2 `analyze_select(x, allow_derived)`

```cpp
std::shared_ptr<Query> analyze_select(
    std::shared_ptr<ast::SelectStmt> x,
    bool allow_derived);
```

| 参数 | 含义 | 举例 |
|---|---|---|
| `x` | 当前 SELECT AST | tables、select_items、conds 都从这里读 |
| `allow_derived` | 是否允许当前层处理派生表/集合查询 | 顶层 true，某些分支递归时 false 防止不支持嵌套 |
| 返回值 | 新建并填好的 Query | Planner 的唯一主要输入 |

`x` 是 shared_ptr，因为 Query.parse 还会保存 AST；函数结束后 AST 不能被释放。

### 0.3 `check_column(all_cols,target,alias_to_table)`

```cpp
TabCol check_column(
    const std::vector<ColMeta> &all_cols,
    TabCol target,
    const std::map<std::string,std::string> &alias_to_table);
```

| 参数 | 含义 |
|---|---|
| `all_cols` | FROM 中所有可见列的完整元数据 |
| `target` | 用户写的列，可能无表名或使用别名 |
| `alias_to_table` | 如 `s -> student` |
| 返回值 | 绑定成真实表名的 TabCol |

为什么 all_cols 用 const 引用：数据可能很多，不复制，函数也不应修改目录信息。为什么 target 按值：函数可以把其中的别名替换成真表名，不影响调用者原对象，最后返回新值。

例子：`student s` 后写 `s.id`，target.tab_name 先从 s 变 student，再在 all_cols 找 `(student,id)`。

### 0.4 `get_clause(sv_conds,conds)`

```cpp
void get_clause(
  const vector<shared_ptr<ast::BinaryExpr>> &sv_conds,
  vector<Condition> &conds);
```

`sv_conds` 是 parser AST 输入；`conds` 是输出参数，函数填入 common::Condition。为什么返回 void：结果通过引用参数写回。

对 `salary >= 7000`：

```text
lhs_col={"","salary"}
op=OP_GE
is_rhs_val=true
rhs_val.int_val=7000
```

### 0.5 `check_clause(query,conds,alias_to_table)`

它继续加工刚才的 Condition：绑定 lhs/rhs 列；查 lhs ColMeta.type/len；把 rhs 常量转成相容类型；调用 init_raw(len)。

为什么传 query：派生表的列不一定在 SmManager.db_ 中，必须通过 query->derived_tables 获取可见 schema。

## 1. 目录职责

Analyze 位于 parser 和 planner 之间，把“用户写法”转换成“已绑定、已检查的 Query”。

```text
AST Col("id")
 -> 查所有候选表
 -> TabCol("student","id")
 -> 找 ColMeta
 -> 转换常量类型并生成 raw
 -> Planner 可直接使用
```

主要文件：

- [analyze.h](../../src/analyze/analyze.h:42)：Query、DerivedTableInfo、接口。
- [analyze.cpp](../../src/analyze/analyze.cpp:301)：实现。

## 2. Query 的归属

Query 由 Analyze 创建，Planner 主要读取。`parse` 保留 AST 引用，便于 DDL/DML 计划识别。

| 变量 | 内容 | 创建/转换 | Planner 用途 |
|---|---|---|---|
| `parse` | 原 AST 根 | `do_analyze` | 判断语句类型 |
| `tables` | 真表名或派生表别名 | analyze_select | 建扫描节点 |
| `conds` | `Condition` | get_clause/check_clause | 下推、连接条件 |
| `cols` | `TabCol` | SELECT 列绑定 | Projection |
| `select_items` | 普通/聚合选择项 | analyze_select | AggregatePlan |
| `group_bys` | 已绑定列 | analyze_select | 分组 |
| `havings` | 已转换条件 | analyze_select | 聚合后过滤 |
| `order_bys` | 列/聚合排序项 | analyze_select | Sort/Aggregate |
| `limit_num` | -1 或限制数 | AST 复制 | 输出截断 |
| `has_agg` | 是否包含聚合 | 遍历 SELECT 项 | 选择聚合路径 |
| `values` | INSERT Value | do_analyze | DMLPlan |
| `set_clauses` | UPDATE SetClause | do_analyze | UpdateExecutor |
| `alias_to_table` | 别名->真表 | FROM 分析 | 列绑定 |
| `table_to_alias` | 真表->别名 | FROM 分析 | 输出/解析 |
| `derived_tables` | 子查询信息 | 派生表分析 | 子计划 |

## 3. Condition 形成过程

AST 的 `BinaryExpr` 仍是指针多态结构。`get_clause()` 转成 [Condition](../../src/common/common.h:106)：

```text
lhs_col + op + (rhs_val 或 rhs_col)
```

| 字段 | 作用 |
|---|---|
| `lhs_col` | 左列，check_clause 后应绑定表名 |
| `op` | OP_EQ/NE/LT/GT/LE/GE |
| `is_rhs_val` | true 表示列与常量比较，false 表示列与列比较 |
| `rhs_col` | 连接/列比较右侧 |
| `rhs_val` | 常量右侧 |

`check_clause()` 再做列存在性、歧义、类型转换，并调用 `Value::init_raw(col.len)` 生成与物理记录布局一致的字节。

## 4. 关键函数

| 函数 | 输入 | 输出/副作用 |
|---|---|---|
| `do_analyze` | AST TreeNode | Query |
| `analyze_select` | SelectStmt | 完整 Query |
| `check_column` | all_cols+TabCol+别名表 | 唯一绑定列 |
| `get_query_cols` | Query | all_cols |
| `get_clause` | BinaryExpr 数组 | Condition 数组 |
| `check_clause` | Query+Conditions | 就地绑定和类型化 |
| `convert_sv_value` | AST Value | common::Value |
| `convert_sv_comp_op` | SvCompOp | CompOp |
| `convert_agg_type` | AST AggFuncType | AggType |

## 5. DerivedTableInfo

| 变量 | 作用 |
|---|---|
| `is_union_table` | 是否由集合查询构成 |
| `cols` | 派生表对外输出 schema |
| `branch_queries` | 每个 SELECT 分支分析结果 |
| `set_ops` | 分支间 UNION/INTERSECT/EXCEPT 类型 |

派生表在 Query.tables 中以别名出现，Planner 查 `derived_tables` 决定建基础 Scan 还是子查询计划。

## 6. 变量生命周期

| 变量 | 归属 | 生命周期/所有权 |
|---|---|---|
| `x` | analyze_select 参数 | shared_ptr AST，Query 仍通过 parse 引用 |
| `query` | Analyze 创建 | shared_ptr 交给 Planner |
| `all_cols` | 函数局部 | 当前语义分析，保存可见列副本 |
| `item` | 循环局部 | 转换完成 push 到 query->select_items |
| `cond` | get_clause 局部 | push 后由 query->conds 拥有值副本 |
| `lhs_meta` | iterator | 指向 all_cols，不能越过 all_cols 生命周期 |

## 7. 实现注意

1. 列绑定应发生在 Analyze，不把 ColumnNotFound 留到执行器。
2. `id` 在多表都有时应判歧义；`t.id` 用别名映射到真表。
3. 常量写进记录前需要正确 `raw` 长度。
4. INT 到 FLOAT 可做允许的提升，反向转换不能随意截断。
