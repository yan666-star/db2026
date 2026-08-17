# replacer 目录详解：LRU

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

传入 nullptr 时实现应按框架约定防御或 assert，至少不要解引用空指针。

### 0.2 `pin(frame_id)` 语义

“这个 frame 正有人用，请从候选名单移除。”它不负责 `Page.pin_count_++`，那个动作由 BufferPool 做。Replacer 的 pin 是同步候选状态。

### 0.3 `unpin(frame_id)` 语义

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

## 4. 替换策略注意事项

1. frame_id 是内存槽，不是 page_id。
2. pin 状态页绝不能 victim。
3. list 与 map/set 每次同步修改。
4. 重复 unpin 不得造成同一 frame 多份。
5. victim 成功后 Size 必须减一。
6. 所有公共方法持 latch，避免并发破坏容器。
7. 策略不负责刷 dirty 页，BufferPoolManager 负责。

## 5. 当前 LRU 三个函数逐行逻辑

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

## 6. BufferPool 策略选择处的字符串细节

当前 [buffer_pool_manager.h](../../src/storage/buffer_pool_manager.h:45) 使用 `REPLACER_TYPE.compare("LRU")`。C++ `compare` 返回 0 才相等。
