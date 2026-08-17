# `analyze.cpp` 详细讲解

源码入口：[analyze.cpp](../../src/analyze/analyze.cpp:12)

这个文件是 Analyze 层的**实现**。阅读顺序：`do_analyze` → `analyze_select` → `check_column` / `get_clause` + `check_clause` → 派生表/UNION。

## 〇、文件里两个静态辅助函数

### 0.1 `cast_val_to_col(Value &val, ColType col_type)`

位置：[analyze.cpp](../../src/analyze/analyze.cpp:21)

```cpp
static void cast_val_to_col(Value &val, ColType col_type) {
    if (val.type == col_type) return;
    if (col_type == TYPE_FLOAT && val.type == TYPE_INT) {
        bool from_float_literal = val.from_float_literal;
        val.set_float(static_cast<float>(val.int_val));
        val.from_float_literal = from_float_literal;
        return;
    }
    throw IncompatibleTypeError(coltype2str(col_type), coltype2str(val.type));
}
```

把字面量 `Value` 对齐到列的类型。目前只支持 **INT 字面量 → FLOAT 列**（`salary` 是 float，`where salary > 5` 时把 `5` 提升成 `5.0`）。注意它**保留 `from_float_literal` 标志**再重新 set_float，因为 set_float 会覆盖该标志（float 字面量标志是 `true`，int 提升来的应保持 `false`）。

### 0.2 `find_col_meta(all_cols, target)` 与 `assign_col_offsets(cols)`

```cpp
static std::vector<ColMeta>::const_iterator find_col_meta(const std::vector<ColMeta> &all_cols, const TabCol &target);
static void assign_col_offsets(std::vector<ColMeta> &cols);
```

`find_col_meta` 按 `(表名,列名)` 精确查找 ColMeta，找不到抛 `ColumnNotFoundError`。`assign_col_offsets` 把一连串列按顺序重新分配 offset（从 0 开始累加 len），派生表/UNION 输出 schema 用。

### 0.3 `resolve_order_by_alias`

`ORDER BY 别名` 时，在 select_items 里找同名 alias，把 `OrderByItem` 绑定到对应列/聚合。

## 一、UNION/派生表相关函数

### 1.1 `union_compatible` 与 `promote_union_col`

```cpp
bool Analyze::union_compatible(ColType a, ColType b);       // INT/FLOAT 互相兼容，STRING 只能配 STRING
ColMeta Analyze::promote_union_col(const ColMeta &a, const ColMeta &b);  // 升成 FLOAT 或取更长的 STRING
```

`union_compatible` 判断两列类型能否合并；`promote_union_col` 产出合并后的列：INT+INT→INT，INT+FLOAT→FLOAT，STRING→取更长 len。

### 1.2 `get_branch_output_cols(query)`

推断一个已分析 Query 对外输出的 schema：
- `SELECT *` → 直接返回全部 all_cols。
- 聚合路径 → 按 select_items 逐项推类型（COUNT→INT，AVG→FLOAT，SUM→列类型，MAX/MIN→列类型）。
- 普通投影 → 拷贝基表 ColMeta，AS 别名时改 `col.name`。

### 1.3 `analyze_union_branches(branches, set_ops, alias)`

集合查询分析：
1. 校验分支数 ≥2、分支不带 GROUP/HAVING、各分支列数相等、类型 compatible。
2. 每个分支递归 `analyze_select(branch, false)`。
3. 逐列 `promote_union_col`，统一列名取第一个分支的，`tab_name=alias`，重排 offset。

### 1.4 `analyze_derived_subquery(subquery, alias)` 与 `analyze_top_level_union(x)`

- 派生表：`from (select...) alias` → 递归 `analyze_select(subquery, true)`，取 `get_branch_output_cols`，把所有列 `tab_name` 改成 alias。
- 顶层 UNION：包装成一个**假表** `__union_output__`，放进 `derived_tables`，再 `tables=[__union_output__]`，让 Planner 走单表路径。这是关键技巧：**顶层 UNION 不需要新增 Plan 分支，伪装成派生表即可**。

## 二、`analyze_select`（SELECT 主流程）逐步讲解

位置：[analyze.cpp](../../src/analyze/analyze.cpp:333)

整体分 8 个阶段：

```text
1. UNION 早退
2. 创建 Query，复制顶层标志（is_explain_analyze / is_select_all）
3. 处理 FROM：真表、别名、派生表
4. 汇总 all_cols
5. 展开 SELECT * 或转换 select_items
6. GROUP BY / HAVING / ORDER BY / LIMIT
7. 聚合语义检查（GROUP 与 SELECT 项一致性）
8. get_clause + check_clause 转条件
```

### 2.1 UNION 早退

```cpp
if (x->is_union) {
    if (!allow_derived) throw RMDBError("failure");
    return analyze_top_level_union(x);
}
```

### 2.2 创建 Query 并复制顶层标志

```cpp
std::shared_ptr<Query> query = std::make_shared<Query>();
query->is_explain_analyze = x->is_explain_analyze;
query->is_select_all = x->is_select_all;
```

