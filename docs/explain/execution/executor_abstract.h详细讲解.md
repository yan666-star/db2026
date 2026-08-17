# `executor_abstract.h` 详细讲解

源码入口：[executor_abstract.h](../../src/execution/executor_abstract.h:45)

这个文件定义 **火山模型（Volcano）执行器抽象基类 `AbstractExecutor`**。**所有执行器都继承它**。理解它的迭代协议，是理解任何执行器的前提。

## 一、火山模型迭代协议（最重要）

SELECT 侧的典型循环（见 QlManager::select_from）：

```cpp
executor->beginTuple();

while (!executor->is_end()) {
    auto record = executor->Next();   // 取当前记录（不推进）
    // 使用 record
    executor->nextTuple();            // 推进到下一条
}
```

**四个接口的区别极易混淆**：

| 接口 | 作用 | 类比 |
|---|---|---|
| `beginTuple()` | 初始化执行器，并**定位到第一条**结果 | 打开文件并读到第一行 |
| `is_end()` | 是否已经没更多结果 | 是否到文件末尾 |
| `Next()` | **取走当前已经准备好的**记录，**不推进游标** | `return 当前答案;` 而不是 `寻找下一条;` |
| `nextTuple()` | 放弃当前答案，**寻找下一条**答案 | 跳到下一行 |

## 二、成员变量逐个解释

位置：[executor_abstract.h](../../src/execution/executor_abstract.h:45)

### 2.1 `_abstract_rid`

```cpp
Rid _abstract_rid;
```

部分算子缓存当前记录定位（Scan/Update 等使用）。很多中间算子自己不用它，但基类要求提供 `rid()`，就返回这个空成员。它类型是 `Rid`（page_no + slot_no），是记录在磁盘上的物理位置。

### 2.2 `context_`

```cpp
Context *context_;
```

执行上下文：事务、锁、日志、输出缓冲等。**借用指针**，执行器不拥有它、不负责释放。Scan 的可见性判断（MVCC）和可串行化读集合登记都靠它。

## 三、虚接口逐个解释

### 3.1 `tupleLen()`

```cpp
virtual size_t tupleLen() const { return 0; };
```

**本执行器输出的一条记录有多少字节**。Join 的父执行器、记录拼接都靠它。基类返回 0（不安全），子类必须覆盖。

### 3.2 `cols()`

```cpp
virtual const std::vector<ColMeta> &cols() const {
    std::vector<ColMeta> *_cols = nullptr;
    return *_cols;   // 解引用空指针！基类默认实现不安全
};
```

**输出记录的"说明书"（schema）**：每列的 `tab_name/name/type/len/offset`。只看字节无法知道哪几个字节是哪一列，必须靠 cols_。**基类默认解引用空指针，子类必须覆盖**，否则 UB。

### 3.3 `getType()` / `rows()`

```cpp
virtual std::string getType() { return "AbstractExecutor"; };
virtual size_t rows() const { return 0; }
```

`getType` 用于调试/EXPLAIN 显示算子名；`rows()` 供 EXPLAIN ANALYZE 与 `Plan::rows_` 对齐。

### 3.4 `beginTuple()` / `nextTuple()` / `is_end()`

```cpp
virtual void beginTuple(){};
virtual void nextTuple(){};
virtual bool is_end() const { return true; };
```

- `beginTuple()`：初始化并定位第一条。它**不只是把游标设为 0**，对于 Join 可能还要扫很多左右记录才能找到第一条匹配。
- `nextTuple()`：从当前状态找下一条。耗尽后 `is_end()` 应为 true。
- `is_end()`：默认 true（空执行器）。

### 3.5 `rid()` / `Next()`

```cpp
virtual Rid &rid() = 0;
virtual std::unique_ptr<RmRecord> Next() = 0;
```

两个纯虚函数（没有默认实现）。`rid()` 返回当前记录物理位置（UPDATE/DELETE 需要）；`Next()` 返回当前记录的**副本**，不推进游标。**DML 路径里 Next() 常作为"执行写操作"的入口**，副作用在实现内部完成。

### 3.6 `get_col_offset(target)`

```cpp
virtual ColMeta get_col_offset(const TabCol &target) { return ColMeta(); };
```

在本算子输出 schema 中按 TabCol 查找列元信息。用于条件求值、投影列定位。基类返回空 ColMeta（不覆盖也基本可用，因为上层多用 get_col）。

### 3.7 `set_index_lookup(target, data, type, len)`

```cpp
virtual bool set_index_lookup(const TabCol &target, const char *data, ColType type, int len) {
    return false;
}
```

可选接口：为**索引嵌套循环（INLJ）**设置等值探测键。基类默认 false（不支持）。NestedLoopJoinExecutor 会调用它把左表等值键传给右子树 IndexScan；IndexScanExecutor 实现它收窄扫描范围；Filter/Projection 透传给子树。

## 四、`get_col` 辅助函数

```cpp
std::vector<ColMeta>::const_iterator get_col(const std::vector<ColMeta> &rec_cols, const TabCol &target) {
    // 第一轮：精确匹配「表名+列名」
    // 第二轮：退化为仅匹配列名
    // 找不到：抛 ColumnNotFoundError
}
```

在列元数据列表中定位目标列。**两轮查找**：先 `(tab_name, col_name)`，再只看 `col_name`。后者让"无表前缀"的列也能匹配（在 schema 无歧义时）。ProjectionExecutor 构造时用它的返回值 `pos - rec_cols.begin()` 算下标。

## 五、新增执行器必须遵守的约定

| 接口 | 必须保证 |
|---|---|
| `beginTuple()` | 可重复调用并重置状态；定位首条 |
| `nextTuple()` | 恰好推进一次；不返回数据 |
| `Next()` | 不推进；返回独立可用副本 |
| `is_end()` | 与"当前记录是否存在"一致 |
| `cols()` / `tupleLen()` | 与 Next 返回的记录布局一致 |

最常见的状态机模板（管道算子）：

```cpp
void beginTuple() {
    prev_->beginTuple();
    find_next_valid();          // 推进到第一条合法结果
}

void nextTuple() {
    if (is_end_) return;
    prev_->nextTuple();
    find_next_valid();          // 推进到下一条
}
```

## 六、易错点总结

1. **基类 `cols()` 默认解引用空指针**，任何新执行器都必须覆盖 cols()，否则 UB。
2. `Next()` 必须返回**副本**（`make_unique<RmRecord>(*current_rec_)`），不能把 `current_rec_` move 出去，否则下次调用拿不到。
3. `plan_`（Plan 裸指针）是**借用**，执行器析构时不能 delete，否则 shared_ptr 后续会二次释放。
4. `nextTuple()` 与 `Next()` 是不同动作：Next 读、nextTuple 推进。命名很像，别搞混。
5. `context_`、`sm_manager_` 都是借用指针，不负责释放。
