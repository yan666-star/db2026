# `sm_manager.cpp` 详细讲解

源码入口：[sm_manager.cpp](../../src/system/sm_manager.cpp:57)

这个文件是 SmManager 的**实现**：数据库/表/索引的生命周期、元数据落地、事务回滚。元数据与 DDL 相关的实现都集中在这里。阅读顺序：`create_table` → `create_index` → `drop_table/drop_index` → `rollback_*`。

## 〇、文件顶部的匿名命名空间

```cpp
void copy_file_for_checkpoint(const std::string &source, const std::string &destination);
```

一个文件复制工具，给 checkpooint 快照（`create_index_snapshots` / `restore_index_snapshots`）用。与常规功能无关。

## 一、数据库生命周期

### 1.1 `create_db(db_name)`

```cpp
void SmManager::create_db(const std::string& db_name) {
    if (is_dir(db_name)) throw DatabaseExistsError(db_name);
    std::string cmd = "mkdir " + db_name;              // 建同名文件夹
    if (system(cmd.c_str()) < 0) throw UnixError();
    if (chdir(db_name.c_str()) < 0) throw UnixError(); // 进入文件夹
    DbMeta *new_db = new DbMeta();
    new_db->name_ = db_name;
    std::ofstream ofs(DB_META_NAME);                   // 创建元数据文件
    ofs << *new_db;                                    // 序列化写入空 DbMeta
    delete new_db;
    disk_manager_->create_file(LOG_FILE_NAME);         // 创建日志文件
    if (chdir("..") < 0) throw UnixError();
}
```

**关键**：用 `system("mkdir ...")` 建文件夹，然后 `chdir` 进入——**当前工作目录会临时变成数据库目录**，创建完再 `chdir("..")` 退回。这就是为什么 open_db/close_db 也有 chdir：**整个数据库操作都在"cd 进数据库文件夹"的假设下进行**，文件路径都是相对当前目录的。

### 1.2 `open_db(db_name)`

```cpp
void SmManager::open_db(const std::string& db_name) {
    ...
    if (chdir(db_name.c_str()) < 0) throw UnixError();
    std::ifstream ifs(DB_META_NAME);
    ifs >> db_;                                  // 读回全部元数据（靠 operator>>）
    ifs.close();
    fhs_.clear();  ihs_.clear();
    for (auto &entry : db_.tabs_) {
        const std::string &tab_name = entry.first;
        fhs_.emplace(tab_name, rm_manager_->open_file(tab_name));       // 打开每张表
        for (auto &index : entry.second.indexes) {
            std::string ix_name = ix_manager_->get_index_name(tab_name, index.cols);
            ihs_.emplace(ix_name, ix_manager_->open_index(tab_name, index.cols));  // 打开每个索引
        }
    }
}
```

### 1.3 `flush_meta()`

```cpp
void SmManager::flush_meta() {
    std::ofstream ofs(DB_META_NAME);   // 默认清空文件（trunc）
    ofs << db_;                        // 重新写全部元数据
}
```

`std::ofstream` 默认以 trunc 打开，所以"先清空再全量重写"。

### 1.4 `close_db()`

```cpp
void SmManager::close_db() {
    for (auto &entry : fhs_) rm_manager_->close_file(entry.second.get());
    fhs_.clear();
    for (auto &entry : ihs_) ix_manager_->close_index(entry.second.get());
    ihs_.clear();
    flush_meta();
    if (chdir("..") < 0) throw UnixError();
}
```

关句柄、清 map、flush 元数据、退回上级目录。

## 二、`show_tables` / `desc_table` / `show_index`（输出类）

这三个都是**只读输出**，用 `RecordPrinter` 打印，并通过 `enable_output_file` 开关同时写入 `output.txt`。

`desc_table` 逐个列打印：

```cpp
std::vector<std::string> field_info = {col.name, coltype2str(col.type), col.index ? "YES" : "NO"};
```

注意第三列用的是 `col.index`，但 `ColMeta::index` 是 **unused** 字段，所以这里永远打印 "NO"。这是实现上容易忽略的细节。

`show_index` 打印每个索引的列组合字符串，第三列硬编码 `"unique"`。

## 三、`create_table`（核心，逐段解释）

