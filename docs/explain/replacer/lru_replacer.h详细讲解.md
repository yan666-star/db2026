# `replacer` 目录详细讲解（replacer.h + lru_replacer.h + lru_replacer.cpp）

本讲解覆盖 replacer 目录下**需要关注的 3 个文件**，按"接口 → 成员变量 → 四个函数逐行"的粒度展开。

- 接口：[replacer.h](../../src/replacer/replacer.h:18)
- 类声明：[lru_replacer.h](../../src/replacer/lru_replacer.h:24)
- 实现：[lru_replacer.cpp](../../src/replacer/lru_replacer.cpp:13)

> ⚠️ **先看这个最重要的核对结论**：`lru_replacer.cpp` 的**实际代码**里 `victim()` 取的是 `LRUlist_.front()`（链表头），而 `unpin()` 是 `push_back`（加到链表尾）。所以**链表头 = 最早 unpin 的 = 最久未使用 = 该被淘汰**。这与头文件注释"首部表示最近被访问"是**矛盾的**（注释写反了），也和 `README.md` 里"victim 从 list 尾端取"的表述**不一致**。**以实际代码为准：victim 从 front 取**。

## 〇、Replacer 到底在做什么（一句话）

BufferPool 有固定数量的内存格子（frame）。磁盘页装进 frame。内存满时，Replacer 只回答一句话：

```text
"可以复用 frame 7"
```

**它不写磁盘、不读磁盘、不知道 frame 里装的是哪个 PageId**。写 dirty 页、删映射、读新页都是 BufferPoolManager 的事。这是它的边界。

## 一、`replacer.h`（抽象接口）

位置：[replacer.h](../../src/replacer/replacer.h:18)

```cpp
class Replacer {
   public:
    virtual ~Replacer() = default;
    virtual bool victim(frame_id_t *frame_id) = 0;  // 删并返回一个可淘汰 frame
    virtual void pin(frame_id_t frame_id) = 0;      // frame 正被使用，移出候选
    virtual void unpin(frame_id_t frame_id) = 0;    // frame 变为可淘汰，加入候选
    virtual size_t Size() = 0;                      // 当前候选数量
};
```

四个纯虚函数是**Replacer 策略必须实现的接口**。逐一解释：

| 接口 | 调用者 | 语义 | 常见实现要点 |
|---|---|---|---|
| `victim(frame_id_t*)` | BufferPoolManager::find_victim_page | **删除并返回**一个可淘汰 frame | 没有候选返回 false，`*frame_id` 不写 |
| `pin(frame_id)` | fetch_page / new_page / delete_page | frame 正被使用，**从候选集合移除** | 幂等：不在集合里就什么都不做 |
| `unpin(frame_id)` | unpin_page 里 pin_count 降到 0 时 | frame 变为**可淘汰**，加入候选 | 幂等：已在集合里不重复加入 |
| `Size()` | 管理/调试 | 当前候选数 | 不是缓冲池总大小 |

**最重要不变量**：Replacer 内的 frame 全部可淘汰，即对应 `Page.pin_count_ == 0`。BufferPoolManager 负责在 pin_count 从 1 变 0 时调 `unpin`，在重新固定时调 `pin`。

## 二、`lru_replacer.h`（LRU 类的成员变量逐个解释）

位置：[lru_replacer.h](../../src/replacer/lru_replacer.h:42)

```cpp
class LRUReplacer : public Replacer {
   public:
    explicit LRUReplacer(size_t num_pages);   // num_pages = 缓冲池容量
    ~LRUReplacer();
    bool victim(frame_id_t *frame_id);
    void pin(frame_id_t frame_id);
    void unpin(frame_id_t frame_id);
    size_t Size();
   private:
    std::mutex latch_;                                        // 互斥锁
    std::list<frame_id_t> LRUlist_;                           // 候选 frame 的先后次序
    std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> LRUhash_;  // frame -> 它在 list 中的位置
    size_t max_size_;   // 最大容量（与缓冲池容量相同）
};
```

| 成员 | 类型 | 作用 |
|---|---|---|
| `latch_` | std::mutex | 保护 list 和 hash 的并发访问。每个公共方法开头 `std::scoped_lock lock{latch_}` |
| `LRUlist_` | std::list\<frame_id_t> | 候选 frame 的顺序。**实际代码中：头 = 最早 unpin（最久未使用），尾 = 最近 unpin** |
| `LRUhash_` | unordered_map\<frame_id, list::iterator> | frame_id → 它在 list 里的迭代器，实现 O(1) 删除 |
| `max_size_` | size_t | 最大候选容量，构造时传入 = 缓冲池 `pool_size_` |

