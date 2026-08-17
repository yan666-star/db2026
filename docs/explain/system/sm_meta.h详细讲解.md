# `sm_meta.h` 详细讲解

源码入口：[sm_meta.h](../../src/system/sm_meta.h:24)

这个文件定义数据库的**元数据结构**：列、索引、表、整个库。它们就是内存里的"户籍册"，`SmManager::db_` 就是这些结构的组合。

## 〇、四个结构的关系

```text
DbMeta (一个库)
 └─ tabs_: map<表名, TabMeta>
      └─ TabMeta (一张表)
           ├─ cols_: vector<ColMeta>      ← 列
           └─ indexes_: vector<IndexMeta> ← 索引
                └─ cols_: vector<ColMeta> ← 索引包含的列
```

每个结构都重载了 `operator<<` 和 `operator>>`，用于**把元数据写成文本文件**（`DB_META_NAME`）和读回来。

## 一、`ColMeta`（列元数据）逐个字段

位置：[sm_meta.h](../../src/system/sm_meta.h:24)

```cpp
struct ColMeta {
    std::string tab_name;   // 字段所属表名称
    std::string name;       // 字段名称
    ColType type;           // 字段类型 (INT/FLOAT/STRING)
    int len;                // 字段长度（字节）
    int offset;             // 字段位于记录中的偏移量
    bool index;             /** unused */
    ...
};
```

| 字段 | 类型 | 作用 |
|---|---|---|
| `tab_name` | string | 所属表名（Analyze 绑定列时按它定位） |
| `name` | string | 列名 |
| `type` | ColType | `TYPE_INT` / `TYPE_FLOAT` / `TYPE_STRING`（定义在 defs.h） |
| `len` | int | 定长字节数（INT=4，FLOAT=4，CHAR(n)=n） |
| `offset` | int | 该列在记录中的起始偏移。**由 create_table 顺序累加** |
| `index` | bool | **unused（注释标注）**。历史遗留，不要依赖它判断是否有索引；联合索引看 `TabMeta::indexes` |

**offset 的重要性**：记录是一串连续字节，`record.data + col.offset` 就是这一列的起始地址。所有 Executor 读列值都靠它。

### 1.1 序列化

```cpp
friend std::ostream &operator<<(std::ostream &os, const ColMeta &col) {
    return os << col.tab_name << ' ' << col.name << ' ' << col.type << ' ' << col.len << ' ' << col.offset << ' '
              << col.index;
}

friend std::istream &operator>>(std::istream &is, ColMeta &col) {
    return is >> col.tab_name >> col.name >> col.type >> col.len >> col.offset >> col.index;
}
```

**关键规则**：`<<` 写的字段顺序必须和 `>>` 读的字段顺序**完全一致**，否则重启读元数据时字段错位、数据损坏。`ColType` 的 `<< >>` 重载在 defs.h 里，所以这里直接 `os << col.type` 就能读写枚举。

## 二、`IndexMeta`（索引元数据）逐个字段

位置：[sm_meta.h](../../src/system/sm_meta.h:44)

```cpp
struct IndexMeta {
    std::string tab_name;           // 索引所属表名称
    int col_tot_len;                // 索引字段长度总和（一个联合键的总字节数）
    int col_num;                    // 索引字段数量
    std::vector<ColMeta> cols;      // 索引包含的字段（按索引顺序）
    ...
};
```

| 字段 | 类型 | 作用 |
|---|---|---|
| `tab_name` | string | 所属表 |
| `col_tot_len` | int | 联合键总长 = 所有 `cols[i].len` 之和。B+树的 key 长度就是它 |
| `col_num` | int | 索引列数 |
| `cols` | vector\<ColMeta> | 索引列，**顺序决定最左前缀**。`(dept,id)` 和 `(id,dept)` 是两个不同索引 |

**顺序不可变**：`IndexMeta::cols` 的排列顺序 = 联合键字节拼接顺序 = 最左前缀匹配顺序。用 `std::set` 之类无序集合保存会丢前缀语义。

### 2.1 序列化

```cpp
friend std::ostream &operator<<(std::ostream &os, const IndexMeta &index) {
    os << index.tab_name << " " << index.col_tot_len << " " << index.col_num;
    for(auto& col: index.cols) { os << "\n" << col; }
    return os;
}
friend std::istream &operator>>(std::istream &is, IndexMeta &index) {
    is >> index.tab_name >> index.col_tot_len >> index.col_num;
    for(int i = 0; i < index.col_num; ++i) { ColMeta col; is >> col; index.cols.push_back(col); }
    return is;
}
```

`>>` 按 `col_num` 循环读 `col_num` 个 ColMeta，保证读回顺序与写入一致。

## 三、`TabMeta`（表元数据）逐个字段与辅助函数

位置：[sm_meta.h](../../src/system/sm_meta.h:70)

```cpp
struct TabMeta {
    std::string name;                   // 表名称
    std::vector<ColMeta> cols;          // 表包含的字段
    std::vector<IndexMeta> indexes;     // 表上建立的索引
    ...
};
```

### 3.1 `is_col(col_name)`

```cpp
bool is_col(const std::string &col_name) const {
    auto pos = std::find_if(cols.begin(), cols.end(), [&](const ColMeta &col) { return col.name == col_name; });
    return pos != cols.end();
}
```

按列名在 `cols` 里找，找到返回 true。

### 3.2 `is_index(col_names)`