位置：[sm_manager.cpp](../../src/system/sm_manager.cpp:361)

```cpp
void SmManager::create_table(const std::string& tab_name, const std::vector<ColDef>& col_defs, Context* context) {
    if (db_.is_table(tab_name)) throw TableExistsError(tab_name);
    int curr_offset = 0;
    TabMeta tab;
    tab.name = tab_name;
    for (auto &col_def : col_defs) {
        ColMeta col = {.tab_name = tab_name,
                       .name = col_def.name,
                       .type = col_def.type,
                       .len = col_def.len,
                       .offset = curr_offset,        // 当前累加偏移
                       .index = false};
        curr_offset += col_def.len;                  // 下一个列从这开始
        tab.cols.push_back(col);
    }
    int record_size = curr_offset;                   // 记录总长 = 最后一列结束位置
    rm_manager_->create_file(tab_name, record_size); // 创建表文件（定长记录）
    db_.tabs_[tab_name] = tab;                       // 登记元数据
    fhs_.emplace(tab_name, rm_manager_->open_file(tab_name));  // 打开句柄
    flush_meta();                                    // 持久化
}
```

**逐个变量**：

| 变量 | 作用 |
|---|---|
| `curr_offset` | 下一个列的起始偏移，逐列累加 `len` |
| `col` | 拼出的 ColMeta，`offset` 用当前 curr_offset |
| `record_size` | 循环结束后 curr_offset = 记录总字节数。传给 `rm_manager_->create_file` 决定定长记录大小 |
| `tab` | 组装中的 TabMeta |
| `fhs_.emplace` | 打开文件句柄并存进 fhs_ |

**关键点**：
1. **列顺序 = 物理布局顺序 = INSERT values 对应顺序**。不能用 map 按列名重排。
2. `record_size` 必须等于所有列长之和。record 层用它在页里算槽地址。
3. create 完成后立即 `flush_meta()`，保证重启不丢。

## 四、`create_index`（核心，逐段解释）

位置：[sm_manager.cpp](../../src/system/sm_manager.cpp:430)

```cpp
void SmManager::create_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context) {
    TabMeta& tab = db_.get_table(tab_name);
    if (ix_manager_->exists(tab_name, col_names)) throw IndexExistsError(tab_name, col_names);

    // 1. 把字符串列名变成 ColMeta，累加联合键总长
    std::vector<ColMeta> cols(col_names.size());
    int col_tot_len = 0;
    for (size_t i = 0; i < col_names.size(); ++i) {
        auto col = tab.get_col(col_names[i]);   // 按列名查 ColMeta
        cols[i] = *col;
        col_tot_len += col->len;
    }

    // 2. 创建并打开索引文件
    ix_manager_->create_index(tab_name, cols);
    auto index_handle = ix_manager_->open_index(tab_name, cols);
    // 3. 登记 IndexMeta 到表元数据
    tab.indexes.push_back({tab_name, col_tot_len, static_cast<int>(cols.size()), cols});
    ihs_.emplace(ix_manager_->get_index_name(tab_name, col_names), std::move(index_handle));
    flush_meta();

    // 4. 扫描旧表，把已有记录装进索引（关键！否则旧数据查不到）
    auto rm_handle = fhs_.at(tab_name).get();
    auto ih = ihs_.at(ix_manager_->get_index_name(tab_name, cols)).get();
    char* key = new char[col_tot_len];
    RmScan rm_scan(rm_handle);
    while (!rm_scan.is_end()) {
        auto record = rm_handle->get_record(rm_scan.rid(), context);
        if (!record) { rm_scan.next(); continue; }
        int offset = 0;
        for (size_t i = 0; i < cols.size(); ++i) {
            memcpy(key + offset, record->data + cols[i].offset, cols[i].len);  // 按索引列序拼 key
            offset += cols[i].len;
        }
        // 5. 唯一性预查：若 key 已存在，回滚建索引（删文件、删元数据）并抛 failure
        std::vector<Rid> tmp_result;
        if (ih->get_value(key, &tmp_result, context == nullptr ? nullptr : context->txn_)) {
            // 清理：close、destroy、pop_back、flush、throw
            ...
            throw RMDBError("failure");
        }
        ih->insert_entry(key, rm_scan.rid(), context == nullptr ? nullptr : context->txn_);
        rm_scan.next();
    }
    delete[] key;
    ih->flush_file_header();
    buffer_pool_manager_->flush_all_pages(ih->GetFd());
    disk_manager_->sync_all_open_files();
}
```