### 2.3 处理 FROM（表/别名/派生表）

```cpp
for (auto &ref : x->tabs) {
    if (ref.is_subquery) {              // 派生表
        query->derived_tables[ref.alias] = analyze_derived_subquery(ref.subquery, ref.alias);
        query->tables.push_back(ref.alias);
        query->alias_to_table[ref.alias] = ref.alias;
        query->alias_to_table[ref.tab_name] = ref.alias;
    } else {                            // 真表
        query->tables.push_back(ref.tab_name);
        std::string visible_name = ref.alias.empty() ? ref.tab_name : ref.alias;
        query->alias_to_table[visible_name] = ref.tab_name;
        query->alias_to_table[ref.tab_name] = ref.tab_name;
        query->table_to_alias[ref.tab_name] = visible_name;
    }
}
```

注意别名映射的写法：
- `student s` → `alias_to_table["s"]="student"`、`alias_to_table["student"]="student"`、`table_to_alias["student"]="s"`。
- 派生表 `from (..) t` → `alias_to_table["t"]="t"`（别名即自身）。

随后校验表存在：

```cpp
if (!sm_manager_->db_.is_table(tab_name)) throw TableNotFoundError(tab_name);
```

### 2.4 汇总 all_cols

```cpp
std::vector<ColMeta> all_cols;
get_query_cols(query, all_cols);
```

`get_query_cols` 遍历 `query->tables`：派生表用 `derived_tables[别名].cols`，真表用 `db_.get_table(表名).cols`。**这个 `all_cols` 是后面所有列绑定的数据源**。

### 2.5 展开 SELECT 项

**分支 A：`select_items.empty()`（即 `SELECT *`）**

```cpp
for (auto &col : all_cols) {
    query->cols.push_back({col.tab_name, col.col_name});
    // 并构造普通 SelectItem
}
```

**`SELECT *` 在这里被展开成所有表的全部列**。

**分支 B：显式列**

```cpp
if (auto sv_col = std::dynamic_pointer_cast<ast::Col>(sv_item->expr)) {
    item.is_agg = false;
    item.col = check_column(all_cols, {sv_col->tab_name, sv_col->col_name}, query->alias_to_table);
    query->cols.push_back(item.col);
} else if (auto sv_agg = std::dynamic_pointer_cast<ast::AggFunc>(sv_item->expr)) {
    item.is_agg = true;
    item.agg.type = convert_agg_type(sv_agg->func_type);
    item.agg.is_star = sv_agg->is_star;
    // COUNT(*) 无 col；否则绑定聚合列并校验 SUM/AVG 只用于数值
    query->has_agg = true;
} else {
    throw RMDBError("failure");
}
```

**ANSI/ANTI 的"只允许左表列"检查就放在 `query->cols` 绑定完成之后**——此时 `col.tab_name` 已是真表名，判断才可靠。

### 2.6 GROUP BY / HAVING / ORDER BY / LIMIT

```cpp
query->limit_num = x->limit_num;
// group_bys: 每个 sv_col 调 check_column
// havings: 转换聚合、比较符、右值，cast 后 init_raw，置 has_agg=true
// order_bys: 优先解析别名(resolve_order_by_alias)，否则 check_column
```

HAVING 的处理要点：`h.rhs_val` 的类型要按聚合结果的类型决定（COUNT→INT、AVG→FLOAT、SUM→列类型），`cast_val_to_col` 后 `init_raw(lhs_len)`。

### 2.7 聚合语义检查

```cpp
if (!query->group_bys.empty()) {
    // 每个非聚合 SELECT 项必须出现在 GROUP BY 里，否则 failure
} else if (query->has_agg) {
    // 有聚合无 GROUP BY 时，所有 SELECT 项必须都是聚合，否则 failure
}
```

这是 SQL 的"SELECT 非聚合列必须属于 GROUP BY"规则。

### 2.8 转条件

```cpp
get_clause(x->conds, query->conds);
check_clause(query, query->conds, query->alias_to_table);
```

## 三、`do_analyze`（总入口）

位置：[analyze.cpp](../../src/analyze/analyze.cpp:556)

```cpp
std::shared_ptr<Query> Analyze::do_analyze(std::shared_ptr<ast::TreeNode> parse) {
    std::shared_ptr<Query> query = std::make_shared<Query>();
    if (auto x = std::dynamic_pointer_cast<ast::SelectStmt>(parse)) {
        query = analyze_select(x, true);
    } else if (auto x = std::dynamic_pointer_cast<ast::UpdateStmt>(parse)) {
        // 填 set_clauses（含列间算术校验）、转 conds
    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(parse)) {
        query->tables = {x->tab_name};  // 转 conds
    } else if (auto x = std::dynamic_pointer_cast<ast::InsertStmt>(parse)) {
        for (auto &sv_val : x->vals) query->values.push_back(convert_sv_value(sv_val));
    } else {
        // do nothing
    }
    query->parse = std::move(parse);
    return query;
}
```

UPDATE/DELETE 的 `conds` 处理有个细节：它们用单表别名映射 `alias_to_table[x->tab_name]=x->tab_name` 后调 `check_clause`，因为没有 FROM 多表。INSERT 只转 `values`，条件为空。