**为什么 list + hash 两件套**：list 提供 O(1) 头尾插入删除和顺序，hash 提供 O(1) 定位。没有 hash，`pin(frame)` 要在 list 里线性查找；没有 list，LRU 顺序无从保持。两者必须**一一对应**：list 里的每个元素，hash 里必有一条指向它的迭代器。

## 三、`lru_replacer.cpp`（四个函数逐行讲解）

### 3.1 构造函数

```cpp
LRUReplacer::LRUReplacer(size_t num_pages) { max_size_ = num_pages; }
```

只保存容量，list/hash 默认空。`max_size_` 其实没被用到（本实现没有容量检查），但保留它让调用方语义清晰。

### 3.2 `unpin(frame_id)` —— 加入候选

```cpp
void LRUReplacer::unpin(frame_id_t frame_id) {
    std::scoped_lock lock{latch_};
    if (LRUhash_.count(frame_id)) {   // 已在候选，幂等返回
        return;
    }
    LRUlist_.push_back(frame_id);                       // 加到链表尾
    LRUhash_[frame_id] = std::prev(LRUlist_.end());     // 记录它在新尾的位置
}
```

- 语义：**这个 frame 的 `Page.pin_count_` 刚降到 0**，可以参与淘汰了。
- `LRUhash_.count(frame_id)`：防止重复 unpin 把同一 frame 加进 list 两份。重复调用必须幂等。
- `push_back`：新候选放**链表尾**。
- `std::prev(LRUlist_.end())`：`end()` 是最后一个元素的**下一个**位置，`prev` 得到最后一个元素本身的迭代器，存入 hash。

### 3.3 `victim(frame_id_t*)` —— 淘汰一个

```cpp
bool LRUReplacer::victim(frame_id_t* frame_id) {
    std::scoped_lock lock{latch_};
    if (LRUlist_.empty()) {
        return false;
    }
    *frame_id = LRUlist_.front();    // 取链表头 = 最久未使用
    LRUhash_.erase(*frame_id);
    LRUlist_.pop_front();
    return true;
}
```

- 空列表返回 false（全部 frame 被 pin，没有可淘汰的）。
- **`LRUlist_.front()`**：链表头是最早 unpin 的，即"最久未被使用"，正是 LRU 该淘汰的。
- 删除顺序：先 `LRUhash_.erase`（用 frame_id 作 key），再 `LRUlist_.pop_front`。顺序无严格要求，但两边必须都删。

### 3.4 `pin(frame_id)` —— 移出候选

```cpp
void LRUReplacer::pin(frame_id_t frame_id) {
    std::scoped_lock lock{latch_};
    auto it = LRUhash_.find(frame_id);
    if (it != LRUhash_.end()) {
        LRUlist_.erase(it->second);   // 用 hash 里的迭代器从 list 删除
        LRUhash_.erase(it);           // 再删 hash 项
    }
}
```

- 语义：**frame 重新被 fetch 固定**，变为不可淘汰。
- 不在 hash 里（不在候选）则什么都不做——幂等。
- **顺序陷阱**：必须先 `LRUlist_.erase(it->second)` 再用 `it` 删 hash。因为 list erase 之后，`it->second` 迭代器就失效了；若先 `LRUhash_.erase(it)`，`it` 也被销毁，就没法拿到 list 迭代器了。

### 3.5 `Size()`

```cpp
size_t LRUReplacer::Size() { return LRUlist_.size(); }
```

返回候选数量（list 长度）。注意：**不是** `max_size_`。

## 四、用例子走一遍（按实际代码）

假设缓冲池 frame 有 1、2、3：

```text
unpin(1):  list=[1]        hash={1:it1}
unpin(2):  list=[1,2]      hash={1:it1, 2:it2}
victim():  *frame=1        ← 淘汰最早 unpin 的 1
           list=[2]        hash={2:it2}
unpin(1):  list=[2,1]      ← 1 重新加入候选，在尾部
pin(2):    list=[1]        hash={1:it1}     ← 2 被使用，移出候选
victim():  *frame=1        ← 淘汰 1
```

## 五、易错点总结

1. **victim 从 `front()` 取（实际代码）**，unpin 加到 `back()`。头文件注释"首部最近"是写反了，不要被误导。
2. list 与 hash 必须**同步增删**：unpin 两处都加，victim 两处都删，pin 两处都删。
3. 重复 `unpin` 必须幂等（`count` 检查），否则同一 frame 进 list 两份，victim 可能淘汰两次同一个 frame。
4. `pin` 的 erase 顺序：先 list 后 hash，避免迭代器失效。
5. Replacer 内只能有 `pin_count==0` 的 frame；BufferPool 负责在 1→0 时 unpin、重新固定时 pin。
6. `victim` 返回 false 表示全被 pin，BufferPool 不能强行淘汰。
7. 策略只返回 frame_id，不写 dirty、不删映射、不做磁盘 I/O。