```cpp
bool is_index(const std::vector<std::string>& col_names) const {
    for(auto& index: indexes) {
        if(index.col_num == col_names.size()) {
            size_t i = 0;
            for(; i < index.col_num; ++i) {
                if(index.cols[i].name.compare(col_names[i]) != 0) break;
            }
            if(i == index.col_num) return true;   // 列序和列名完全一致
        }
    }
    return false;
}
```

判断是否存在"列序完全一致"的索引。**顺序敏感**：`(a,b)` 和 `(b,a)` 是不同索引。

### 3.3 `get_index_meta(col_names)` —— 最左前缀匹配 + 打分

位置：[sm_meta.h](../../src/system/sm_meta.h:103)

这是**联合索引选型的关键函数**，逐段解释：

```cpp
std::vector<IndexMeta>::iterator get_index_meta(const std::vector<std::string>& col_names) {
    struct IndexCandidate {
        std::vector<IndexMeta>::iterator iter;
        int consecutive_prefix_len;   // 连续命中前缀长度（主要排序键）
        int match_score;              // 命中列数打分（次要排序键）
        bool has_first_col;           // 索引第一列是否在查询列里
    };
    std::vector<IndexCandidate> candidates;
    std::set<std::string> query_col_set(col_names.begin(), col_names.end());

    for (auto index = indexes.begin(); index != indexes.end(); ++index) {
        // 1. 检查第一列：索引的 cols[0] 必须出现在查询列里，否则该索引不可定位，跳过
        // 2. 从 cols[0] 开始连续数命中前缀 consecutive_prefix_len（一旦断档就 break）
        // 3. 再统计所有查询列在索引里的出现次数累加 match_score（次要打分）
        candidates.push_back(candidate);
    }
    if (candidates.empty()) throw IndexNotFoundError(name, col_names);

    // 选 candidates 里 consecutive_prefix_len 最大；相同时 match_score 最大
    auto best = std::max_element(...);
    return best->iter;
}
```

**打分规则**：

```text
第一排序键：consecutive_prefix_len（连续前缀命中长度）→ 越大越好
第二排序键：match_score               → 越大越好
硬性前提：索引第一列 cols[0] 必须被查询列覆盖（否则 break/跳过）
```

**为什么前缀必须是连续的**：B+树按索引列顺序拼 key，`(a,b)` 索引只有约束了 a 才能二分定位 b 的范围。若查询只约束 b，`has_first_col=false`，该索引不可用。

**与 Planner::get_index_cols 的区别**：`get_index_cols`（planner.cpp）是 Planner 侧"选物理扫描方式"；`get_index_meta` 是 SmManager 侧"从元数据里挑出具体索引"。逻辑相似，都坚持最左前缀。

### 3.4 `get_col(col_name)`

```cpp
std::vector<ColMeta>::iterator get_col(const std::string &col_name) {
    auto pos = std::find_if(...);
    if (pos == cols.end()) throw ColumnNotFoundError(col_name);
    return pos;
}
```

按列名找 ColMeta，找不到抛 `ColumnNotFoundError`。create_index 用它把字符串列名变成 ColMeta。

### 3.5 TabMeta 序列化

```cpp
os << tab.name << '\n' << tab.cols.size() << '\n';
for (auto &col : tab.cols) os << col << '\n';
os << tab.indexes.size() << "\n";
for (auto &index : tab.indexes) os << index << "\n";
```

先写列数再逐列写；再写索引数再逐索引写。读回对称。

## 四、`DbMeta`（数据库元数据）逐个字段

位置：[sm_meta.h](../../src/system/sm_meta.h:208)

```cpp
class DbMeta {
    friend class SmManager;   // SmManager 可以直接访问私有成员
   private:
    std::string name_;                      // 数据库名称
    std::map<std::string, TabMeta> tabs_;   // 表名 -> 表元数据
   public:
    bool is_table(const std::string &tab_name) const;   // 表是否存在
    void SetTabMeta(const std::string &tab_name, const TabMeta &meta);
    TabMeta &get_table(const std::string &tab_name);    // 取表元数据，不存在抛 TableNotFoundError
};
```

| 成员 | 类型 | 作用 |
|---|---|---|
| `name_` | string | 数据库名 |
| `tabs_` | map\<string, TabMeta> | 所有表的元数据，**表名作 key** |

注意：
- `tabs_` 是 **private**，但 `SmManager` 是 `friend`，所以 `db_.tabs_[tab_name] = tab` 这类直接操作在 SmManager 里是合法的。
- `get_table` 返回**引用**，调用方可以用它改表元数据，但要注意：如果在持有 `TabMeta&` 期间又对 `tabs_` 做 insert（rehash），**旧引用不失效**（std::map 是节点容器，插入不使迭代器失效）。但 erase 会让指向被删节点的引用失效。

### 4.1 序列化

```cpp
os << db_meta.name_ << '\n' << db_meta.tabs_.size() << '\n';
for (auto &entry : db_meta.tabs_) os << entry.second << '\n';   // 逐个 TabMeta

is >> db_meta.name_ >> n;
for (i...) { TabMeta tab; is >> tab; db_meta.tabs_[tab.name] = tab; }
```

读回时用 `tab.name` 作为 map 的 key 重新注册。

## 五、易错点总结

1. **`<<` 与 `>>` 字段顺序必须完全一致**，否则重启元数据错位。
2. `IndexMeta::cols` 顺序 = 联合键顺序 = 最左前缀。不要用无序容器保存。
3. `ColMeta::index` 是 unused，判断索引要看 `TabMeta::indexes`。
4. `get_index_meta` 的第一列硬性前提：索引 `cols[0]` 必须被查询列覆盖。
5. `tabs_` 是 map，insert 不使已有引用失效，但 erase 会。
