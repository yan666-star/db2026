# `common.h` 详细讲解

源码入口：[common.h](../../src/common/common.h:13)

这个文件是 **执行层的公共数据结构**：`TabCol`、`CompOp`、`Value`、`Condition`、`SelectItem` 等。这些结构贯穿 Analyze → Planner → Executor 全程，**是条件、投影、聚合的"标准件"**。做任何功能题前，先确认公共结构有没有对应字段。

## 一、`TabCol`（绑定后的列引用）

位置：[common.h](../../src/common/common.h:22)

```cpp
struct TabCol {
    std::string tab_name;
    std::string col_name;

    friend bool operator<(const TabCol &x, const TabCol &y) {
        return std::make_pair(x.tab_name, x.col_name) < std::make_pair(y.tab_name, y.col_name);
    }
};
```

| 字段 | 含义 |
|---|---|
| `tab_name` | 表名（Analyze 绑定后一定是真表名） |
| `col_name` | 列名 |

与 AST 层 `Col` 的区别：`Col` 是用户**写出来的**（`tab_name` 可能空）；`TabCol` 是 **Analyze 绑定后的**（`tab_name` 必填）。`operator<` 提供字典序，用于去重、排序、作 map 键。

## 二、`CompOp`（比较运算符）

```cpp
enum CompOp { OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE };
```

**执行层的比较符**。由 AST 层 `SvCompOp` 通过 `Analyze::convert_sv_comp_op` 一对一转换而来。新增比较符要两边同步加。

## 三、`AggType` 与 `AggExpr`

```cpp
enum AggType { AGG_NONE = 0, AGG_COUNT, AGG_MAX, AGG_MIN, AGG_SUM, AGG_AVG };

struct AggExpr {
    AggType type = AGG_NONE;
    TabCol col;
    bool is_star = false;  // only valid for COUNT(*)
};
```

`AggType` 是执行层聚合类型，`AggExpr` 是"一条聚合表达式"：对哪个列、什么函数、是否 `COUNT(*)`。

## 四、`SelectItem` / `OrderByItem`

```cpp
struct SelectItem {
    bool is_agg = false;   // 是否为聚合项
    TabCol col;            // 普通列（非聚合时）
    AggExpr agg;           // 聚合表达式（聚合时）
    std::string alias;
};

struct OrderByItem {
    bool is_agg = false;
    TabCol col;
    AggExpr agg;
    bool is_desc = false;
};
```

`SelectItem` 是 Query 里的投影项。`is_agg` 区分普通列和聚合：普通用 `col`，聚合用 `agg`。`OrderByItem` 同构，多一个 `is_desc` 升降序。

## 五、`Value`（字面量值，条件右侧和 INSERT 值用）逐个字段

位置：[common.h](../../src/common/common.h:55)

```cpp
struct Value {
    bool from_float_literal = false;   // 是否来自 float 字面量（区分提升来的）
    ColType type;                      // value 类型
    union {
        int int_val;                   // int value
        float float_val;               // float value
    };
    std::string str_val;               // string value

    std::shared_ptr<RmRecord> raw;     // raw record buffer（物理字节）
    ...
};
```

逐个解释：

| 字段 | 类型 | 含义 |
|---|---|---|
| `from_float_literal` | bool | 是否**真正**写了 float 字面量。`5.0` 是 true；`5` 提升成 float 后保持 false。用于输出/精度判断 |
| `type` | ColType | TYPE_INT / TYPE_FLOAT / TYPE_STRING |
| `int_val` | int | union 成员，type==TYPE_INT 时有效 |
| `float_val` | float | union 成员，type==TYPE_FLOAT 时有效 |
| `str_val` | string | type==TYPE_STRING 时有效 |
| `raw` | shared_ptr\<RmRecord> | 按列类型和长度生成的物理字节，`init_raw` 后有效 |

### 5.1 set 方法

```cpp
void set_int(int);     // type=TYPE_INT, int_val=...
void set_float(float); // type=TYPE_FLOAT, float_val=...
void set_str(string);  // type=TYPE_STRING, str_val=...
```

### 5.2 `init_raw(len)`（物理关键）

