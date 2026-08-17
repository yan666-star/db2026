# `executor_extended_join.h` 详细讲解

源码入口：[executor_extended_join.h](../../src/execution/executor_extended_join.h:56)

> 注意：当前文件被 `#if 0` 包住，**尚未参与编译**。本讲解解释它的设计意图，并指出模板中的缺陷。

## 一、先理解：执行器是什么

数据库执行 SQL 时，不是一次性把整个结果集全部算完，而是组成一棵执行器树。例如：

```sql
SELECT a.id
FROM a SEMI JOIN b
ON a.id = b.a_id;
```

执行器树可以大致理解为：

```text
ProjectionExecutor
        │
ExtendedJoinExecutor
      /          \
SeqScan(a)     SeqScan(b)
```

其中：
- SeqScan(a)：一条一条提供 a 表记录；
- SeqScan(b)：一条一条提供 b 表记录；
- ExtendedJoinExecutor：组合左右记录并判断 ON 条件；
- ProjectionExecutor：从 JOIN 结果中选出最终需要的列。

ExtendedJoinExecutor 并不知道左右两边到底是不是表扫描。左边、右边也可能是其他 JOIN、Filter、Projection。因此它统一保存成：

```cpp
std::unique_ptr<AbstractExecutor> left_;
std::unique_ptr<AbstractExecutor> right_;
```

也就是说 `left_` = 左子树根执行器，`right_` = 右子树根执行器，它们不一定是单张表。

## 二、四个最重要的执行器接口

所有执行器都继承 [executor_abstract.h](../../src/execution/executor_abstract.h:45)。核心接口是：

```text
beginTuple();
is_end();
Next();
nextTuple();
```

典型调用方式：

```cpp
executor->beginTuple();
while (!executor->is_end()) {
    auto record = executor->Next();   // 使用当前记录
    executor->nextTuple();
}
```

它们的区别非常容易混淆。

### 1. beginTuple()
作用：初始化执行器，并定位到第一条可以输出的结果。它不只是"把游标设为 0"。对于 JOIN 来说，它还可能需要扫描很多左右记录，才能找到第一条匹配结果。

### 2. Next()
作用：取出当前已经准备好的记录。**注意：它不推进游标**。可以理解成 `return 当前答案;`，不是 `寻找下一条答案;`。

### 3. nextTuple()
作用：放弃当前答案，寻找下一条答案。在 ExtendedJoinExecutor 中，它会调用 `find_next()`。

### 4. is_end()
作用：判断是否已经没有更多结果。

## 三、一条记录在内存里是什么样的

这里的记录类型是 `RmRecord`，可以简单理解成一段连续字节。假设左表有两列：

```text
a.id   INT  (4 字节)
a.age  INT  (4 字节)
左记录: 偏移 0~3 = a.id，偏移 4~7 = a.age，left_len_ = 8
```

右表有 `b.a_id INT`、`b.score INT`，右记录也是 8 字节：`right_len_ = 8`。

JOIN 拼接以后：偏移 0~7 是左记录，偏移 8~15 是右记录，总长度 16。所以 `join_records()` 做的就是这种内存复制：

```cpp
memcpy(joined->data, left_rec.data, left_len_);
memcpy(joined->data + left_len_, right_rec.data, right_len_);
```

```text
left_rec
┌───────────────┐
│ a.id │ a.age  │
└───────────────┘

right_rec
┌─────────────────┐
│ b.a_id │ b.score│
└─────────────────┘

joined
┌───────────────┬─────────────────┐
│ a.id │ a.age  │ b.a_id │ b.score│
└───────────────┴─────────────────┘
0               left_len_
```

## 四、成员变量逐个解释

成员变量定义在：[executor_extended_join.h](../../src/execution/executor_extended_join.h:67)

### 1. `plan_`

```cpp
JoinPlan *plan_ = nullptr;
```

