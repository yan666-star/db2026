# `executor_nestedloop_join.h` 详细讲解

源码入口：[executor_nestedloop_join.h](../../src/execution/executor_nestedloop_join.h:43)

这个文件是 **嵌套循环内连接执行器 `NestedLoopJoinExecutor`**。它只实现 **INNER JOIN**（含可选的索引嵌套循环 INLJ）。

## 一、它和 ExtendedJoinExecutor 的区别

| | NestedLoopJoinExecutor | ExtendedJoinExecutor |
|---|---|---|
| 语义 | 仅 INNER | LEFT/RIGHT/FULL/ANTI |
| 右表 | 实时扫描，不物化 | 物化到 right_buffer_ |
| 加速 | 支持 set_index_lookup（INLJ） | 无索引探测 |
| 状态 | is_end_ + current_rec_ | 更复杂的状态机 |

## 二、成员变量逐个解释

位置：[executor_nestedloop_join.h](../../src/execution/executor_nestedloop_join.h:44)

```cpp
JoinPlan *plan_ = nullptr;                    // 对应 JoinPlan；每输出一对匹配则 rows_++
std::unique_ptr<AbstractExecutor> left_;      // 外表（驱动表）
std::unique_ptr<AbstractExecutor> right_;     // 内表（被扫描 / 索引探测）
size_t len_;                                  // 输出元组长度 = 左长 + 右长
std::vector<ColMeta> cols_;                   // 输出 schema：左列 + 右列（右 offset 已平移）
std::vector<Condition> fed_conds_;            // 连接条件（在拼接后的元组上求值）
std::unique_ptr<RmRecord> current_rec_;       // 当前匹配结果
bool is_end_ = true;
```

逐个解释：

| 变量 | 类型 | 含义 |
|---|---|---|
| `plan_` | JoinPlan\* | 借用指针，`rows_` 统计 |
| `left_` | unique_ptr | 外表（驱动表），外层循环固定一行 |
| `right_` | unique_ptr | 内表，每个左行都要重开扫描 |
| `len_` | size_t | 输出记录长 = `left_->tupleLen() + right_->tupleLen()` |
| `cols_` | vector\<ColMeta> | 左列 + 右列，**右列 offset 全部加上左元组长度**（平移） |
| `fed_conds_` | vector\<Condition> | 连接条件，在拼接记录上求值 |
| `current_rec_` | unique_ptr | 当前匹配的拼接行 |
| `is_end_` | bool | 是否还有结果 |

## 三、`find_exact_col`（私有静态辅助）

```cpp
static const ColMeta *find_exact_col(const std::vector<ColMeta> &cols, const TabCol &target);
```

按 `(tab_name, col_name)` **精确**查找列元数据，找不到返回 nullptr（不抛异常）。与 AbstractExecutor::get_col 不同——get_col 会退化为只匹配列名、找不到会 throw。这里需要精确判定"条件左右列各自属于左表还是右表"，用精确版本更安全。

## 四、`prepare_index_lookup`（INLJ 加速）

位置：[executor_nestedloop_join.h](../../src/execution/executor_nestedloop_join.h:68)

```cpp
bool prepare_index_lookup() {
    auto left_rec = left_->Next();
    if (left_rec == nullptr) return false;
    const auto &left_cols = left_->cols();
    const auto &right_cols = right_->cols();
    for (const auto &cond : fed_conds_) {
        if (cond.is_rhs_val || cond.op != OP_EQ) continue;   // 只看列=列的等值条件
        // 判断 cond 的左右两端，哪一端属于左表、哪一端属于右表（可能顺序相反）
        // ... 找到外键列 outer_meta
        if (right_->set_index_lookup(*right_col, left_rec->data + outer_meta->offset,
                                     outer_meta->type, outer_meta->len)) {
            return true;   // 绑定成功：右子树 IndexScan 会用探测键收窄
        }
    }
    return false;
}
```

要点：
- 只处理"列 = 列"且 `OP_EQ` 的条件。
- 通过 `find_exact_col` 分别查左右表的 cols_，确定条件哪端是左列、哪端是右列（书写顺序可能反过来）。
- 把左表的**当前行的值**（`left_rec->data + offset`）作为探测键，调用 `right_->set_index_lookup`。
- 任意一个等值条件绑定成功就返回 true，否则退化为普通全扫右表。

