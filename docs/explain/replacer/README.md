# replacer 目录详解：LRU、FIFO、LFU/自定义 FRU

## 0. Replacer 淘汰的是 frame，不是直接删除页面

BufferPool 有固定数量内存格子，每个格子叫 frame。磁盘页装进 frame。内存满时 Replacer 只回答：

```text
“可以复用 frame 7”
```

BufferPool 再检查旧页 dirty、写回磁盘、删除旧映射、读入新页。Replacer 不做这些。

### 0.1 `victim(frame_id_t *frame_id)` 为什么用指针参数

函数需要返回两件事：

```text
bool：有没有候选
frame_id：候选是谁
```

bool 做普通返回值，frame_id 通过输出指针写入：

```cpp
frame_id_t victim_frame;
if (!replacer_->victim(&victim_frame)) {
    // 全部 frame 被 pin
}
```

传入 nullptr 时实现应按框架约定防御或 assert，现场最小实现至少不要解引用空指针。

### 0.2 `pin(frame_id)` 人话解释

“这个 frame 正有人用，请从候选名单移除。”它不负责 `Page.pin_count_++`，那个动作由 BufferPool 做。Replacer 的 pin 是同步候选状态。

### 0.3 `unpin(frame_id)` 人话解释

“这个 frame 的 Page.pin_count 已经变成0，现在可以加入候选。”只有 BufferPool 观察到计数从1到0时调用。重复调用必须幂等。

### 0.4 LRU 例子

候选顺序头部最近、尾部最老：

```text
unpin(1): [1]
unpin(2): [2,1]
pin(1):   [2]
unpin(1): [1,2]
victim(): 取2
```

`LRUhash_[1]` 保存 1 在 list 中的 iterator，使 pin(1) 不必遍历整个 list。

## 1. 归属

Replacer 只管理“哪些 frame 当前允许被淘汰，以及按什么顺序选 victim”。它不读取磁盘、不写 dirty 页、不管理 page_id。

| 文件 | 作用 |
|---|---|
| [replacer.h](../../src/replacer/replacer.h:17) | 稳定抽象接口 |
| [lru_replacer.h](../../src/replacer/lru_replacer.h:23) | LRU 成员与接口 |
| [lru_replacer.cpp](../../src/replacer/lru_replacer.cpp:1) | LRU 算法 |

## 2. 接口语义

| 函数 | 调用者 | 语义 |
|---|---|---|
| `victim(frame_id*)` | BufferPoolManager | 删除并返回一个可淘汰 frame |
| `pin(frame_id)` | fetch/new/delete 等 | frame 正在使用，必须从候选集合移除 |
| `unpin(frame_id)` | pin_count 降到 0 时 | frame 成为候选；重复 unpin 不重复插入 |
| `Size()` | 管理/检查 | 当前候选数，不是缓冲池总大小 |

最重要不变量：Replacer 内的 frame 全部可淘汰，即对应 Page.pin_count_==0。BufferPoolManager 负责在 pin_count 从 1 变 0 时调用 unpin，在重新固定时调用 pin。

## 3. 当前 LRU 变量

| 变量 | 所有权 | 作用 |
|---|---|---|
| `latch_` | LRUReplacer | 保护 list/hash |
| `LRUlist_` | LRUReplacer | 候选 frame 次序；当前约定头部最近、尾部最久 |
| `LRUhash_` | LRUReplacer | frame_id 到 list iterator，实现 O(1) 删除 |
| `max_size_` | LRUReplacer | 最大候选容量 |

典型操作：unpin 新候选放 list 前端；pin 根据 hash 找到 iterator 并 erase；victim 从 list 尾端取最久未使用并同步 erase hash。

## 4. 如果 LRU 改 FIFO

FIFO 的含义是按“第一次进入可淘汰队列”的先后淘汰，不因访问/再次状态变化更新顺序。保持 Replacer 接口不变，BufferPoolManager 通常无需修改。

可新建 `FIFOReplacer`，成员：

```cpp
std::mutex latch_;
std::list<frame_id_t> queue_;
std::unordered_map<frame_id_t,
    std::list<frame_id_t>::iterator> pos_;
size_t max_size_;
```

核心骨架：

```cpp
void FIFOReplacer::unpin(frame_id_t frame) {
    std::scoped_lock lock(latch_);
    if (pos_.count(frame)) return;
    queue_.push_back(frame);
    pos_[frame] = std::prev(queue_.end());
}

void FIFOReplacer::pin(frame_id_t frame) {
    std::scoped_lock lock(latch_);
    auto it = pos_.find(frame);
    if (it == pos_.end()) return;
    queue_.erase(it->second);
    pos_.erase(it);
}

bool FIFOReplacer::victim(frame_id_t *frame) {
    std::scoped_lock lock(latch_);
    if (queue_.empty()) return false;
    *frame = queue_.front();
    queue_.pop_front();
    pos_.erase(*frame);
    return true;
}
```

