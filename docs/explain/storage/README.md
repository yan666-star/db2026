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

## 8. 注意

1. free_list 和 replacer 的 frame 不应重复。
2. page_table 必须与 pages_[frame].id_ 一致。
3. dirty 页淘汰前先写回。
4. pin_count>0 永不淘汰/删除。
5. 返回 Page* 后调用方必须最终 unpin。
6. 不把 frame_id 当 page_no。
7. 选 victim 后要先处理旧页，再覆盖 Page 对象。

## 9. `fetch_page()` 逐行状态表

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

## 10. `unpin_page()` dirty 合并

正确：

```cpp
page->is_dirty_ = page->is_dirty_ || is_dirty;
```

如果页面先被写操作 unpin(true)，之后另一个只读者 unpin(false)，直接赋值 false 会丢失脏标记，最终淘汰不写回。

pin_count 只有大于 0 才能减；只有恰好变 0 才进入 replacer。重复 unpin 会让同一 frame 重复候选或负计数。

## 11. 锁顺序

常见层次：BufferPool latch 保护映射，Page latch 保护内容，Replacer latch 保护策略。不要在 Replacer 回调 BufferPool，避免反向锁序。Replacer 的 victim 只返回 frame，实际页处理由 BufferPool 完成正是为了保持边界。