**逐个要点**：

| 步骤 | 作用 |
|---|---|
| `tab.get_col` | 把列名字符串变成 ColMeta（带 offset/len） |
| `col_tot_len` | 联合键总长，创建 B+树文件时用 |
| `tab.indexes.push_back` | 登记 IndexMeta（**顺序 = 联合键顺序**） |
| RmScan 扫表 | **建索引必须覆盖旧数据**，否则只对之后插入的行生效 |
| `memcpy(key + offset, record->data + cols[i].offset, cols[i].len)` | **按索引列顺序**拼 key，源偏移是 ColMeta.offset，目标偏移是索引序累加 |
| `get_value` 预查 | 已存在的 key → 说明有重复 → 撤销建索引并抛 failure |
| `insert_entry(key, rid)` | 把 (key → 表 Rid) 插进 B+树 |

**create_index 的几个易错点**：
1. **联合键拼序**：`(dept,id)` 索引，key = dept 4 字节 + id 4 字节。源偏移分别取 dept 和 id 的 ColMeta.offset，**不是**按表列顺序。写错就插错键。
2. **唯一性预查**：当前实现**默认索引是唯一的**——遇到重复 key 直接抛 failure 并撤销。
3. **清理顺序**：预查失败时，先 close/destroy 索引文件、erase ihs_、pop_back 元数据、flush_meta，最后才 throw。漏一步会残留。
4. 创建完要 `flush_file_header` + `flush_all_pages` + `sync`，保证索引落盘。

## 五、`drop_table` / `drop_index`

### 5.1 `drop_table`

```cpp
void SmManager::drop_table(const std::string& tab_name, Context* context) {
    if (!db_.is_table(tab_name)) throw TableNotFoundError(tab_name);
    TabMeta tab = db_.get_table(tab_name);
    // 1. 关掉这个表的所有索引句柄，并从 ihs_ 移除
    for (auto &index : tab.indexes) {
        std::string ix_name = ix_manager_->get_index_name(tab_name, index.cols);
        auto it = ihs_.find(ix_name);
        if (it != ihs_.end()) { ix_manager_->close_index(it->second.get()); ihs_.erase(it); }
    }
    // 2. 关掉表文件句柄
    auto fh_it = fhs_.find(tab_name);
    if (fh_it != fhs_.end()) { rm_manager_->close_file(fh_it->second.get()); fhs_.erase(fh_it); }
    // 3. 删元数据 + flush
    db_.tabs_.erase(tab_name);
    flush_meta();
    // 4. 删除物理文件
    for (auto &index : tab.indexes) ix_manager_->destroy_index(tab_name, index.cols);
    rm_manager_->destroy_file(tab_name);
}
```

**顺序是关键**：先关句柄/移除 map，再删元数据并 flush，最后删物理文件。只 erase db_ 不删文件会残留；只删文件不 erase db_ 目录仍声称存在。

### 5.2 `drop_index`

```cpp
void SmManager::drop_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context) {
    if (!ix_manager_->exists(tab_name, col_names)) throw IndexNotFoundError(tab_name, col_names);
    auto index_name = ix_manager_->get_index_name(tab_name, col_names);
    TabMeta& tab = db_.get_table(tab_name);
    auto index_meta = tab.get_index_meta(col_names);  // 用最左前缀匹配找到索引
    tab.indexes.erase(index_meta);                    // 删元数据
    ix_manager_->close_index(ihs_.at(index_name).get());   // 关句柄
    ix_manager_->destroy_index(tab_name, col_names);       // 删文件
    ihs_.erase(index_name);                                // 移除句柄 map
    flush_meta();
}
```

另一个重载 `drop_index(tab_name, vector<ColMeta>, context)` 把 ColMeta 转成列名后委托给上面这个。

## 六、回滚（事务 ABORT 的物理撤销）

位置：[sm_manager.cpp](../../src/system/sm_manager.cpp:549)

### 6.1 `rollback(record, context)` —— 总分发