来源：构造函数参数（`ExtendedJoinExecutor(..., JoinPlan *plan = nullptr)`）。`JoinPlan` 是优化器生成的执行计划节点，告诉执行层：左子计划是什么、右子计划是什么、JOIN 条件是什么、JOIN 类型是什么。这个文件中 `plan_` 主要用于统计输出行数：`plan_->rows_++;`。它不负责真正执行 JOIN。`nullptr` 表示调用者没有提供计划指针，因此代码使用前必须判断 `if (plan_ != nullptr)`。

### 2. `join_type_`

```cpp
JoinType join_type_ = INNER_JOIN;
```

来源：构造函数参数 `join_type`。它决定执行器采取哪种 JOIN 语义：INNER/LEFT/RIGHT/FULL/ANTI，未来增加 SEMI_JOIN。例如 `if (join_type_ == ANTI_JOIN)` 表示只有当前执行的是 ANTI JOIN 时才进入这个分支。

### 3. `left_` 和 `right_`

```cpp
std::unique_ptr<AbstractExecutor> left_;
std::unique_ptr<AbstractExecutor> right_;
```

来源：Portal 根据 JoinPlan 递归创建出来的左右子执行器。构造函数收到它们后 `left_ = std::move(left); right_ = std::move(right);`。`unique_ptr` 表示这个执行器独占左右子执行器的所有权。`std::move(left)` 的含义是：把 `left` 参数所管理的执行器交给 `left_`，之后原来的 `left` 不再拥有该对象。

左右子执行器向 JOIN 提供以下能力：`left_->beginTuple()`、`left_->Next()`、`left_->nextTuple()`、`left_->is_end()`、`left_->tupleLen()`、`left_->cols()`。右边完全相同。

### 4. `left_len_`、`right_len_`、`len_`

```cpp
size_t len_ = 0;
size_t left_len_ = 0;
size_t right_len_ = 0;
```

- `left_len_`：`left_->tupleLen()`，左子执行器输出的一条记录有多少字节。
- `right_len_`：`right_->tupleLen()`，右子执行器输出的一条记录有多少字节。
- `len_`：当前 JOIN 执行器对外输出的一条记录有多少字节。
  - 普通 JOIN：`len_ = left_len_ + right_len_;`（输出左记录 + 右记录）。
  - ANTI/SEMI：`len_ = left_len_;`（只输出左记录）。

上层通过 `size_t tupleLen() const override { return len_; }` 询问这个执行器输出记录的长度。

### 5. `cols_`

```cpp
std::vector<ColMeta> cols_;
```

它是输出记录的"说明书"，即 schema。记录本身只是一串字节，只看字节无法知道哪几个字节是 a.id、哪几个字节是 a.name、数据类型是什么、列属于哪张表。`ColMeta` 保存的就是这些信息，大致包括：`tab_name`（表名）、`name`（列名）、`type`（列类型）、`len`（列字节长度）、`offset`（该列从记录第几个字节开始）。

例如：

```text
列       offset  len
a.id     0       4
a.age    4       4
b.a_id   8       4
b.score  12      4
```

构造 JOIN schema 时先复制左列 `cols_ = left_->cols();`，再取得右列 `auto right_cols = right_->cols();`。但右表原来的 offset 是相对右记录计算的，可能从 0 开始，拼到左记录后前面多了 `left_len_` 字节，所以要修改：

```cpp
for (auto &col : right_cols) {
    col.offset += left_len_;
}
```

然后追加进完整 schema：`cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());`。

### 6. `fed_conds_`

```cpp
std::vector<Condition> fed_conds_;
```

来源：构造函数参数 `conds`（`fed_conds_ = std::move(conds);`）。它保存 JOIN 的条件，例如 `ON a.id = b.a_id AND b.score > 60`，可能被表示成两条 Condition：

```text
条件1: lhs_col = a.id,   op = OP_EQ, rhs_col = b.a_id,  is_rhs_val = false
条件2: lhs_col = b.score, op = OP_GT, rhs_val = 60,     is_rhs_val = true
```

`is_rhs_val` 用于区别条件右边是列还是常量：`a.id = b.a_id` 右边是列所以 `is_rhs_val = false`；`b.score > 60` 右边是常量所以 `is_rhs_val = true`。

### 7. `current_rec_`

```cpp
std::unique_ptr<RmRecord> current_rec_;
```

