# storage 目录详解：BufferPool 与磁盘页

## 0. page_id、frame_id、Rid 不要混

| 名称 | 所在位置 | 例子 |
|---|---|---|
| `page_id` | 磁盘文件中的页号 | 文件3的第10页 |
| `frame_id` | BufferPool 内存数组下标 | pages_[7] |
| `Rid` | 表记录位置 | page_no=10,slot_no=5 |

page_table_ 做 PageId 到 frame_id 映射。一页被淘汰后，其 page_id 仍在磁盘，但原 frame 可装另一页。

### 0.1 `fetch_page(PageId page_id)` 参数和返回

输入包含 fd+page_no，返回 `Page*` 指向缓冲池 frame。返回时 pin_count 已加1，调用者使用完必须 unpin。

为什么返回裸指针：Page 由 BufferPool 的 `pages_` 数组拥有，调用者只是借用。不能 delete。

### 0.2 `unpin_page(page_id,is_dirty)`

| 参数 | 含义 |
|---|---|
| `page_id` | 要释放一次使用计数的页面 |
| `is_dirty` | 本次使用是否改过内容 |
| 返回 bool | 页面存在且成功减计数 |

`is_dirty=false` 不代表页面整体干净，只代表本次没修改，因此必须与旧 dirty 做 OR。

### 0.3 `new_page(PageId *page_id)`

输出参数接收新磁盘页 ID，返回承载它的 Page*。同时需要：DiskManager 分配 page_no、找 frame、初始化数据和映射、pin_count=1。

### 0.4 `delete_page(page_id)`

如果 pin_count>0 表示还有人持有 Page*，不能删除。成功后从 page_table 和 replacer 移除，frame 清空进入 free_list，并通知 DiskManager 回收磁盘页。

### 0.5 `find_victim_page(frame_id*)`

先用 free_list，因为空 frame 无旧数据；free_list 空才调用 Replacer。这个函数只选择 frame，旧 dirty 页写回通常由后续 update_page/fetch/new 逻辑完成。

## 1. 归属

storage 管理磁盘文件页与内存缓冲帧。Replacer 只决定可淘汰 frame，BufferPoolManager 完成实际写回、映射替换和读页。

| 文件 | 作用 |
|---|---|
| [page.h](../../src/storage/page.h:1) | Page/PageId |
| [disk_manager.h](../../src/storage/disk_manager.h:1) | 文件与页 I/O |
| [buffer_pool_manager.h](../../src/storage/buffer_pool_manager.h:28) | 缓冲池成员/接口 |
| [buffer_pool_manager.cpp](../../src/storage/buffer_pool_manager.cpp:28) | fetch/new/unpin/delete/flush |

## 2. Page 变量

| 变量 | 作用 |
|---|---|
| `id_` | 当前 frame 装载的 PageId(fd,page_no) |
| `data_` | PAGE_SIZE 字节数据 |
| `pin_count_` | 当前使用者数量，>0 不可淘汰 |
| `is_dirty_` | 内存内容是否比磁盘新 |
| `rwlatch_` | 页内容并发读写保护 |

Page 是 frame 中反复复用的对象。被淘汰后同一个 `pages_[frame_id]` 会改装另一个 PageId。

## 3. BufferPoolManager 成员

| 变量 | 所有权/作用 |
|---|---|
| `pool_size_` | frame 总数 |
| `pages_` | new[] 的 Page 数组，析构释放 |
| `page_table_` | PageId -> frame_id |
| `free_list_` | 从未占用/已释放 frame |
| `disk_manager_` | 借用，实际读写磁盘 |
| `replacer_` | 拥有，析构 delete |
| `latch_` | 保护页表、free list、pin/dirty 状态 |

## 4. find_victim_page

优先从 `free_list_` 取 frame；没有空闲 frame 时调用 `replacer_->victim(frame_id)`。Replacer 返回 false 表示全部页面仍被 pin，不能强行淘汰。

## 5. fetch_page

### 命中

从 page_table 找 frame，`pin_count_++`，调用 replacer.pin 移出候选，返回 `&pages_[frame]`。

### 未命中

选 victim；若旧页 dirty 先写磁盘；删除旧 PageId 映射；从磁盘读新页；设置 id/pin_count=1/dirty=false；建立新映射。

关键局部变量：`it` 是 page_table iterator；`frame_id` 是目标内存帧；`page` 指向 pages_ 内对象；`old_page_id` 用于刷旧页和 erase。

## 6. unpin_page

检查页面存在且 pin_count>0，然后减一；`is_dirty_ = is_dirty_ || 参数is_dirty`，不能用 false 覆盖已有 true。只有从 1 降为 0 时调用 replacer.unpin。

## 7. new_page/delete_page/flush

- new_page：选 frame，刷旧页，向 DiskManager 分配 page_no，初始化 Page，pin=1。
- delete_page：若 pin_count>0 拒绝；从 replacer 移除、删页表、清 Page、frame 回 free_list、deallocate 磁盘页。
- flush_page：页面存在时写磁盘并清 dirty。
- flush_all_pages：遍历属于指定 fd 的页。

## 8. 从 LRU 换 FIFO/LFU/自定义 FRU

BufferPoolManager 不应包含具体排序算法。修改路径：

```text
新增 Replacer 子类
 -> 保持 victim/pin/unpin/Size
 -> CMake 加实现文件
 -> 构造函数 new 新策略
```

BufferPoolManager 中这些调用不变：

