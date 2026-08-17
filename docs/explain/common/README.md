# common 模块讲解

## 0. common 是各层共同使用的“统一语言”

Parser 有自己的 AST 类型，磁盘有字节记录。如果 Analyze、Planner、Executor 各自定义一套条件，它们无法直接传递。common 中的 `Condition/Value/TabCol` 就是跨层统一格式。

### 0.1 `TabCol` 举例

```cpp
TabCol col{"student", "score"};
```

它只说明“student.score”，不说明类型和偏移。真正读取记录时还要用 System 的 ColMeta。

### 0.2 `Value::init_raw(len)` 为什么必须调用

Value 有两份表现：

```text
int_val=80：方便 C++ 逻辑计算
raw=四个二进制字节：方便 memcpy 到 RmRecord
```

例如 InsertExecutor 不是读取 int_val 再按类型写，而可能统一：

```cpp
memcpy(rec.data + col.offset, value.raw->data, col.len);
```

因此 Analyze 必须先 `value.init_raw(col.len)`。

`len` 参数来自 ColMeta.len：INT 应是 4；CHAR(20) 是20。字符串 raw 会把有效字符复制进去，其余补零。

### 0.3 `Condition` 用具体例子拆解

SQL：

```sql
student.score >= 80
```

对象：

```text
lhs_col={student,score}
op=OP_GE
is_rhs_val=true
rhs_val={type=INT,int_val=80,raw=...}
```

SQL：

```sql
student.class_id = class.id
```

对象：

```text
lhs_col={student,class_id}
op=OP_EQ
is_rhs_val=false
rhs_col={class,id}
```

`is_rhs_val` 是分支开关。若它是 false，却错误读取 rhs_val，就会得到未初始化值。

### 0.4 为什么 Condition 按值复制

Condition 只含小型字段和 Value 智能指针，可从 Query 复制到 Plan，再复制到 Executor。执行器得到自己的条件数组，不依赖 Analyze 局部变量。

## 1. 目录职责

common 保存 parser 之后、多个模块共同使用的数据结构。这里的改动影响 Analyze、Planner、Executor、System，适合放“跨层协议”，不放具体执行算法。

| 文件 | 作用 |
|---|---|
| [common.h](../../src/common/common.h:22) | 条件、值、列引用、聚合、SET 子句 |
| [context.h](../../src/common/context.h:22) | 当前执行上下文 |
| [config.h](../../src/common/config.h:45) | 全局 ID 类型、容量常量 |
| [exception.h](../../src/common/exception.h:1) | 公共异常支持 |

## 2. TabCol 与 ColMeta 的区别

`TabCol` 只标识逻辑列：

```cpp
{tab_name, col_name}
```

`ColMeta` 位于 system，描述物理列：

```text
表名、列名、类型、长度、记录内 offset
```

Planner/Condition 常保存 TabCol；Executor 的 `cols_` 保存 ColMeta，借助它从 RmRecord 的正确字节位置读值。

## 3. Value

| 字段 | 归属/作用 |
|---|---|
| `type` | 当前值类型 |
| `int_val` | INT 逻辑值 |
| `float_val` | FLOAT 逻辑值 |
| `str_val` | STRING 逻辑值 |
| `bool_val` | BOOL 逻辑值（若分支启用） |
| `raw` | 按列长度编码的 RmRecord 字节 |

`set_int/set_float/set_str` 设置逻辑值；`init_raw(len)` 根据列物理长度产生 raw。字符串列需要补零并检查长度；INT/FLOAT 长度必须与实际类型一致。

## 4. Condition

```cpp
struct Condition {
    TabCol lhs_col;
    CompOp op;
    bool is_rhs_val;
    TabCol rhs_col;
    Value rhs_val;
};
```

读取规则：

```text
is_rhs_val=true  -> 使用 rhs_val，忽略 rhs_col
is_rhs_val=false -> 使用 rhs_col，忽略 rhs_val
```

它被 Planner 用于谓词下推、索引选择、Join 条件划分，被 Executor 用于真实求值。

## 5. 聚合与排序结构

| 类型 | 变量 | 作用 |
|---|---|---|
| `AggExpr` | `type/col/is_star` | COUNT(*) 或列聚合 |
| `SelectItem` | `is_agg/col/agg/alias` | SELECT 输出项 |
| `HavingCond` | `lhs/op/rhs_val` | 聚合后过滤 |
| `OrderByItem` | 普通列或聚合项、方向 | 排序规则 |

## 6. SetClause

| 字段 | 作用 |
|---|---|
| `lhs` | 被更新列 |
| `is_rhs_val` | 右侧是常量还是列 |
| `rhs_col` | 列赋值/算术来源列 |
| `rhs` | 常量值 |
| `is_arithmetic` | 是否为加减乘除更新 |
| `arithmetic_op` | 具体算术操作 |

UpdateExecutor 根据 lhs 的 ColMeta.offset 覆写记录。

## 7. Context

| 字段 | 所属对象 | 作用 | 所有权 |
|---|---|---|---|
| `lock_mgr_` | LockManager | 锁操作 | 借用 |
| `log_mgr_` | LogManager | WAL | 借用 |
| `txn_` | Transaction | 当前事务 | 借用，TransactionManager 管理 |
| `data_send_` | 客户端缓冲 | 输出数据 | 借用 |
| `offset_` | 响应偏移 | 当前写入位置 | 借用 |
| `ellipsis_` | Context | 输出是否截断 | 自有值 |
| `txn_mgr_` | TransactionManager | MVCC/事务接口 | 借用 |
| `session_isolation_` | 会话变量 | 修改/读取隔离级别 | 借用 |

Context 贯穿 Portal、Executor、SmManager。不要在 Executor 析构时释放这些指针。

## 8. config 类型

| 类型 | 含义 |
|---|---|
| `frame_id_t` | 页在内存缓冲池中的帧号 |
| `page_id_t` | 页在磁盘文件中的页号 |
| `txn_id_t` | 事务 ID |
| `lsn_t` | 日志序列号 |
| `slot_offset_t` | 页内槽位置 |
| `timestamp_t` | MVCC 时间戳 |

不要混淆 frame_id 与 page_id；Rid 保存的是 page_no+slot_no，不是缓冲帧号。
