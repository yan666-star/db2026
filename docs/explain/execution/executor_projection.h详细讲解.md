# `executor_projection.h` 详细讲解

源码入口：[executor_projection.h](../../src/execution/executor_projection.h:38)

这个文件是 **投影执行器 `ProjectionExecutor`**，同时可选实现 **LIMIT**。

## 一、投影执行器在做什么

`SELECT score, id FROM student` 时，下层的 SeqScan 输出整行（id+name+score），Projection 只挑出 `score, id` 两列拼成新记录，并按新顺序重排 offset：

```text
原表记录:  id(0-3)  name(4-23)  score(24-27)      len=28
投影输出:  score(0-3)  id(4-7)                    len=8
sel_idxs_ = [2, 0]                                ← score 在下层 cols 的下标是 2，id 是 0
```

## 二、成员变量逐个解释

位置：[executor_projection.h](../../src/execution/executor_projection.h:39)

```cpp
ProjectionPlan *plan_ = nullptr;         // 对应 ProjectionPlan；输出一行 rows_++
std::unique_ptr<AbstractExecutor> prev_; // 子执行器
std::vector<ColMeta> cols_;              // 投影后输出 schema（offset 从 0 重排）
size_t len_;                             // 投影后元组总长度
std::vector<size_t> sel_idxs_;           // 每个投影列在 prev_->cols() 中的下标
int limit_;                              // 最多输出行数；-1 → INT_MAX
int result_idx_ = 0;                     // 已推进的输出计数（配合 LIMIT）
bool is_sel_all_ = false;                // true：SELECT * 且列序一致，可透传
```

逐个解释：

| 变量 | 类型 | 含义 |
|---|---|---|
| `plan_` | ProjectionPlan\* | 借用指针，提供 `limit_num_` 和 `rows_` 统计 |
| `prev_` | unique_ptr | 独占子执行器所有权 |
| `cols_` | vector\<ColMeta> | 投影后 schema，offset 从 0 重排（**不是原表 offset**） |
| `len_` | size_t | 投影后记录字节数 |
| `sel_idxs_` | vector\<size_t> | 每个投影列在下层 schema 里的下标 |
| `limit_` | int | 最多输出行数；`limit_num_==-1` 时换成 `INT_MAX`（无限） |
| `result_idx_` | int | 已输出计数；`result_idx_ >= limit_` 时结束 |
| `is_sel_all_` | bool | `SELECT *` 且列序与下层一致时 true，直接透传记录不拷贝 |

## 三、构造函数做了什么

位置：[executor_projection.h](../../src/execution/executor_projection.h:60)

```cpp
ProjectionExecutor(std::unique_ptr<AbstractExecutor> prev,
                   const std::vector<TabCol> &sel_cols,
                   ProjectionPlan *plan = nullptr) {
```

三步：

1. **解析 limit**：

```cpp
limit_ = (plan_ != nullptr) ? plan_->limit_num_ : -1;
if (limit_ == -1) limit_ = std::numeric_limits<int>::max();
```

`limit_num_ == -1` 表示无 LIMIT，这里换成 INT_MAX 当"无限"。`limit_ == 0` 表示 LIMIT 0，输出空。

2. **按 sel_cols 定位并重排**：

```cpp
size_t curr_offset = 0;
for (auto &sel_col : sel_cols) {
    auto pos = get_col(prev_cols, sel_col);       // 在下层 schema 里找列
    sel_idxs_.push_back(pos - prev_cols.begin()); // 记下标
    auto col = *pos;
    col.name = sel_col.col_name;                  // 覆盖列名（AS 别名）
    col.tab_name = sel_col.tab_name;
    col.offset = curr_offset;                     // 新 offset 从 0 重排
    curr_offset += col.len;
    cols_.push_back(col);
}
len_ = curr_offset;
```

3. **判断 `is_sel_all_`**：

```cpp
is_sel_all_ = true;
if (sel_idxs_.size() != prev_cols.size()) is_sel_all_ = false;
else for (i...) if (sel_idxs_[i] != i) is_sel_all_ = false;
```

只有"选中的列数 == 下层列数"且"顺序完全一致"（`sel_idxs_[i]==i`）才算 `SELECT *`，可以透传。`SELECT *` 但列序不同（罕见）会走复制路径。

## 四、四个迭代接口

### 4.1 `is_end()`

```cpp
bool is_end() const override {
    return prev_->is_end() || result_idx_ >= limit_;
}
```

**子树耗尽 或 已输出满 LIMIT**，都算结束。

### 4.2 `beginTuple()`

```cpp
void beginTuple() override {
    prev_->beginTuple();
    result_idx_ = 0;
    if (limit_ == 0) return;                  // LIMIT 0 → 无输出
    if (!prev_->is_end() && plan_ != nullptr) plan_->rows_++;
}
```

打开子树，重置计数。`limit_==0` 直接返回（is_end 恒 true）。否则第一行输出时 `rows_++`。

### 4.3 `nextTuple()`

```cpp
void nextTuple() override {
    prev_->nextTuple();
    result_idx_++;
    if (!prev_->is_end() && plan_ != nullptr && result_idx_ < limit_) plan_->rows_++;
}
```

子树推进、计数加一、条件性 rows_++。**注意：LIMIT 截断发生在计数上，而不是真的停止推进子树**——`is_end()` 用 `result_idx_ >= limit_` 挡住上层调用，所以子树会在没被调用时停在原地。

### 4.4 `Next()`

```cpp
std::unique_ptr<RmRecord> Next() override {
    if (is_end()) return nullptr;
    auto rec = prev_->Next();
    if (rec == nullptr) return nullptr;
    if (is_sel_all_) return rec;              // SELECT * 快路径：直接透传
    auto &prev_cols = prev_->cols();
    auto projected = std::make_unique<RmRecord>(len_);
    size_t offset = 0;
    for (size_t idx : sel_idxs_) {
        const auto &col = prev_cols[idx];
        memcpy(projected->data + offset, rec->data + col.offset, col.len);
        offset += col.len;
    }
    return projected;
}
```

- `is_sel_all_`：直接返回子记录（不拷贝重组）。
- 否则：按 `sel_idxs_` 逐列 `memcpy` 到新 RmRecord。**源偏移用下层 `col.offset`，目标偏移从 0 递增**。
