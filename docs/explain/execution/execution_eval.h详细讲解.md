# `execution_eval.h` 详细讲解

源码入口：[execution_eval.h](../../src/execution/execution_eval.h:1)

这个文件是 **执行期条件求值与类型安全比较工具集**（header-only，全 `inline`）。Scan/Filter/Join 的谓词求值、QlManager 的输出格式化都靠它。

## 〇、文件里所有函数一览

| 函数 | 作用 |
|---|---|
| `trim_trailing_zeros` | 去掉小数串尾部 0 和末尾 `.` |
| `format_float_output` | float 格式化为输出字符串 |
| `format_col_value` | 按列类型取字段并格式化（SELECT 打印用） |
| `compare_string_value` | 定长 CHAR 三路比较（截断空字符、去尾部空格） |
| `compare_col_value` | 同类型两列原始字节三路比较 |
| `compare_number_value` | 数值（double）三路比较 |
| `compare_col_to_value` | 列值 vs SQL 字面量比较（INT/FLOAT 提升为 double） |
| `compare_record_by_cols` | 两条记录按列列表字典序比较（Sort/去重用） |
| `eval_comp` | 三路比较结果 → 布尔（配合 CompOp） |
| `find_col` | 在 schema 中查找列（先精确再退列名） |
| `eval_condition` | 单条 Condition 求值 |
| `eval_conditions` | 全部 Condition 求值（AND 语义） |

## 一、字符串/浮点格式化工具

### 1.1 `trim_trailing_zeros`

```cpp
inline std::string trim_trailing_zeros(std::string s);
```

去掉小数串尾部多余的 `0` 和末尾的 `.`。`"1.500"` → `"1.5"`，`"1.0"` → `"1"`。

### 1.2 `format_float_output(f, agg_float_fixed)`

```cpp
inline std::string format_float_output(float f, bool agg_float_fixed);
```

- `agg_float_fixed=true`：固定 6 位小数（聚合结果等）。
- `false`：先 `%g`；若结果含 `e`/`E`（科学计数法），改用 `%.10f` 再 `trim_trailing_zeros`。

### 1.3 `format_col_value(col, rec_buf, agg_float_fixed)`

```cpp
inline std::string format_col_value(const ColMeta &col, const char *rec_buf, bool agg_float_fixed = false);
```

按列类型从记录缓冲取字段格式化：
- INT：`*(const int *)rec_buf` → to_string
- FLOAT：`format_float_output`
- STRING：读到 `\0` 或列长为止（**避免把填充字节打出**）

**字符串截断到 `\0`** 是因为定长 CHAR 会用 `\0` 填充未用部分。这是输出可读性的关键。

## 二、比较函数

### 2.1 `compare_string_value(a, b, col_len)`

```cpp
inline int compare_string_value(const char *a, const char *b, int col_len);
```

定长 CHAR 比较的三步：
1. 取 `col_len` 字节构造 string。
2. 截掉 `\0` 之后的部分（`sa.erase(null_pos_a)`）。
3. 再去掉尾部空格。

返回 `-1/0/1` 表示 `a<b / a==b / a>b`。**"截断空字符 + 去尾部空格"** 是为了让 `'abc'`、`'abc '`、`'abc\0...'` 都判为相等。

### 2.2 `compare_col_value(a, b, type, col_len)`

```cpp
inline int compare_col_value(const char *a, const char *b, ColType type, int col_len);
```

同类型两列原始字节三路比较。INT/FLOAT 直接解引用比较，STRING 走 `compare_string_value`，未知类型抛 `InternalError`。**用于列 vs 列**（等值/连接条件）。

### 2.3 `compare_number_value(a, b)` 与 `compare_col_to_value`

```cpp
inline int compare_number_value(double a, double b);
inline int compare_col_to_value(const char *lhs_data, const ColMeta &lhs_col, const Value &rhs_val);
```

`compare_col_to_value` 用于**列 vs SQL 字面量**：
- INT 列：lhs 转 double，rhs 按类型取 `float_val` 或 `int_val` 转 double，再比较。INT/FLOAT 统一提升到 double，**这是"int 列 vs 浮点字面量"能比较的原因**。
- FLOAT 列：同理。
- STRING 列：把 `rhs_val` 拷入 `lhs_col.len` 字节的缓冲区（不足补 0，超长截断），再走 `compare_string_value`。拷入定长缓冲是为了与列对齐。
- 类型不可比：抛 `IncompatibleTypeError`。

### 2.4 `compare_record_by_cols(a, b, cols)`

```cpp
inline int compare_record_by_cols(const RmRecord &a, const RmRecord &b, const std::vector<ColMeta> &cols);
```

两条记录按给定列列表做**字典序比较**，从前到后比，首个非零结果即返回。

## 三、条件求值核心

### 3.1 `eval_comp(cmp, op)`

```cpp
inline bool eval_comp(int cmp, CompOp op) {
    switch (op) {
        case OP_EQ: return cmp == 0;
        case OP_NE: return cmp != 0;
        case OP_LT: return cmp < 0;
        case OP_GT: return cmp > 0;
        case OP_LE: return cmp <= 0;
        case OP_GE: return cmp >= 0;
        default: throw InternalError("Unexpected comparison operator");
    }
}
```

把三路比较结果映射为布尔。

### 3.2 `find_col(cols, target)`

```cpp
inline const ColMeta *find_col(const std::vector<ColMeta> &cols, const TabCol &target);
```

在 schema 中查找列：先精确 `(tab_name, col_name)`，再退化为仅 `col_name`；找不到抛 `ColumnNotFoundError`。与 executor_abstract 里的 `get_col` 逻辑一致。

### 3.3 `eval_condition(rec, cond, cols)`（单条条件）

位置：[execution_eval.h](../../src/execution/execution_eval.h:245)

```cpp
inline bool eval_condition(const RmRecord &rec, const Condition &cond, const std::vector<ColMeta> &cols) {
    const ColMeta *lhs_col = find_col(cols, cond.lhs_col);
    const char *lhs_data = rec.data + lhs_col->offset;   // 左列值的位置

    int cmp;
    if (cond.is_rhs_val) {
        cmp = compare_col_to_value(lhs_data, *lhs_col, cond.rhs_val);   // 列 vs 常量
    } else {
        const ColMeta *rhs_col = find_col(cols, cond.rhs_col);
        cmp = compare_col_value(lhs_data, rec.data + rhs_col->offset, lhs_col->type, lhs_col->len);  // 列 vs 列
    }
    return eval_comp(cmp, cond.op);
}
```

`is_rhs_val` 决定走哪条比较路径。注意 `cols` 参数是**求值用的 schema**——对于 Join，这是"左+右"的 cols_；条件里同时读左右列靠的就是它。

### 3.4 `eval_conditions(rec, conds, cols)`（AND 语义）

位置：[execution_eval.h](../../src/execution/execution_eval.h:262)

```cpp
inline bool eval_conditions(const RmRecord &rec, const std::vector<Condition> &conds, const std::vector<ColMeta> &cols) {
    for (const auto &cond : conds) {
        if (!eval_condition(rec, cond, cols)) return false;
    }
    return true;
}
```

**全部条件为真才返回 true（AND 语义）**。空条件数组返回 true——这就是 CROSS JOIN 依赖的行为。
