# `sm_manager.h` 详细讲解

源码入口：[sm_manager.h](../../src/system/sm_manager.h:24)

这个文件声明 **系统管理器 `SmManager`**：数据库的"户籍管理处"。它保存库元数据、所有打开的表/索引句柄，并执行 DDL（create/drop table、create/drop index）和事务回滚。

## 一、`ColDef`（建表列定义）

位置：[sm_manager.h](../../src/system/sm_manager.h:24)

```cpp
struct ColDef {
    std::string name;  // Column name
    ColType type;      // Type of column
    int len;           // Length of column
};
```

这是 **Planner → SmManager 之间传递的建表列定义**。Planner 从 `ast::ColDef` 转成这个 `system::ColDef`（见 planner.cpp 的 CreateTable 分支），再传给 `SmManager::create_table`。

## 二、`SmManager` 成员变量逐个解释

位置：[sm_manager.h](../../src/system/sm_manager.h:31)

### 2.1 公开成员（元数据 + 句柄）

```cpp
DbMeta db_;             // 当前打开的数据库的元数据
std::unordered_map<std::string, std::unique_ptr<RmFileHandle>> fhs_;  // 表名 -> 表文件句柄
std::unordered_map<std::string, std::unique_ptr<IxIndexHandle>> ihs_; // 索引文件名 -> 索引句柄
```

| 成员 | 类型 | 作用 |
|---|---|---|
| `db_` | DbMeta | 内存中的库目录：所有表和索引的元数据 |
| `fhs_` | map\<string, unique_ptr\<RmFileHandle>> | **表名** → 已打开的表记录文件句柄。Executor 用 `fhs_.at(name).get()` 借用 |
| `ihs_` | map\<string, unique_ptr\<IxIndexHandle>> | **索引文件名** → 已打开的 B+树句柄。索引文件名用 `ix_manager_->get_index_name(tab_name, cols)` 生成 |

**所有权**：SmManager **拥有**这些句柄（unique_ptr）。Executor 只借用 `.get()` 裸指针，不能 delete。

### 2.2 私有成员（依赖管理器，全部借用）

```cpp
DiskManager* disk_manager_;
BufferPoolManager* buffer_pool_manager_;
RmManager* rm_manager_;
IxManager* ix_manager_;
```

都是借用指针，由 rmdb.cpp 创建后传入构造函数，SmManager 不负责 delete。`get_bpm()` / `get_rm_manager()` / `get_ix_manager()` 提供公开访问。

## 三、接口逐个解释

### 3.1 数据库生命周期

```cpp
bool is_dir(const std::string& db_name);
void create_db(const std::string& db_name);
void drop_db(const std::string& db_name);
void open_db(const std::string& db_name);
void close_db();
void flush_meta();
```

- `create_db`：建同名文件夹、写入空 DbMeta 到 `DB_META_NAME`、创建日志文件。
- `open_db`：进入文件夹、从 `DB_META_NAME` 读回 `db_`、打开每张表的 fh 和每个索引的 ih。
- `flush_meta`：把 `db_` 写回 `DB_META_NAME`。**内存目录改了不 flush，重启就丢**。
- `close_db`：关闭所有句柄、flush_meta、退回上级目录。

### 3.2 DDL

```cpp
void show_tables(Context* context);
void desc_table(const std::string& tab_name, Context* context);
void create_table(const std::string& tab_name, const std::vector<ColDef>& col_defs, Context* context);
void drop_table(const std::string& tab_name, Context* context);
void create_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context);
void drop_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context);
void drop_index(const std::string& tab_name, const std::vector<ColMeta>& col_names, Context* context);  // 重载
void show_index(const std::string& tab_name, Context* context);
```

注意 `drop_index` 有两个重载：一个收 `vector<string>`（列名），一个收 `vector<ColMeta>`（列元数据，内部转成列名再调前一个）。`Context*` 用于事务环境/输出缓冲。

### 3.3 回滚

```cpp
void rollback(WriteRecord* record, Context* context);
void rollback_insert(const std::string& table_name, Rid& rid, Context* context);
void rollback_delete(const std::string& table_name, Rid& rid, RmRecord& record, Context* context);
void rollback_update(const std::string& table_name, Rid& rid, RmRecord& record, Context* context);
```

`rollback` 按 `WriteRecord` 的写类型分派到三个具体回滚函数。**它们是事务 ABORT 的物理撤销逻辑**，见 sm_manager.cpp 详解。

## 四、易错点总结

1. `db_` 是内存目录，改动后必须 `flush_meta()`，否则重启丢失。
2. `fhs_`/`ihs_` 的 key 一个是**表名**、一个是**索引文件名**，别混。索引文件名必须用 `ix_manager_->get_index_name()` 生成，不要手拼字符串。
3. 句柄是 unique_ptr，Executor 只借用裸指针，析构时不能 delete。
4. drop 操作必须"句柄 + 物理文件 + 元数据"三处一起清，只删一处会残留。
5. `Context*` 是借用指针，不负责释放。