这是"当前已经准备好、等待上层取走"的结果。例如 `find_next()` 找到一条匹配记录后调用 `emit(std::move(joined));`，`emit()` 把它放进 `current_rec_ = std::move(rec);`，之后上层调用 `Next()` 取得它的副本。所以：`find_next()` 负责找，`current_rec_` 负责存，`Next()` 负责给。

### 8. `is_end_`

```cpp
bool is_end_ = true;
```

表示执行器是否已经没有结果。

- 初始设置为 `true`，因为执行器还没有初始化。
- 找到结果时：`is_end_ = false;`
- 所有记录处理完时：`is_end_ = true;`，同时清空 `current_rec_.reset();`

上层用 `is_end()` 读取它。

### 9. `right_buffer_`

```cpp
std::vector<std::unique_ptr<RmRecord>> right_buffer_;
```

它保存右子树的全部记录。例如右表有 R1/R2/R3，物化后 `right_buffer_[0]=R1`、`[1]=R2`、`[2]=R3`。

为什么要全部存起来？因为嵌套循环 JOIN 要对每条左记录重新扫描整个右表（L1 对比 R1、R2、R3，L2 再对比一遍）。如果右子执行器只能向前走，那么每换一条左记录都需要重新执行右子树。这里采用简单做法：**第一次把右边全部读取到内存**，之后通过数组下标反复扫描。这就是"物化右表"。缺点是右表大时会占大量内存，但实现简单。

### 10. `right_matched_`

```cpp
std::vector<char> right_matched_;
```

它和 `right_buffer_` 一一对应。含义：`0` = 这条右记录还没有匹配任何左记录，`1` = 这条右记录至少匹配过一条左记录。它主要服务于 RIGHT JOIN / FULL JOIN，因为最后还要输出"完全没有匹配过的右记录"。例如 R1 没匹配、R2 匹配过、R3 没匹配，左表扫描结束后就输出"左侧空值 + R1"和"左侧空值 + R3"。

为什么用 `char` 而不是 `bool`？这里把一个字节当作布尔标记使用，便于直接赋值 `right_matched_[i] = 1;`。

### 11. `right_idx_`

```cpp
size_t right_idx_ = 0;
```

表示当前正在检查 `right_buffer_` 的第几条记录。找到一条普通 JOIN 结果后，函数会返回给上层。为了下一次能从后面继续，而不是从头重复，就把下标推进：`right_idx_++; emit(...); return;`。所以 `right_idx_` 还是一个"断点记录"。

### 12. `scanning_unmatched_right_`

```cpp
bool scanning_unmatched_right_ = false;
```

表示当前是否已经进入 RIGHT/FULL JOIN 的第二阶段。

- 第一阶段：逐条扫描左记录，将匹配过的右记录标成 1，输出正常匹配结果。
- 第二阶段：左表已经耗尽，扫描 `right_matched_`，输出所有标记为 0 的右记录。

当它是 `true` 时，`find_next()` 不再处理左表，而是专门输出未匹配右行。

### 13. `left_has_match_`

```cpp
bool left_has_match_ = false;
```

含义：当前正在处理的这条左记录，是否至少找到过一次匹配。例如当前左行 L1：与 R1 不匹配 → false；与 R2 匹配 → true；与 R3 不匹配 → 仍然是 true。**它不能在检查完一条右记录后立即清零**，因为它记录的是当前左行对整个右表的匹配情况。

这个变量主要决定：LEFT/FULL 是否需要输出"左+空右"；ANTI 是否需要输出左行；SEMI 是否已经满足"存在一条匹配"。换到下一条左行时必须重置：`left_has_match_ = false; right_idx_ = 0;`。

## 五、辅助函数逐个解释

### 1. `join_records()`（位置 [line 92](../../src/execution/executor_extended_join.h:92)）

```cpp
std::unique_ptr<RmRecord> join_records(const RmRecord &left_rec, const RmRecord &right_rec) {
    auto joined = std::make_unique<RmRecord>(len_);
    memcpy(joined->data, left_rec.data, left_len_);
    memcpy(joined->data + left_len_, right_rec.data, right_len_);
    return joined;
}
```