注意：在本接口下，pin 会将 frame 移出候选，之后再次 unpin 会重新入队。这是“重新成为可淘汰状态的时间”FIFO，而不是页面第一次载入时间。如果题目要求严格载入时间 FIFO，需要 BufferPoolManager 在载入时通知策略或保存 arrival sequence。

## 5. 如果题意实际是 LFU

LFU 淘汰访问次数最少的可淘汰 frame。需要定义访问次数何时增加。当前 Replacer 只在 pin/unpin 时收到状态变化，没有单独 `record_access`；最小方案把每次 `pin(frame)` 视为一次访问并累计频次。

成员骨架：

```cpp
struct Entry {
    size_t frequency;
    uint64_t sequence;
};

std::unordered_map<frame_id_t, Entry> history_;
std::unordered_set<frame_id_t> evictable_;
uint64_t clock_ = 0;
std::mutex latch_;
```

规则：

```text
pin(frame): history[frame].frequency++，从 evictable 移除
unpin(frame): 加入 evictable，记录/更新 sequence
victim: 在 evictable 中选 frequency 最小；并列选 sequence 最早
```

简单遍历 victim 是 O(N)，资格赛功能正确性通常足够；若要求效率，再用频次桶。

## 6. 如果题目明确叫 FRU

FRU 不是本框架已定义策略，必须先按赛题文字确认 F 表示什么。无论具体规则如何，都采用同一落地方式：

1. 保持 `Replacer` 四个接口。
2. 明确状态数据：时间、频次、队列或引用位。
3. pin 只让 frame 不可淘汰，并按题意记录访问。
4. unpin 只在首次变为可淘汰时加入候选。
5. victim 只能从候选集合选并同时删除策略状态。
6. Size 返回候选数。

若只是把类名从 LRU 改 FRU，不改变 victim 排序依据，不算实现新策略。

## 7. BufferPoolManager 的唯一接线点

[构造函数](../../src/storage/buffer_pool_manager.h:38) 中：

```cpp
replacer_ = new FIFOReplacer(pool_size_);
```

并在 CMakeLists 加新 cpp。更稳妥是根据 `REPLACER_TYPE` 用正确字符串比较选择类型。注意 `std::string::compare()` 返回 0 才表示相等，不能把非 0 当相等。

## 8. 替换策略题注意

1. frame_id 是内存槽，不是 page_id。
2. pin 状态页绝不能 victim。
3. list 与 map/set 每次同步修改。
4. 重复 unpin 不得造成同一 frame 多份。
5. victim 成功后 Size 必须减一。
6. 所有公共方法持 latch，避免并发破坏容器。
7. 策略不负责刷 dirty 页，BufferPoolManager 负责。

## 9. 当前 LRU 三个函数逐行逻辑

### `unpin(frame_id)`

含义不是“页面被访问”，而是该 frame 的 Page.pin_count 刚降到 0，可以参与淘汰。

```cpp
std::scoped_lock lock(latch_);
if (LRUhash_.count(frame_id) != 0) return;
LRUlist_.push_front(frame_id);
LRUhash_[frame_id] = LRUlist_.begin();
```

先查 hash 防止重复。list 存顺序，hash 存位置；二者必须一一对应。

### `pin(frame_id)`

页面重新被 fetch，变为不可淘汰：

```cpp
auto it = LRUhash_.find(frame_id);
if (it == LRUhash_.end()) return;
LRUlist_.erase(it->second);
LRUhash_.erase(it);
```

erase list 后 iterator 失效，所以先用 iterator 删除 list，再删 map；不能删除 map 后再访问 `it->second`。

### `victim(frame_id*)`

```cpp
if (LRUlist_.empty()) return false;
*frame_id = LRUlist_.back();
LRUlist_.pop_back();
LRUhash_.erase(*frame_id);
return true;
```

尾部是最早加入且未再被 pin 的 frame。成功后必须从两个容器同时移除。

## 10. 从 LRU 改 LFU：需要改哪些文件

建议新增而不是直接把 LRU 类改名：

```text
src/replacer/lfu_replacer.h
src/replacer/lfu_replacer.cpp
src/replacer/CMakeLists.txt
src/storage/buffer_pool_manager.h
```

头文件完整骨架：