```cpp
void SmManager::rollback(WriteRecord* record, Context* context) {
    switch (record->GetWriteType()) {
        case WType::INSERT_TUPLE: rollback_insert(record->GetTableName(), record->GetRid(), context); break;
        case WType::DELETE_TUPLE: rollback_delete(record->GetTableName(), record->GetRid(), record->GetRecord(), context); break;
        case WType::UPDATE_TUPLE: rollback_update(record->GetTableName(), record->GetRid(), record->GetRecord(), context); break;
        default: throw RMDBError("Invalid rollback type");
    }
}
```

按 WriteRecord 的类型分派。**INSERT 的 WriteRecord 不需要旧记录**（撤销=删除 rid），**DELETE/UPDATE 必须保存旧记录**。

### 6.2 `rollback_insert` —— 撤销插入

```cpp
void SmManager::rollback_insert(const std::string& table_name, Rid& rid, Context* context) {
    auto file_handle = fhs_.at(table_name).get();
    std::unique_ptr<RmRecord> inserted_record;
    try { inserted_record = file_handle->get_record(rid, nullptr); }
    catch (const RecordNotFoundError&) { return; }   // 记录已不存在（可能被后续操作删了），无需撤销

    // 1. 对每个索引：用当前记录拼 key，删除索引项
    for (auto& index_meta : db_.get_table(table_name).indexes) {
        // 按 index_meta.cols 顺序拼 key_buf，index_handle->delete_entry(key_buf, txn)
    }
    // 2. 删除表记录
    file_handle->delete_record(rid, context);
}
```

**顺序**：先删索引项，再删表记录。因为删表记录后 rid 位置无效，取不到 key 字节了。

### 6.3 `rollback_delete` —— 撤销删除（恢复记录）

```cpp
void SmManager::rollback_delete(const std::string& table_name, Rid& rid, RmRecord& record, Context* context) {
    auto file_handle = fhs_.at(table_name).get();
    file_handle->insert_record(rid, record.data);   // 按原 rid 插回旧记录
    // 对每个索引：按旧记录拼 key，insert_entry(key, rid)
    ...
}
```

**注意**：`insert_record(rid, data)` 是**指定位置恢复**（不是找新槽）。旧记录深拷贝保存在 WriteRecord 里，所以能恢复原样。然后重建所有索引项。

### 6.4 `rollback_update` —— 撤销更新

```cpp
void SmManager::rollback_update(const std::string& table_name, Rid& rid, RmRecord& record, Context* context) {
    auto file_handle = fhs_.at(table_name).get();
    std::unique_ptr<RmRecord> new_record;
    try { new_record = file_handle->get_record(rid, nullptr); }
    catch (const RecordNotFoundError&) { return; }

    file_handle->update_record(rid, record.data, context);   // 把表记录覆盖回旧值

    for (auto& index_meta : ...) {
        // 拼 old_key（旧记录）和 new_key（当前新记录）
        if (memcmp(old_key, new_key, index_meta.col_tot_len) == 0) continue;  // 键没变，跳过
        index_handle->insert_entry(old_key, rid, txn);    // 插回旧键
        index_handle->delete_entry(new_key, txn);         // 删掉新键
    }
}
```

**关键点**：
1. 先覆盖表记录，再修索引。
2. `old_key` 来自 WriteRecord 保存的旧记录，`new_key` 来自当前表里的新记录。
3. `memcmp` 比较键：**没变就跳过**（避免白插白删）。
4. 键变了：插入旧键、删除新键。

## 七、易错点总结

1. create_index 的联合键拼序是**索引列序**，源偏移是 ColMeta.offset，别按表列序拼。
2. create_index **必须扫旧表装索引**，否则旧数据查不到。
3. drop_table 顺序：关句柄 → 删元数据 → flush → 删物理文件，三处都要做。
4. 回滚顺序：INSERT 先删索引再删记录；UPDATE 先覆盖记录再改索引（键没变跳过）；DELETE 先插记录再建索引。
5. `desc_table` 用的 `col.index` 是 unused 字段，永远 "NO"。
6. 任何 DDL 改动后要 flush_meta，重启才不丢。
7. `create_index` 唯一性预查抛 failure 前必须清理干净（关文件、删元数据），否则残留。
