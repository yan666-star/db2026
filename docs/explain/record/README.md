# record 目录详解

## 0. 一张表为什么需要页、槽和 Rid

一张表文件很大，磁盘按页读。每页放很多固定长度记录，每条记录占一个槽。

```text
Rid{page_no=3,slot_no=5}
```

表示文件第3个数据页的第5个槽。它是“地址”，RmRecord 是地址处的“内容”。

### 0.1 `RmFileHandle::get_record(rid,context)` 参数

| 参数 | 含义 |
|---|---|
| `rid` | 要读取的页号+槽号 |
| `context` | 事务可见性、锁等环境 |
| 返回值 | 独立 RmRecord 副本或不可见/不存在 |

函数要 fetch 页面、检查 rid 范围和 bitmap、复制 slot 字节、unpin 页面。返回副本后即使 frame 被替换，记录仍有效。

### 0.2 `insert_record(buf,context)` 参数

`buf` 指向恰好 `file_hdr_.record_size` 字节；record 层不会检查其中第0到3字节是不是合法 INT。`context` 用于锁/事务路径。返回 Rid 交给索引和写集。

### 0.3 `delete_record(rid,context)` 与 `update_record`

delete 清占用位，不需要列信息。update 保持 Rid 不变，直接用新 buf 覆盖同一槽。上层必须在调用前保存旧记录并维护索引；record 层不知道哪些字节属于索引键。

### 0.4 为什么 `RmRecord` 需要深拷贝

页被 unpin 后，原 data 地址可能装载别的页。事务 WriteRecord 要一直保存旧行直到 commit/abort，因此必须复制字节，而不能保存 Page.data 内指针。

## 1. 归属

record 管理定长记录文件：页内 bitmap、槽位、插入/读取/更新/删除以及全表扫描。它不理解列名，只认识字节数组与 Rid。

| 文件 | 作用 |
|---|---|
| [rm_defs.h](../../src/record/rm_defs.h:21) | 文件头、页头、RmRecord |
| [rm_file_handle.h](../../src/record/rm_file_handle.h:30) | 页句柄、文件句柄接口 |
| [rm_file_handle.cpp](../../src/record/rm_file_handle.cpp:1) | 记录操作实现 |
| [rm_scan.h](../../src/record/rm_scan.h:17) | 全表扫描器 |
| [rm_manager.h](../../src/record/rm_manager.h:20) | 创建/打开/关闭表文件 |
| [bitmap.h](../../src/record/bitmap.h:1) | 槽位位图 |

`rm.h` 为聚合 include；CMakeLists 编译 rm_file_handle.cpp、rm_scan.cpp。新增独立实现文件要加入构建目标。

## 2. 物理布局变量

### RmFileHdr

| 变量 | 作用 |
|---|---|
| `record_size` | 每条定长记录字节数 |
| `num_pages` | 文件已分配页数 |
| `num_records_per_page` | 每页槽数 |
| `first_free_page_no` | 空闲页链表头 |
| `bitmap_size` | 每页 bitmap 字节数 |

### RmPageHdr

`next_free_page_no` 串联仍有空槽的页；`num_records` 记录当前占用槽数。

### Rid

`page_no` 定位数据页，`slot_no` 定位页内槽。它与 page_id/frame_id 不同，索引叶子和事务写集都保存 Rid。

## 3. RmRecord 所有权

| 变量 | 作用 |
|---|---|
| `data` | 连续记录字节 |
| `size` | 长度 |
| `allocated_` | 当前对象是否负责 delete[] data |

拷贝构造做深拷贝；移动构造转移指针并清空来源。返回 `unique_ptr<RmRecord>` 时通常拥有独立副本，不应引用已 unpin 页中的裸地址。

## 4. RmPageHandle/RmFileHandle

RmPageHandle 借用 Page，计算 page_hdr、bitmap、slots 地址。RmFileHandle 保存文件 fd、file_hdr_、DiskManager/BufferPoolManager 指针以及插入锁。

关键接口：

| 函数 | 作用 |
|---|---|
| `get_record` | 按 Rid 读记录 |
| `insert_record` | 找空闲槽并写入 |
| `insert_record(rid,buf)` | 指定位置恢复记录，回滚使用 |
| `delete_record` | 清 bitmap 并维护空闲链 |
| `update_record` | 原槽覆盖 |
| `create_new_page_handle` | 新分配数据页 |
| `release_page_handle` | unpin 并处理页状态 |
| `all_record_slots` | 枚举槽，MVCC 路径使用 |

## 5. 插入关键变量

| 变量 | 生命周期 | 作用 |
|---|---|---|
| `page_handle` | 一次操作 | 当前可插入页及页内布局 |
| `slot_no` | 一次插入 | bitmap 中找到的空位 |
| `rid` | 返回值 | 新记录位置 |
| `insert_latch_` | RmFileHandle 生命周期 | 串行保护空闲页链与槽分配 |
| `first_free_page_no` | 文件头 | 快速找到有空槽页 |

插入页变满时要从空闲链移除；删除使满页重新有空位时要加入链。bitmap、num_records、空闲链三者必须一致。

## 6. RmScan