## 五、`join_records`（物理拼接）

```cpp
std::unique_ptr<RmRecord> join_records(const RmRecord &left_rec, const RmRecord &right_rec) {
    auto joined = std::make_unique<RmRecord>(len_);
    memcpy(joined->data, left_rec.data, left_->tupleLen());
    memcpy(joined->data + left_->tupleLen(), right_rec.data, right_->tupleLen());
    return joined;
}
```

前半拷贝左记录，后半拷贝右记录。`len_ = 左长 + 右长`。拼接后 ON 条件就能在同一条记录里通过 offset 读取左右两端的列。

## 六、`find_match`（核心主循环）

位置：[executor_nestedloop_join.h](../../src/execution/executor_nestedloop_join.h:120)

```cpp
void find_match() {
    while (!left_->is_end()) {
        while (!right_->is_end()) {
            auto left_rec = left_->Next();
            auto right_rec = right_->Next();
            auto joined = join_records(*left_rec, *right_rec);
            if (fed_conds_.empty() || eval_conditions(*joined, fed_conds_, cols_)) {
                if (plan_ != nullptr) plan_->rows_++;
                current_rec_ = std::move(joined);
                is_end_ = false;
                return;                        // 找到一条就暂停，交给上层取走
            }
            right_->nextTuple();               // 当前右行不匹配，看下一条右行
        }
        left_->nextTuple();                    // 右表耗尽，换下一个左行
        if (left_->is_end()) break;
        prepare_index_lookup();                // 新左行 → 重新绑定索引探测键
        right_->beginTuple();                  // 重开右表扫描
    }
    current_rec_.reset();
    is_end_ = true;                            // 左右都尽 → 结束
}
```

**为什么是两个 while**：外层遍历左行，内层为当前左行遍历所有右行。每发现一个匹配就 `return`（暂停），上层 `Next()` 取走后，下一次 `nextTuple()` 从"当前 right 位置的下一条"继续。这就是 INNER JOIN 一对多时输出多行的机制。

## 七、构造函数

```cpp
NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right,
                       std::vector<Condition> conds, JoinPlan *plan = nullptr) {
    left_ = std::move(left);
    right_ = std::move(right);
    plan_ = plan;
    len_ = left_->tupleLen() + right_->tupleLen();
    cols_ = left_->cols();
    auto right_cols = right_->cols();
    for (auto &col : right_cols) {
        col.offset += left_->tupleLen();       // 右列 offset 平移
    }
    cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());  // 左+右
    fed_conds_ = std::move(conds);
}
```

**右列 offset 平移是 Join 的关键**：右表原来的 offset 是相对右记录算的（从 0 开始），拼到左记录后面后，每个右列前面多了 `left_->tupleLen()` 字节。不平移，条件求值读右列会读到左记录的错误位置。

## 八、四个迭代接口

```cpp
void beginTuple() override {
    left_->beginTuple();
    if (left_->is_end()) { is_end_ = true; return; }   // 左表空 → 内连接空
    prepare_index_lookup();
    right_->beginTuple();
    find_match();
}

void nextTuple() override {
    if (is_end_) return;
    right_->nextTuple();     // 内层右表前进一步（可能跨到下一左行）
    find_match();
}

std::unique_ptr<RmRecord> Next() override {
    if (is_end_ || current_rec_ == nullptr) return nullptr;
    return std::make_unique<RmRecord>(*current_rec_);   // 副本
}
```

`beginTuple`：左表空直接结束（INNER 空左表结果为空）。否则绑定索引、打开右表、定位首个匹配。
`nextTuple`：只推进右表（不手动推左表），`find_match` 内部在右表耗尽时自动换左行。**这就是"断点续传"**：状态都收敛在 find_match 里。

## 九、易错点总结

1. `right_->beginTuple()` 必须在每个新左行之前调用，否则右表停在上一左行的位置。
2. `Next()` 返回副本，不能 move 出 current_rec_。
3. 右列 offset 平移只做一次（构造时），不要每次求值都平。
4. `is_end_` 与 `current_rec_` 的置空/置 false 要同步（find_match 结尾 reset + is_end_=true）。
5. 空条件（`fed_conds_.empty()`）意味着"所有组合匹配"——CROSS JOIN 依赖这一点，不要改成 false。
6. 表为空时 beginTuple 立即结束，这由 `left_->is_end()` 天然满足。