`left_rec` 来自 `left_->Next()`，`right_rec` 来自 `right_buffer_[right_idx_]`。作用：创建一条"左记录+右记录"的临时拼接记录。为什么必须先拼接？因为 ON 条件可能同时读取左右列（`a.id = b.a_id`），条件求值函数需要在同一条记录里根据 offset 找到 a.id 和 b.a_id。

**当前代码的问题**：当前函数用 `RmRecord(len_)` 分配空间。普通 JOIN 时 `len_ = left_len_ + right_len_` 没问题；但 ANTI/SEMI 输出只有左表，若 `len_ = left_len_`，后面仍然复制右记录就会越界。正确实现需要新增 `joined_len_ = left_len_ + right_len_;`，改用 `auto joined = std::make_unique<RmRecord>(joined_len_);`。

### 2. `pad_left_null()`（位置 [line 100](../../src/execution/executor_extended_join.h:100)）

```cpp
std::unique_ptr<RmRecord> pad_left_null(const RmRecord &right_rec) {
    auto joined = std::make_unique<RmRecord>(len_);
    memset(joined->data, 0, left_len_);
    memcpy(joined->data + left_len_, right_rec.data, right_len_);
    return joined;
}
```

用途：RIGHT JOIN 或 FULL JOIN 中，某条右记录没有匹配左记录。正常完整记录布局是"左半段|右半段"，现在没有左记录，所以 `memset(joined->data, 0, left_len_)` 把左半段填成 0，再复制右记录。注意：SQL 正常语义应该是 NULL，但注释说明当前数据库没有 NULL 位图，所以模板暂时用全 0 占位。**全 0 并不严格等价于 SQL NULL**。

### 3. `pad_right_null()`（位置 [line 107](../../src/execution/executor_extended_join.h:107)）

与上一个函数相反：

```cpp
memcpy(joined->data, left_rec.data, left_len_);
memset(joined->data + left_len_, 0, right_len_);
```

用途：LEFT/FULL JOIN 中，左记录没有匹配右记录，输出"左记录 | 全0右半段"。

### 4. `anti_left_only()`（位置 [line 115](../../src/execution/executor_extended_join.h:115)）

```cpp
std::unique_ptr<RmRecord> anti_left_only(const RmRecord &left_rec) {
    auto out = std::make_unique<RmRecord>(left_len_);
    memcpy(out->data, left_rec.data, left_len_);
    return out;
}
```

作用：只复制并返回左记录。ANTI JOIN 无匹配时调用它。SEMI JOIN 匹配时也需要完全相同的操作，所以更合理的名字是 `left_only()`。这个函数本身不判断匹配，只负责复制左记录。判断"什么时候调用它"是在 `find_next()` 中完成的。

### 5. `cond_ok()`（位置 [line 121](../../src/execution/executor_extended_join.h:121)）

```cpp
bool cond_ok(const RmRecord &joined) {
    return fed_conds_.empty() ||
           eval_conditions(joined, fed_conds_, cols_);
}
```

作用：判断当前左记录和右记录是否满足全部 JOIN 条件。`A || B` 表示：如果没有 JOIN 条件，直接认为匹配；否则计算全部条件。`eval_conditions()` 是 AND 语义（对每个条件，某个不成立就返回 false；全成立返回 true）。例如 `ON a.id = b.a_id AND b.score > 60` 必须两个条件都成立。

`cols_` 在这里有什么用？假设条件要读取 `b.score`，程序必须在 `cols_` 中找到 b.score 的 offset，再从 `joined.data + offset` 读取该列的字节。

**当前模板的问题**：若 ANTI/SEMI 的 `cols_` 只保留左表列，那么条件中的右表列无法找到。因此正确实现要有两套 schema：

```cpp
cols_       // 对外输出，SEMI/ANTI 只有左表
eval_cols_  // 内部条件判断，永远是左表+右表
```

`cond_ok()` 应使用 `eval_conditions(joined, fed_conds_, eval_cols_);`。

### 6. `materialize_right()`（位置 [line 126](../../src/execution/executor_extended_join.h:126)）