```cpp
class LFUReplacer : public Replacer {
public:
    explicit LFUReplacer(size_t num_pages) : max_size_(num_pages) {}
    bool victim(frame_id_t *frame_id) override;
    void pin(frame_id_t frame_id) override;
    void unpin(frame_id_t frame_id) override;
    size_t Size() override;

private:
    struct Entry {
        size_t frequency = 0;
        uint64_t last_sequence = 0;
    };
    std::mutex latch_;
    std::unordered_map<frame_id_t, Entry> history_;
    std::unordered_set<frame_id_t> evictable_;
    uint64_t sequence_ = 0;
    size_t max_size_;
};
```

`history_` 保存所有见过 frame 的频次，即使当前被 pin 也保留；`evictable_` 只保存 pin_count=0 候选。若 pin 时把 history 也删除，下一次 unpin 频次会归零，退化成别的策略。

victim 最小 O(N) 实现：

```cpp
bool LFUReplacer::victim(frame_id_t *out) {
    std::scoped_lock lock(latch_);
    if (evictable_.empty()) return false;

    auto best = evictable_.begin();
    for (auto it = std::next(best); it != evictable_.end(); ++it) {
        const Entry &a = history_.at(*it);
        const Entry &b = history_.at(*best);
        if (a.frequency < b.frequency ||
            (a.frequency == b.frequency &&
             a.last_sequence < b.last_sequence)) {
            best = it;
        }
    }

    *out = *best;
    evictable_.erase(best);
    history_.erase(*out);
    return true;
}
```

淘汰后清 history 是因为 frame 随后会装入另一个 PageId；频次不能从旧页面继承给新页面。

pin/unpin：

```cpp
void LFUReplacer::pin(frame_id_t frame) {
    std::scoped_lock lock(latch_);
    Entry &entry = history_[frame];
    ++entry.frequency;
    entry.last_sequence = ++sequence_;
    evictable_.erase(frame);
}

void LFUReplacer::unpin(frame_id_t frame) {
    std::scoped_lock lock(latch_);
    if (history_.find(frame) == history_.end()) {
        history_[frame] = Entry{1, ++sequence_};
    }
    evictable_.insert(frame);
}
```

题目如果定义“每次 fetch 计一次访问”，pin 增频合理；如果定义“每次 unpin 计一次”，把增频移到 unpin，文档/代码必须只选一种语义。

## 11. 从 LRU 改 CLOCK

成员：

```cpp
std::vector<bool> evictable_;
std::vector<bool> reference_;
size_t hand_ = 0;
size_t size_ = 0;
```

`pin(frame)`：若 evictable 原为 true，设 false 并 size--。`unpin(frame)`：首次变候选设 evictable=true、reference=true、size++。victim 循环最多多圈：跳过不可淘汰；引用位 true 则清零；false 则选中并设不可淘汰。

CLOCK 数组下标就是 frame_id，所以构造时大小等于 pool_size。不要把 PageId.page_no 用作下标。

## 12. BufferPool 策略选择处的字符串细节

当前 [buffer_pool_manager.h](../../src/storage/buffer_pool_manager.h:45) 使用 `REPLACER_TYPE.compare("LRU")`。C++ `compare` 返回 0 才相等，安全写法：

```cpp
if (REPLACER_TYPE == "LRU") {
    replacer_ = new LRUReplacer(pool_size_);
} else if (REPLACER_TYPE == "LFU") {
    replacer_ = new LFUReplacer(pool_size_);
} else if (REPLACER_TYPE == "FIFO") {
    replacer_ = new FIFOReplacer(pool_size_);
} else {
    throw InternalError("Unknown replacer type");
}
```

如果题目只要求直接替换 LRU，可在构造中固定 new 新类；但保留类型分支更适合比较策略。

## 13. 自定义 FRU 的答题模板

先从题面抄出排序键：

```text
候选条件：pin_count==0
第一排序键：题面定义的 F 指标
第二排序键：并列时的时间/Frame ID
状态更新时机：fetch、pin、unpin 中哪一个
淘汰后状态：删除还是保留
```

再选择数据结构。数据量只是 buffer pool frame 数时，O(N) victim+map 状态最容易现场写对；不要为了 O(logN) 使用带可变 key 的 set，更新频次时还要先 erase 旧键，很容易状态重复。

## 14. 策略替换不应修改的内容

- BufferPool 的 dirty 写回。
- page_table PageId->frame 映射。
- Page.pin_count_ 的增减。
- DiskManager 分配页。
- record/index 的页格式。

策略只返回 frame_id。它甚至不应知道该 frame 当前装的是哪个 PageId。
