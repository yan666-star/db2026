# index 目录详解

## 0. B+树里存的是什么

表记录仍在 record 文件。索引叶子存：

```text
联合key -> Rid
```

例如 `(dept,id)=(10,2)` 的 key 是 dept 四字节后接 id 四字节，Rid 指向表记录。索引扫描先找 Rid，再回表读完整行。

### 0.1 `IxIndexHandle::get_value(key,result,transaction)`

| 参数 | 含义 |
|---|---|
| `key` | 长度必须等于 file_hdr_->col_tot_len_ 的二进制键 |
| `result` | 输出 Rid 数组的指针 |
| `transaction` | 索引页锁/latch 上下文，可为空取决于路径 |
| 返回 bool | 是否找到键 |

`key` 不是以 `\0` 结尾的字符串，不能 strlen。INT 中可包含零字节。

### 0.2 `insert_entry(key,value,transaction)`

`value` 是表 Rid。函数找叶子并插入；若溢出 split；可能一直向父分裂并更换 root。返回 page_id 常用于告诉调用方发生了什么或按接口约定处理。

### 0.3 `delete_entry(key,transaction)`

根据完整 key 删除。若索引允许相同 key 多 Rid，仅给 key 无法指定删哪条，因此当前接口/物理结构是否支持重复必须先看实现，不能只删唯一性检查。

### 0.4 `lower_bound/upper_bound`

```text
lower_bound(k)：第一个 >=k 的位置
upper_bound(k)：第一个 >k 的位置
```

所以等值范围通常 `[lower_bound(k), upper_bound(k))`。右端不包含 upper 指向项。

### 0.5 `split(node)` 参数为什么是节点指针

`node` 是已经 pin 的超容量节点。split 创建并返回新兄弟，原 node 保留前半。调用者随后把新节点首键插入父。两节点 Page 在维护完成前都必须保持有效。

## 1. 归属

index 实现联合键 B+树。Planner/IndexScan 构造 key，IxIndexHandle 只比较字节键并维护树。

| 文件 | 作用 |
|---|---|
| [ix_defs.h](../../src/index/ix_defs.h:35) | 文件头、页头、Iid |
| [ix_index_handle.h](../../src/index/ix_index_handle.h:61) | 节点与整树接口 |
| [ix_index_handle.cpp](../../src/index/ix_index_handle.cpp:1) | B+树算法 |
| [ix_scan.h](../../src/index/ix_scan.h:28) | 叶子范围扫描 |
| [ix_manager.h](../../src/index/ix_manager.h:30) | 索引文件生命周期 |

`ix.h` 为聚合 include；`ix_scan.cpp` 实现跨叶移动；CMakeLists 编译 ix_index_handle.cpp/ix_scan.cpp。

## 2. IxFileHdr

| 变量 | 作用 |
|---|---|
| `root_page_` | 根页 |
| `first_leaf_/last_leaf_` | 叶链边界 |
| `col_types_/col_lens_` | 联合键各列类型和长度 |
| `col_tot_len_` | 一个 key 总长 |
| `btree_order_` | 节点容量阶数 |
| `keys_size_` | 页内 keys 区总长度 |
| `num_pages_` | 索引页数 |

序列化与反序列化 offset 顺序必须一致。

## 3. IxNodeHandle

它借用一个已 pin Page，将页面解释为：IxPageHdr、key 数组、Rid 数组。

| 变量/函数 | 作用 |
|---|---|
| `file_hdr` | 键长度、阶数等全局布局 |
| `page` | 当前物理页 |
| `page_hdr` | 是否叶子、父页、键数、叶链指针 |
| `keys` | 定长联合键区域 |
| `rids` | 叶子为数据 Rid；内部节点用 page_no 指子页 |
| `lower_bound/upper_bound` | 页内二分位置 |
| `insert/remove` | 页内移动并维护键数 |

## 4. IxIndexHandle

| 变量 | 所有权/作用 |
|---|---|
| `fd_` | 索引文件描述符 |
| `file_hdr_` | 堆分配文件头，由句柄管理 |
| `disk_manager_` | 借用 |
| `buffer_pool_manager_` | 借用 |
| `root_latch_` | 保护根与结构变化 |

核心路径：

```text
find_leaf_page -> get_value/lower_bound/upper_bound
insert_entry -> leaf insert -> split -> insert_into_parent
delete_entry -> remove -> redistribute/coalesce -> adjust_root
```

## 5. 键与比较

`ix_compare` 逐列按 ColType 比较。联合键是所有列字节拼接，偏移由 `col_lens_` 累加。INT/FLOAT 不能用 memcmp 代替数值比较；STRING 可按固定长度 memcmp。

## 6. IxScan

Iid 表示 `(page_no,slot_no)` 索引位置，不是表 Rid。IxScan 从 lower Iid 遍历到 upper Iid，跨叶页利用 next_leaf；`rid()` 返回当前叶子项保存的表 Rid。

## 7. 类似现场改动

### 支持新的范围写法

优先改 IndexScanExecutor 的边界构造，不改 B+树。只要能生成 lower_key/upper_key，IxIndexHandle 的 lower_bound/upper_bound 可复用。