```cpp
void materialize_right() {
    right_buffer_.clear();
    right_matched_.clear();

    right_->beginTuple();
    while (!right_->is_end()) {
        auto rec = right_->Next();
        if (rec != nullptr) {
            right_buffer_.push_back(std::make_unique<RmRecord>(*rec));
            right_matched_.push_back(0);
        }
        right_->nextTuple();
    }
}
```

逐句解释：

- `right_buffer_.clear(); right_matched_.clear();`：执行器可能被重新打开，所以不能保留上一次的数据。
- `right_->beginTuple();`：让右子执行器定位第一条记录。
- `while (!right_->is_end())`：只要右子执行器没有耗尽，就继续。
- `auto rec = right_->Next();`：取得当前右记录（不推进）。
- `right_buffer_.push_back(std::make_unique<RmRecord>(*rec));`：**为什么复制？**因为下一次 `right_->nextTuple();` 后右子执行器的当前记录可能变化，缓冲区需要拥有独立副本。
- `right_matched_.push_back(0);`：每放入一条右记录，就为它创建一个"尚未匹配"的标记。
- `right_->nextTuple();`：处理下一条右记录。

### 7. `emit()`（位置 [line 140](../../src/execution/executor_extended_join.h:140)）

```cpp
void emit(std::unique_ptr<RmRecord> rec) {
    if (plan_ != nullptr) {
        plan_->rows_++;
    }
    current_rec_ = std::move(rec);
    is_end_ = false;
}
```

它不是"打印记录"，也不是立即发送给客户端。它的意思是：**我找到下一条结果了，把它放到 current_rec_，等待上层 Next() 获取**。作用有三步：计划统计行数加一；保存当前输出记录；标记执行器还没有结束。`find_next()` 每次调用 `emit()` 后通常立刻 return，因为一次只能准备一条结果。

## 六、最核心的 `find_next()`（位置 [line 157](../../src/execution/executor_extended_join.h:157)）

它不是"找一组结果"，而是：**从当前状态继续运行，直到找到一条可以输出的记录，或者确认所有结果都已耗尽**。

外层 `while (true)` 表示它可能跳过很多不满足条件的组合。但它不会无限输出，一旦找到一条就会 `emit(...); return;`。

### 阶段一：输出未匹配右记录

```cpp
if (scanning_unmatched_right_) {
```

只有 RIGHT/FULL JOIN 会进入。遍历 `while (right_idx_ < right_buffer_.size())`：如果某条右记录从未匹配（`!right_matched_[right_idx_]`），输出 `emit(pad_left_null(*right_buffer_[right_idx_]));`（全0左半段 + 当前右记录），然后 `right_idx_++; return;`。**为什么先 right_idx_++ 再返回？**因为当前记录已经输出，下次必须从下一条右记录继续。全部扫描完后 `current_rec_.reset(); is_end_ = true; return;`，表示 JOIN 彻底结束。

### 阶段二：检查左表是否耗尽

```cpp
if (left_->is_end()) {
```

如果是 RIGHT/FULL：`scanning_unmatched_right_ = true; right_idx_ = 0; continue;`，意思是左表处理完了，现在开始第二阶段，从头检查哪些右记录从未匹配。`continue` 会返回 `while(true)` 顶部，立刻进入前面的未匹配右行分支。其他 JOIN 不需要第二阶段，直接结束：`current_rec_.reset(); is_end_ = true; return;`。

### 阶段三：取得当前左记录

```cpp
auto left_rec = left_->Next();
```

这条记录来自左子执行器当前游标。如果意外返回空指针（`left_rec == nullptr`），则跳过当前左位置：`left_->nextTuple(); left_has_match_ = false; right_idx_ = 0; continue;`。这属于防御性处理。

### 阶段四：当前左行逐条对比右表

```cpp
while (right_idx_ < right_buffer_.size()) {
    auto joined = join_records(*left_rec, *right_buffer_[right_idx_]);
    if (cond_ok(*joined)) {
        left_has_match_ = true;
        right_matched_[right_idx_] = 1;
        // ... 按 join_type_ 分支
    }
}
```