```cpp
replacer_->pin(frame_id);    // 页面被使用
replacer_->unpin(frame_id);  // pin_count 变 0
replacer_->victim(&frame_id);// 需要空帧
```

具体 FIFO/LFU/FRU 状态设计见 [README.md](../../src/replacer/README.md:1)。

## 9. 类似现场改动

### 改为 CLOCK

Replacer 保存 frame 数组/指针和 reference bit。unpin 设置可淘汰；pin 清除可淘汰；victim 循环：遇引用位 1 清零并跳过，遇 0 选中。BufferPool 无需理解时钟指针。

### 增加预取

新增 prefetch_page 可将页载入但 pin_count 保持 0，并立即放入 replacer；不要复用 fetch_page 后忘记 unpin。

### 改 dirty 写回策略

必须保持淘汰 dirty 页前写回。后台刷页可以提前清 dirty，但需要页 latch 和 BufferPool latch 顺序明确。

### 增加统计

命中/未命中计数可放 BufferPoolManager；访问频次若仅属于 LFU 策略则放 Replacer，避免两份状态不同步。

## 10. 注意

1. free_list 和 replacer 的 frame 不应重复。
2. page_table 必须与 pages_[frame].id_ 一致。
3. dirty 页淘汰前先写回。
4. pin_count>0 永不淘汰/删除。
5. 返回 Page* 后调用方必须最终 unpin。
6. 不把 frame_id 当 page_no。
7. 选 victim 后要先处理旧页，再覆盖 Page 对象。

## 11. `fetch_page()` 逐行状态表

### 命中前

```text
page_table_[page_id] = frame
pages_[frame].id_ = page_id
```

命中动作：

```cpp
Page *page = &pages_[frame];
page->pin_count_++;
replacer_->pin(frame);
return page;
```

即使 page 已经不在 replacer，pin 应幂等。

### 未命中选择 victim 后

假设 frame 原装 old_id：

```text
若 dirty：write_page(old_id, data)
page_table.erase(old_id)
read_page(new_id, data)
page.id_=new_id
page.pin_count_=1
page.is_dirty_=false
page_table[new_id]=frame
replacer.pin(frame)
```

次序关键：旧 dirty 数据必须在 data 被 read_page 覆盖之前写回。

## 12. `unpin_page()` dirty 合并

正确：

```cpp
page->is_dirty_ = page->is_dirty_ || is_dirty;
```

如果页面先被写操作 unpin(true)，之后另一个只读者 unpin(false)，直接赋值 false 会丢失脏标记，最终淘汰不写回。

pin_count 只有大于 0 才能减；只有恰好变 0 才进入 replacer。重复 unpin 会让同一 frame 重复候选或负计数。

## 13. 专题：真正把 LRU 替换为 LFU

Storage 层改动只有接线：

1. include `replacer/lfu_replacer.h`。
2. 构造函数创建 LFUReplacer。
3. CMake 编译 lfu cpp。
4. `find_victim_page/fetch_page/unpin_page/delete_page` 不改接口调用。

原因：这些函数只关心 frame 是否候选，不关心 LRU/LFU 排序。若你在 fetch_page 里直接维护 frequency map，算法状态就被拆到 BufferPool 和 LFU 两处，容易不同步。

LFU 详细成员和实现见 [README.md](../../src/replacer/README.md:1)。

## 14. 专题：增加 BufferPool 命中率

成员：

```cpp
uint64_t fetch_count_ = 0;
uint64_t hit_count_ = 0;
```

每次 fetch 开始 fetch_count++；page_table 命中时 hit_count++。需要并发安全，可在现有 latch 保护区更新或使用 atomic。命中率只观察，不影响替换策略。

## 15. 专题：调整缓冲池容量

`pool_size_`、pages_ 数组、free_list 初始 frame、Replacer 最大容量必须一致。只改全局 `BUFFER_POOL_SIZE` 而某个 Replacer 数组仍用旧常量会越界。

frame_id 合法范围始终 `[0,pool_size_)`。策略收到非法 frame 应按框架约定拒绝或 assert。

## 16. 专题：增加 PageGuard

PageGuard 用 RAII 在析构时自动 unpin，保存：

```cpp
BufferPoolManager *bpm_;
Page *page_;
bool dirty_;
```

移动构造转移责任并将来源 page_=nullptr；禁止复制，否则两个 guard 会 unpin 两次。写 guard 在修改后把 dirty_=true。这个改动会影响 record/index 大量 fetch/unpin 调用，不是资格赛最小题时不要全仓重构。

## 17. 专题：后台刷脏页

后台线程选 dirty 且可安全读的页写盘，但写盘期间要防止同一页继续修改导致清错 dirty。简单方式持页写 latch，复制/写出后确认版本未变再清标志。BufferPool 全局 latch 不宜跨慢磁盘 I/O 长期持有。

## 18. 锁顺序

常见层次：BufferPool latch 保护映射，Page latch 保护内容，Replacer latch 保护策略。不要在 Replacer 回调 BufferPool，避免反向锁序。Replacer 的 victim 只返回 frame，实际页处理由 BufferPool 完成正是为了保持边界。

## 19. BufferPool 修改检查表

```text
新 frame 来源是 free_list 还是 victim
旧 dirty 页是否先写回
旧 page_table 映射是否删除
新 PageId 与 frame 映射是否一致
pin_count 是否正确
replacer 中是否只有 pin_count=0 的 frame
异常/失败时 frame 是否丢失
delete 后 frame 是否只进入 free_list，不同时留在 replacer
```