## 四、`check_column`（列消解）逐步语义

位置：[analyze.cpp](../../src/analyze/analyze.cpp:623)

```cpp
TabCol Analyze::check_column(const std::vector<ColMeta> &all_cols, TabCol target,
                             const std::map<std::string, std::string> &alias_to_table) {
```

1. `target.tab_name` 非空且是别名 → `alias_to_table` 换成真表名。
2. `tab_name` 为空 → 按 `col_name` 在所有可见列找唯一匹配：多个抛 `AmbiguousColumnError`，无则抛 `ColumnNotFoundError`。
3. `tab_name` 非空 → 精确匹配 `(表名,列名)`，无则抛 `ColumnNotFoundError`。

返回值是绑定后的 TabCol。

## 五、`get_clause`（BinaryExpr → Condition 结构转换）

位置：[analyze.cpp](../../src/analyze/analyze.cpp:671)

```cpp
void Analyze::get_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds, std::vector<Condition> &conds) {
    conds.clear();
    for (auto &expr : sv_conds) {
        Condition cond;
        auto lhs_col = std::dynamic_pointer_cast<ast::Col>(expr->lhs);
        if (lhs_col == nullptr) throw RMDBError("failure");      // 左端必须是列
        cond.lhs_col = {lhs_col->tab_name, lhs_col->col_name};
        cond.op = convert_sv_comp_op(expr->op);
        if (auto rhs_val = std::dynamic_pointer_cast<ast::Value>(expr->rhs)) {
            cond.is_rhs_val = true;
            cond.rhs_val = convert_sv_value(rhs_val);
        } else if (auto rhs_col = std::dynamic_pointer_cast<ast::Col>(expr->rhs)) {
            cond.is_rhs_val = false;
            cond.rhs_col = {rhs_col->tab_name, rhs_col->col_name};
        }
        conds.push_back(cond);
    }
}
```

要点：
- **左端必须是 `Col`**，否则抛 failure（当前不支持 `5 < x` 这种左值为常量的写法）。
- 右端 `dynamic_pointer_cast<ast::Value>` 成功 → `is_rhs_val=true`；是 `ast::Col` → `is_rhs_val=false`。**两种都可能没匹配**（比如右端是聚合函数），此时 `is_rhs_val` 保持默认 false 但 `rhs_col` 为空——这种非法条件会留到 check_clause 报错。

## 六、`check_clause`（条件语义加工）逐个变量

位置：[analyze.cpp](../../src/analyze/analyze.cpp:693)

```cpp
void Analyze::check_clause(const std::shared_ptr<Query> &query, std::vector<Condition> &conds,
                           const std::map<std::string, std::string> &alias_to_table) {
    std::vector<ColMeta> all_cols;
    get_query_cols(query, all_cols);
    for (auto &cond : conds) {
        cond.lhs_col = check_column(all_cols, cond.lhs_col, alias_to_table);   // 绑定左列
        if (!cond.is_rhs_val) {
            cond.rhs_col = check_column(all_cols, cond.rhs_col, alias_to_table); // 绑定右列
        }
        ColType lhs_type, rhs_type;
        auto lhs_meta = find_col_meta(all_cols, cond.lhs_col);   // 拿左列 ColMeta
        lhs_type = lhs_meta->type;
        if (cond.is_rhs_val) {
            cast_val_to_col(cond.rhs_val, lhs_type);              // 常量对齐列类型
            cond.rhs_val.init_raw(lhs_meta->len);                 // 生成物理字节
            rhs_type = cond.rhs_val.type;
        } else {
            auto rhs_meta = find_col_meta(all_cols, cond.rhs_col);
            rhs_type = rhs_meta->type;
        }
        if (lhs_type != rhs_type) throw IncompatibleTypeError(...);  // 类型一致
    }
}
```

**`init_raw(lhs_meta->len)` 是物理关键**：`Value::init_raw` 分配 `len` 字节的 `RmRecord` 并按类型写入值（int/float 直接写，string 用 `\0` 填充到定长）。执行器比较时读的就是这段 raw。

## 七、类型转换函数

```cpp
Value Analyze::convert_sv_value(const std::shared_ptr<ast::Value> &sv_val);   // IntLit→set_int 等
CompOp Analyze::convert_sv_comp_op(ast::SvCompOp op);                          // map 一对一
AggType Analyze::convert_agg_type(ast::AggFuncType func_type);                 // switch 一对一
```

## 八、易错点总结

1. `SELECT *` 在第 2.5 阶段展开，之后所有限制检查都要考虑它（ANTI 的 `SELECT *` 会被列为限制条件）。
2. `conds` 处理顺序是 `get_clause` **然后** `check_clause`，顺序不能反。
3. 常量的 `init_raw` 必须按列 len 做，否则定长 STRING 的填充字节、INT 的字节序都会错。
4. `query->parse = std::move(parse)` 在函数末尾；move 之后不能再依赖原 parse 对象。
5. 聚合语义检查（GROUP 一致性）在转条件**之前**执行。