```cpp
void init_raw(int len) {
    assert(raw == nullptr);
    raw = std::make_shared<RmRecord>(len);
    if (type == TYPE_INT) {
        *(int *)(raw->data) = int_val;
    } else if (type == TYPE_FLOAT) {
        *(float *)(raw->data) = float_val;
    } else if (type == TYPE_STRING) {
        if (len < (int)str_val.size()) throw StringOverflowError();
        memset(raw->data, 0, len);                    // 定长填充 0
        memcpy(raw->data, str_val.c_str(), str_val.size());
    }
}
```

把值写入定长字节缓冲，**让字面量能和记录里的列字节直接比较**。要点：
- INT/FLOAT 直接按 4 字节写入。
- STRING 先 `memset 0` 再拷贝（定长 CHAR 用 `\0` 填充剩余空间）。
- 字符串超长抛 `StringOverflowError`。
- **`init_raw` 只能在还没初始化过时调用一次**（`assert(raw == nullptr)`）。execution_eval 的 `compare_col_to_value` 在 STRING 分支会用 `rhs_val.raw` 当定长缓冲源。

## 六、`HavingCond` / `Condition`（条件结构）

### 6.1 `HavingCond`

```cpp
struct HavingCond {
    AggExpr lhs;       // 左端聚合表达式
    CompOp op = OP_EQ;
    Value rhs_val;     // 右端字面量
};
```

HAVING 专用：左端必须是聚合，右端是字面量。

### 6.2 `Condition`（最重要）

位置：[common.h](../../src/common/common.h:106)

```cpp
struct Condition {
    TabCol lhs_col;   // 左列
    CompOp op;        // 比较符
    bool is_rhs_val;  // true：右端是值；false：右端是列
    TabCol rhs_col;   // 右列（is_rhs_val==false 时有效）
    Value rhs_val;    // 右值（is_rhs_val==true 时有效）
};
```

这是 **WHERE/ON 条件在执行的统一表示**。两种情况：

```text
salary >= 7000:    lhs_col={,salary}, op=OP_GE, is_rhs_val=true,  rhs_val.int_val=7000
a.id = b.a_id:     lhs_col={a,id},    op=OP_EQ, is_rhs_val=false, rhs_col={b,a_id}
```

`is_rhs_val` 决定 execution_eval 里走 `compare_col_to_value` 还是 `compare_col_value`。Analyze 的 `get_clause` 负责填它，`check_clause` 负责绑定列、cast 常量、init_raw。

### 6.3 `SetClause`

```cpp
struct SetClause {
    TabCol lhs;
    bool is_arithmetic = false;
    TabCol rhs_col;
    char arithmetic_op = '\0';
    Value rhs;
};
```

UPDATE SET：`is_arithmetic` 为 true 表示 `a = b <op> val` 的列间算术。

## 七、资格赛扩展的落点对照

| 功能 | 改 common.h 哪里 |
|---|---|
| 新比较符（LIKE） | `CompOp` 加枚举 |
| 新聚合函数 | `AggType` 加枚举 |
| 新类型（DATE） | `Value` 加 union 成员 + `set_*` + `init_raw` 分支；`ColType`（在 defs.h）加枚举 |
| 新 SELECT 标志 | 不加在 Value，加在 `Query`（analyze.h）和对应 Plan |
| SQL NULL | 加 NULL 位图到记录/列定义，`Condition` 可能需要 `is_null` 表示 |

## 八、易错点总结

1. `Value` 的 `int_val`/`float_val` 是 **union 成员**，只按 `type` 读一个，混读是 UB。
2. `init_raw` 前必须 `cast_val_to_col` 对齐类型；`raw` 为空时 execution_eval 的 STRING 比较会走不了。
3. `Condition` 里 `is_rhs_val` 为 false 时，`rhs_col` 必须已绑定；为 true 时 `rhs_val` 必须已 `init_raw`。
4. `TabCol` 的 `operator<` 让它可以作 `std::set`/`std::map` 的键（去重用）。
5. 新增比较符必须 `SvCompOp / CompOp / convert_sv_comp_op / execution_eval` 四处同步。
6. `Condition` 是扁平结构，只表达 AND 合取；OR 需要更高层结构（条件组/表达式树）。