如果匹配：`left_has_match_ = true`（当前左行至少匹配过一次）、`right_matched_[right_idx_] = 1`（当前右行至少匹配过一次）。

**ANTI 匹配时为什么不输出**：

```cpp
if (join_type_ == ANTI_JOIN) {
    right_idx_ = right_buffer_.size();
    break;
}
```

ANTI JOIN 要的是完全没有匹配的左行。当前已经找到一个匹配，因此这条左行必然不应该输出，后续右记录也不必再检查，于是直接把 `right_idx_` 设到右缓冲区末尾并 break 结束内层循环。离开内层循环后 `left_has_match_ == true`，不会进入"无匹配输出 ANTI 左行"的分支，最后推进到下一条左记录。

**普通 JOIN 匹配时为什么返回**：

```cpp
right_idx_++;
emit(std::move(joined));
return;
```

例如 L1 同时匹配 R1、R2。第一次 `right_idx_=0` 输出 L1+R1，`right_idx_` 变成 1，返回上层。下一次上层调用 `nextTuple()` 再次进入 find_next()：左游标仍然是 L1，`right_idx_=1`，继续检查 R2，输出 L1+R2。这正是普通 JOIN 一对多时输出多行的原因。

**SEMI 匹配时应该怎么做**：

SEMI 只输出左行一次，因此应增加：

```cpp
if (join_type_ == SEMI_JOIN) {
    auto result = left_only(*left_rec);
    left_->nextTuple();          // 先推进左游标！
    left_has_match_ = false;
    right_idx_ = 0;
    emit(std::move(result));
    return;
}
```

为什么马上执行 `left_->nextTuple();`？因为当前左行既然已经找到一个匹配，SEMI 的判断就完成了。如果不推进左游标，下一次还会处理同一条左行，可能因为另一条右记录再次输出，违反"只输出一次"。

### 阶段五：当前左行扫描完整个右表

内层循环结束后，`if (!left_has_match_)` 表示当前左行与所有右记录都不匹配：

- **LEFT/FULL**：`emit(pad_right_null(*left_rec));`（当前左记录 + 全0右半段）。输出前把左游标推进（当前左行已完成）：`left_->nextTuple(); left_has_match_ = false; right_idx_ = 0; return;`
- **ANTI**：`emit(anti_left_only(*left_rec));`（正好需要没有任何匹配的左行）。同样在返回前推进左游标并重置状态。
- **INNER/RIGHT/SEMI**：无匹配时不输出，直接落到下面 `left_->nextTuple(); left_has_match_ = false; right_idx_ = 0;`，然后外层 `while(true)` 继续处理下一条左记录。

## 七、构造函数做了什么（位置 [line 250](../../src/execution/executor_extended_join.h:250)）

构造函数不是开始执行 JOIN，它只保存配置并建立输出 schema。

参数来源对照：

| 参数 | 来源 | 用途 |
|---|---|---|
| `left` | Portal 根据左子 Plan 创建 | 提供左记录 |
| `right` | Portal 根据右子 Plan 创建 | 提供右记录 |
| `conds` | JoinPlan::conds_ | 判断左右记录是否匹配 |
| `join_type` | JoinPlan::type | 决定 INNER/LEFT/RIGHT 等语义 |
| `plan` | 当前 JoinPlan | 统计输出行数 |

构造函数先接管资源：`left_ = std::move(left); right_ = std::move(right);`；保存配置：`plan_ = plan; join_type_ = join_type; fed_conds_ = std::move(conds);`；询问左右输出宽度：`left_len_ = left_->tupleLen(); right_len_ = right_->tupleLen();`；最后建立输出长度和列信息。

**注意，构造函数执行完以后**：没有读取任何表记录、`right_buffer_` 仍为空、没有 JOIN 结果。真正执行要等 `beginTuple()`。

## 八、beginTuple() 为什么这么复杂（位置 [line 291](../../src/execution/executor_extended_join.h:291)）

作用是：重置状态，并找到第一条结果。