扫描器保存文件句柄和当前 Rid。`next()` 从当前槽后找下一个 bitmap=1 的槽，跨页继续；`is_end()` 在超过最后页时为真。

## 7. 类似现场改动

### 增加批量读取

保持单条 get_record 不变，新接口按同页 Rid 分组，只 fetch 一次 Page，复制多个 RmRecord 后统一 unpin。不能把 Page 内裸指针返回到 unpin 之后。

### 改每页布局

任何 bitmap/page header 大小变化都要同步 `num_records_per_page` 计算、槽起始地址和已有文件格式。资格赛通常不宜改磁盘格式，优先新增逻辑而非重排。

### 增加记录标记

若标记要永久存储，必须决定放在 tuple bytes、页外元数据还是 MVCC 管理器；直接在 RmRecord C++ 对象加字段不会自动写入磁盘。

### 等值缓存策略

`int_equality_caches_` 属于 RmFileHandle，insert/delete/update 都必须同步。新增缓存列时要定义失效和更新规则。

## 8. 注意

1. 修改页后 unpin 必须 dirty=true。
2. 访问页内数据期间 Page 必须保持 pin。
3. 记录大小来自表 schema，record 层不做列类型转换。
4. 指定 Rid 恢复必须维护 bitmap、计数、空闲链。
5. 文件头变化必须刷新到第 0 页。

## 9. 页面内存布局

一个数据页可理解为：

```text
[RmPageHdr][bitmap][slot0][slot1]...[slotN-1]
```

slot 地址：

```cpp
slots + slot_no * file_hdr->record_size
```

bitmap 第 i 位表示 slot i 是否有记录。`num_records_per_page` 必须使页头、bitmap、所有槽总和不超过 PAGE_SIZE。

## 10. `insert_record()` 详细状态变化

1. 获取 insert_latch_，保护空闲链。
2. `create_page_handle()`：若 first_free=-1 新建页，否则 fetch 空闲页。
3. Bitmap::first_bit(false) 找空槽。
4. memcpy buf 到 slot。
5. bitmap 置 1，page_hdr.num_records++。
6. 若页满，从 free list 取下。
7. unpin dirty。
8. 返回 Rid(page_no,slot_no)。

任何提前异常都要考虑页是否已 pin、锁是否由 RAII 释放。

## 11. `delete_record()` 详细状态变化

删除前页可能是满页。如果满页删除一条，它第一次重新获得空槽，必须挂回空闲页链。然后 bitmap 清 0、num_records--、缓存删除。重复删除同一空槽不应再次减计数。

## 12. 专题：从 bitmap 改空闲槽链

若题目要求每页维护 free slot linked list，需要改变页内格式：每个空槽前若干字节保存 next slot，并在 RmPageHdr 增加 first_free_slot。此改动会减少可用记录字节或改变槽内容解释，风险大。

最小设计：仍保留 bitmap 判占用，只用页头缓存下一个搜索起点，避免每次从 0 扫描；不改变磁盘格式主要部分。

## 13. 专题：增加批量 insert

不要简单循环公共 insert_record 导致反复加锁/fetch/unpin。可新增内部 `insert_records_internal`：持有一次 insert_latch，在当前 free page 连续填槽，页满再切页。每条仍返回 Rid 数组。

上层索引和事务写集仍必须逐条登记；record 批量接口只负责表字节。

## 14. 专题：记录可变长

当前 ColMeta.offset 和 RmFileHdr.record_size 假设定长。真正 VARCHAR 会影响页布局、Rid 稳定性、更新移动、索引键提取，不是简单把 CHAR 长度设大。资格赛若只要求 VARCHAR(n) 语法，最小可仍按定长 n 存储并明确语义，而不要引入 slotted-page 可变长格式。

## 15. Page pin 的生命周期

```text
fetch_page -> pin_count+1 -> 得到 Page*
访问/复制数据
unpin_page(page_id, dirty)
```

RmPageHandle 中的 `slots/bitmap/page_hdr` 都指向 Page.data_ 内部；unpin 后不应继续使用这些指针，因为 frame 可能被替换。

## 16. record 层变量归属边界

| 信息 | 应放位置 |
|---|---|
| 列名/类型 | System ColMeta，不放 RmFileHandle |
| record_size | RmFileHdr |
| 当前页记录数 | RmPageHdr |
| 槽是否占用 | bitmap |
| 页是否 dirty/pin | storage::Page |
| 行的物理位置 | Rid |
| 事务可见版本 | TransactionManager，不写进普通 RmRecord C++字段就自动持久化 |

## 17. Bitmap 接口如何使用

常见操作：初始化全 0、set(slot)、reset(slot)、is_set(slot)、first_bit(value)。传入的 bitmap 指针属于当前 Page，操作后该页必须 dirty。slot 范围必须小于 num_records_per_page，多出的 bitmap padding 位不能当真实槽。

## 18. 专题：实现反向全表扫描

可新增 RmReverseScan：初始为最后数据页最后已占用槽；next 向 slot--，页内无更多则 page_no--。接口仍实现 RecScan。它只改变输出顺序，不改变记录。

若表页可能存在空洞，不能简单 Rid-- 就返回，必须查 bitmap。
