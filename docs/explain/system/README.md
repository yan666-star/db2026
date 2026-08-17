# system 目录详解

## 0. System Manager 相当于数据库的“户籍管理处”

Record 层只知道某个文件里有定长记录，不知道文件代表 student 表。SmManager 保存：有哪些表、每表哪些列、每列在记录哪里、有哪些索引，以及已打开文件句柄。

### 0.1 `create_table(tab_name,col_defs,context)` 参数

```cpp
void create_table(
  const std::string &tab_name,
  const std::vector<ColDef> &col_defs,
  Context *context);
```

| 参数 | 含义 |
|---|---|
| `tab_name` | 用户 CREATE TABLE 的表名 |
| `col_defs` | 按 SQL 顺序排列的列定义 |
| `context` | 当前事务/输出环境，函数不拥有 |

例子 `create table t(id int,name char(8))`：

```text
col_defs[0]={id,INT,4}
col_defs[1]={name,STRING,8}
```

循环后 ColMeta：id offset0，name offset4，record_size=12。

### 0.2 `create_index(tab_name,col_names,context)` 参数

`col_names` 保留用户顺序。例如 `(dept,id)` 与 `(id,dept)` 是两种不同联合键。函数先用 TabMeta.get_col 将字符串列名变成 ColMeta，然后调用 IxManager。

创建后还要扫描旧表。原因：索引文件刚创建是空的，但表可能已有100行；若不装入旧记录，索引查询只能看见之后新插入的行。

### 0.3 `fhs_` 和 `ihs_` 的 key/value

```text
fhs_[table_name] -> unique_ptr<RmFileHandle>
ihs_[index_file_name] -> unique_ptr<IxIndexHandle>
```

SmManager 拥有这些句柄。Executor 只借用 `fhs_.at(name).get()`，不能 delete。

### 0.4 `flush_meta()`

db_ 是内存目录。create/drop 只改 db_ 还不够，重启会读回旧目录；flush_meta 将 DbMeta 序列化到磁盘。ColMeta/IndexMeta 新字段若没写进 `operator<< >>`，也会重启丢失。

## 1. 归属

System Manager 管理数据库目录、表和索引的生命周期，是 SQL DDL 与 record/index 层之间的门面。

| 文件 | 作用 |
|---|---|
| [sm_meta.h](../../src/system/sm_meta.h:24) | ColMeta/IndexMeta/TabMeta/DbMeta |
| [sm_manager.h](../../src/system/sm_manager.h:31) | 系统管理接口和句柄表 |
| [sm_manager.cpp](../../src/system/sm_manager.cpp:354) | 数据库、表、索引、回滚实现 |
| [sm_defs.h](../../src/system/sm_defs.h:1) | 系统层定义 |

`sm.h` 是聚合头文件；CMakeLists 管理 sm_manager.cpp。新增纯头字段不必加源文件，新建 `.cpp` 才需要调整构建列表。

## 2. 元数据变量

### ColMeta

| 变量 | 作用 |
|---|---|
| `tab_name` | 归属表 |
| `name` | 列名 |
| `type` | INT/FLOAT/STRING 等 |
| `len` | 固定字节长度 |
| `offset` | 列在 RmRecord 中的起点 |
| `index` | 旧单列索引标记，当前联合索引主要看 TabMeta.indexes |

### IndexMeta

`tab_name` 是归属表；`cols` 按索引顺序保存列；`col_num` 是列数；`col_tot_len` 是联合键总长。顺序决定联合索引最左前缀，不能用无序集合替代。

### TabMeta/DbMeta

TabMeta 拥有列和索引元数据副本；DbMeta 的 `tabs_` 映射表名到 TabMeta。`get_table/get_col/get_index_meta` 返回引用或迭代器，调用方不得在容器结构变化后继续使用旧迭代器。

## 3. SmManager 成员归属

| 变量 | 所有权 | 作用 |
|---|---|---|
| `db_` | SmManager 自有值 | 当前数据库元数据 |
| `fhs_` | unique_ptr map | 当前打开表文件句柄 |
| `ihs_` | unique_ptr map | 当前打开索引句柄 |
| `disk_manager_` | 借用 | 文件操作 |
| `buffer_pool_manager_` | 借用 | 页缓存 |
| `rm_manager_` | 借用 | 表记录文件管理 |
| `ix_manager_` | 借用 | 索引文件管理 |

## 4. DDL 调用

### create_table

根据 ColDef 逐列累加 offset，形成 ColMeta 和 record_size；RmManager 创建文件；打开句柄加入 fhs_；TabMeta 加入 db_；flush_meta 持久化。

### create_index

按 SQL 给定顺序查 ColMeta，创建 Ix 文件并打开句柄；扫描表中已有记录，按每列 `record.data+col.offset` 拼接 key，插入 `(key,rid)`；登记 IndexMeta。

### drop_table/drop_index

先处理打开句柄和依赖索引，再删除物理文件和元数据。不要只 erase db_，否则磁盘文件残留；也不要只删文件，否则目录仍声称对象存在。

## 5. 回滚

`rollback(WriteRecord*)` 按 WType 分派：

- rollback_insert：删除刚插入记录和索引。
- rollback_delete：恢复旧记录和旧索引键。
- rollback_update：删新键、恢复旧记录、插旧键。

关键变量 `record` 是旧映像，`rid` 是原物理位置，`table_name` 用于定位 fhs_ 和 TabMeta。