1. **清除状态**：`scanning_unmatched_right_ = false; left_has_match_ = false; right_idx_ = 0;`（从头执行）。
2. **物化右表**：`materialize_right();`（把右子树所有记录放进 right_buffer_，匹配标志全 0）。
3. **打开左表**：`left_->beginTuple();` 定位第一条左记录。
4. **左表为空**：RIGHT/FULL 仍可能有结果（需要输出右表全部未匹配记录）：`scanning_unmatched_right_ = true; right_idx_ = 0; find_next(); return;`。其他 JOIN（INNER/LEFT/ANTI/SEMI）空左表直接结束。
5. **右表为空**：

| 类型 | 右表为空时 |
|---|---|
| INNER | 空 |
| RIGHT | 空 |
| SEMI | 空 |
| LEFT | 输出所有左行，右侧填空 |
| FULL | 输出所有左行，右侧填空 |
| ANTI | 输出所有左行 |

因此增加 SEMI 后判断应为：

```cpp
if (join_type_ == INNER_JOIN ||
    join_type_ == RIGHT_JOIN ||
    join_type_ == SEMI_JOIN) {
    is_end_ = true;
    return;
}
```

其余类型调用 `find_next();` 寻找第一条结果。

## 九、nextTuple() 和 Next() 的差别

### nextTuple()

```cpp
void nextTuple() override {
    if (is_end_) return;
    find_next();
}
```

它负责从当前状态继续寻找下一条结果。它没有固定写 `left_->nextTuple();` 或 `right_idx_++;`，因为这些状态已经由 find_next() 在不同分支中维护。

### Next()

```cpp
std::unique_ptr<RmRecord> Next() override {
    if (is_end_ || current_rec_ == nullptr) return nullptr;
    return std::make_unique<RmRecord>(*current_rec_);
}
```

它只是返回 `current_rec_` 的副本。**为什么返回副本，而不是把 current_rec_ 移出去？**因为 Next() 的约定是不推进、不破坏当前状态。调用者理论上可以在推进前再次读取当前记录。

## 十、用一个完整例子跟踪变量

```text
左表：L1(id=1), L2(id=2)
右表：R1(a_id=1), R2(a_id=1), R3(a_id=3)
条件：L.id = R.a_id
物化后：right_buffer_ = [R1, R2, R3], right_matched_ = [0,0,0], right_idx_ = 0
```

**INNER JOIN**：
- L1-R1 匹配 → left_has_match_=true, right_matched_=[1,0,0], right_idx_=1，输出 L1+R1
- 下一次：L1-R2 匹配 → right_matched_=[1,1,0], right_idx_=2，输出 L1+R2
- 下一次：L1-R3 不匹配，右表扫描完成，推进左表到 L2，重置状态
- L2 无匹配，不输出。最终 INNER 输出两行。

**SEMI JOIN**：
- L1-R1 第一次匹配 → 立即只复制 L1，立即推进左表到 L2，right_idx_=0，输出 L1。不会再检查 L1-R2，所以 L1 只输出一次。
- L2 扫描全部右表无匹配，跳过。
- 最终 SEMI 输出一行：L1。

**ANTI JOIN**：
- L1-R1 匹配 → L1 不符合 ANTI，立即结束 L1 的右表扫描，推进到 L2。
- L2 与 R1、R2、R3 都不匹配 → left_has_match_=false，输出 L2。
- 最终 ANTI 输出：L2。

## 十一、可以这样记住整个文件

```text
left_ / right_         = 左右数据从哪里来
right_buffer_          = 右表所有记录的内存副本
right_idx_             = 当前检查右表第几条
left_has_match_        = 当前左行是否至少匹配一次
right_matched_         = 每条右行是否至少匹配一次
join_records()         = 临时拼成"左+右"，用于判断条件
cond_ok()              = 判断 ON 条件
pad_left_null/right    = 外连接缺一侧时补全0
left_only()            = SEMI/ANTI 只复制左行
find_next()            = 找到下一条应该输出的结果
emit()                 = 把找到的结果存进 current_rec_
Next()                 = 把 current_rec_ 的副本交给上层
nextTuple()            = 继续调用 find_next() 找下一条
beginTuple()           = 重置状态并找到第一条
```