### 唯一索引改普通索引

当前叶项结构通常假定一个 key 对一个 Rid。若允许重复键，需要定义 `(key,rid)` 排序或重复 Rid 容器，影响查找、插入、删除和扫描，不属于简单只删冲突检查。若赛题只要求普通索引但框架叶子支持重复，应先检查 Node::insert 的相等键行为。

### 改节点分裂策略

保持 B+树不变量：叶子链、父指针、父分隔键、根页号。split 后新节点所有子页的 parent 都要维护。

### 新键类型

同步 ColType 序列化、ix_compare、IndexScan key 写入、最小最大哨兵。

## 8. 注意

1. 每个 fetch/new page 最终必须 unpin。
2. 修改页面必须标 dirty。
3. 叶子分裂要维护前后叶链和 first/last leaf。
4. 根变化要写 file_hdr_ 并持久化。
5. 内部节点 Rid.page_no 表示子页，不是表记录页。
6. 删除索引项必须使用完整联合键。

## 9. 查找路径逐层解释

`find_leaf_page(key, operation, transaction)` 从 root 开始：内部节点 `internal_lookup` 找应该进入的子页 page_no，fetch 子页并释放父页，直到 leaf。返回的 IxNodeHandle 指向仍被 pin 的叶页，调用者负责 release。

`get_value` 在叶子做 lower_bound，比较相等后取 Rid。`lower_bound/upper_bound` 返回 Iid，IxScan 用它们组成半开区间。

## 10. 插入和 split

叶插入后：

```text
size <= max -> 完成
size > max  -> split
```

split 分配新节点，将后半 keys/rids 移过去。叶节点还要：

- 新叶 next=旧叶 next。
- 新叶 prev=旧叶。
- 旧后继 prev=新叶。
- 旧叶 next=新叶。
- 旧叶是 last_leaf 时更新 file_hdr.last_leaf。

`insert_into_parent`：旧节点无父表示创建新根；否则在父节点插入新子的首键，父溢出再递归 split。

## 11. 内部节点 key 的含义

本框架内部节点的 key/rid 对表示分隔键和子页。修改父分隔键时必须遵守当前 `internal_lookup` 的 lower/upper 规则；不能照搬另一套 B+树教材中“n keys, n+1 pointers”的数组下标而不看本实现。

## 12. 删除、redistribute、coalesce

删除后节点低于 min size：

1. 找父和相邻兄弟。
2. 兄弟有多余项则 redistribute，并更新父分隔键。
3. 否则合并节点，父删除一个子项。
4. 父下溢递归处理。
5. 根只剩一个子时 adjust_root，让唯一子成为新根。

叶合并必须修复叶链；内部合并必须更新移入子节点的 parent。

## 13. 专题：降序索引扫描

当前 IxScan 沿 next_leaf 正向。若题目要求利用索引完成 DESC，需要：

- IxScan 支持 reverse 标志。
- 初始 Iid 设为 upper 边界前一个位置。
- next() 在页内 slot--，越界走 prev_leaf 并定位末项。
- 结束条件与 lower 边界比较。

Planner 只有在 ORDER BY 列序与索引前缀兼容时才能省略 Sort。仅实现反向 IxScan 但 Planner 不选择，功能仍由 Sort 正确完成，只是没有优化。

## 14. 专题：前缀字符串 LIKE 使用索引

`name LIKE 'abc%'` 可转范围：

```text
lower = 'abc' 后补最小字节
upper = 下一个前缀界限
```

但 `%` 在中间或 `_` 通配不能简单转连续范围。即使构造候选范围，仍需回表 LIKE 过滤。

## 15. 专题：新增键类型

例如 BIGINT：

```text
ColType 加 TYPE_BIGINT
IxFileHdr 序列化能保存枚举
ix_compare 按 int64_t memcpy 后比较
IndexScan 写 rhs_val 到 8 字节 key
write_min/write_max 使用 int64 极值
System ColMeta.len=8
```

只改 ix_compare 不够，边界键仍会写错长度。

## 16. 页与句柄所有权

IxNodeHandle 通常由 `new` 返回，但内部 Page 属于 BufferPool。释放函数既要 unpin Page，也要 delete handle；事务延迟页集合的所有权按当前 B+树协议处理。新增提前 return 时逐条检查是否 release_node_handle。

## 17. IxManager 文件名规则

`get_index_name(table, cols)` 按表名和索引列组成物理文件名。SmManager 创建、打开、删除以及 ihs_ 查找必须使用同一个函数，不能各自手拼字符串。

新增“索引自定义名称”时，IndexMeta 必须存 index_name，IxManager 接口应按名字定位；否则重启后只靠列名无法恢复用户命名。

## 18. 专题：覆盖索引

若查询列全部在索引 key 中，可不回表，但当前叶子只返回 key+Rid，IndexScanExecutor 需要从 key 解码投影列并构造 schema。Planner 还需判定 select/filter 列是否被覆盖。若只在 Executor 擅自不回表，非索引列条件会读不到。