## 6. 类似现场改动

### CREATE TABLE 新列属性

Parser/AST ColDef 加字段，Analyze/Optimizer 传递，SmManager 在构造 ColMeta 时落地。若属性需要持久化，要同步 `operator<<` 和 `operator>>`，否则重启丢失。

### CREATE UNIQUE/普通索引区分

IndexMeta 加 `is_unique`，序列化同步；Insert/Update 只有 unique 时做冲突检查；普通索引允许重复键。不要只改语法。

### SHOW 新元数据

优先在 SmManager 遍历 db_ 并通过 Context 输出；无需新 Executor。

### 新表级约束

需要在 TabMeta 持久化，并在 Insert/Update 写表前验证；回滚仍复用物理旧映像。

## 7. 注意

1. 元数据内存与磁盘序列化字段顺序必须完全一致。
2. 联合键列顺序不可改变。
3. 创建索引必须覆盖旧数据。
4. DML 维护 `tab.indexes` 的全部索引。
5. 返回引用前确认对应 map/vector 在使用期不会重分配或 erase。

## 8. `create_table()` 应逐段理解

典型局部变量：

```text
tab_name：目标表
col_defs：来自 DDLPlan 的列定义
curr_offset：下一列字节起点
record_size：最终等于 curr_offset
tab：准备登记的 TabMeta
```

逐列逻辑：

```cpp
ColMeta col;
col.tab_name = tab_name;
col.name = def.name;
col.type = def.type;
col.len = def.len;
col.offset = curr_offset;
curr_offset += col.len;
tab.cols.push_back(col);
```

列顺序同时决定 INSERT values 对应顺序和物理记录布局。不能用 map 按列名字典序重排。

## 9. 专题：增加 DEFAULT 列值

需要持久化到 ColMeta：

```cpp
bool has_default = false;
Value default_value;
```

但 Value 的 raw/shared_ptr 直接用文本流序列化不方便，建议元数据保存类型化文本或单独字段，再由 Insert Analyze 转换。链路：

```text
parser ColDef
 -> system ColDef
 -> ColMeta 持久化
 -> Insert 值数不足时按列补默认值
```

如果题目只要求 `INSERT DEFAULT VALUES`，也不能在 record 层补，因为 record 不认识列。

## 10. 专题：普通索引与唯一索引

元数据增加：

```cpp
bool is_unique = true;
```

序列化：

```cpp
os << index.is_unique;
is >> index.is_unique;
```

字段加入输出和输入的相同位置。然后 Insert/Update：

```cpp
if (index.is_unique) {
    // get_value / MVCC unique conflict check
}
// 无论是否 unique 都要插入索引项
```

前提是 B+树物理实现允许重复键；若不允许，这题会扩大到 index 层，不可只加元数据字段。

## 11. 专题：重命名表或列

这是跨层持久化操作：

- DbMeta.tabs_ 的 map key。
- TabMeta.name。
- 每个 ColMeta.tab_name。
- IndexMeta.tab_name。
- fhs_ 表名 key。
- ihs_ 的索引文件名及磁盘文件。

仅修改 TabMeta.name 会导致 get_table(new_name) 找不到。资格赛若要求简单 RENAME，先确认是否要求重启后保持；要求持久化则需物理文件 rename 和 flush_meta。

## 12. 专题：SHOW CREATE TABLE

无需 Executor。QlManager/SmManager 从 TabMeta 拼 SQL：按 cols 顺序输出类型和长度，再输出索引。变量读取而不修改元数据。输出通过 `context->data_send_` 和 `offset_`，写入前检查缓冲容量。

## 13. `create_index()` 联合键实例

表记录布局：id offset0 len4，dept offset4 len4，name offset8 len20。索引 `(dept,id)`：

```cpp
char key[8];
memcpy(key,     rec.data + 4, 4); // dept
memcpy(key + 4, rec.data + 0, 4); // id
```

不能按表列顺序写成 id+dept。`key_offset` 由 index.cols 循环累加，而源 offset 取每个 ColMeta.offset。

## 14. SmManager 改动同步表

| 改动 | 还要检查 |
|---|---|
| ColMeta 新字段 | <<、>>、create_table、desc/show |
| IndexMeta 新字段 | <<、>>、create_index、DML |
| 表文件名规则 | open/close/drop/recovery |
| 索引文件名规则 | ihs_ 全部查找点、IxManager |
| 回滚动作 | 表记录、每个索引、MVCC路径 |

## 15. open_db/close_db 为什么重要

open_db 从元数据文件恢复 db_，打开每张表到 fhs_，打开每个索引到 ihs_。新增元数据字段后，不仅 create 时赋值，反序列化也要恢复，否则重启后的运行状态不同。

close_db 应刷新/关闭句柄并清 map。外部仍持有 RmFileHandle/IxIndexHandle 裸指针时清理会悬空，所以数据库关闭前不应还有 Executor 运行。

## 16. 专题：DROP COLUMN（若题目简化）

真实删除列会改变所有记录布局和索引，必须重写表文件，不是只 erase ColMeta。资格赛若给简化要求，可创建新临时表布局、逐记录投影复制、重建索引、替换文件；这已经属于较大题。

若题目只要求元数据显示删除但旧记录保持，后续 ColMeta.offset 仍可读取其他列，但 record_size 不变；必须明确被删列相关索引先删除。
