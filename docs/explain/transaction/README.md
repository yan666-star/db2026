# transaction 目录详解

## 0. 事务对象到底保存什么

事务不是表数据副本，而是一份“本事务身份+状态+做过哪些事”的记录。

```text
txn_id：我是谁
state：我是否已经结束
write_set：我做了哪些写，可怎样撤销
read/start/commit timestamp：MVCC 中我能看什么
lock_set：我持有什么锁
```

### 0.1 `begin(txn,log_manager,isolation_level)` 参数

| 参数 | 含义 |
|---|---|
| `txn` | 可复用已有 Transaction；传 nullptr 通常新建 |
| `log_manager` | 写 BEGIN 等 WAL，借用 |
| `isolation_level` | 新事务采用的隔离级别 |
| 返回值 | Transaction*，由 TransactionManager 管理 |

begin 分配 txn_id、初始化状态/时间戳、登记 txn_map。调用方保存 txn_id，不负责直接 delete。

### 0.2 `commit(txn,log_manager)`

`txn` 必须是活动事务；`log_manager` 用于提交日志和持久性。基础写集路径提交时不再修改表，因为 DML 已经写了，主要清写集/释放锁/标 COMMITTED。MVCC 路径还要发布提交时间戳和版本。

### 0.3 `abort(txn,log_manager)`

物理路径从 write_set.back 开始：

```text
取最后 WriteRecord
 -> SmManager.rollback
 -> delete WriteRecord
 -> pop_back
```

为什么逆序：先 INSERT A，再 UPDATE A，回滚必须先把 UPDATE 还原，再删除 INSERT 的 A。

### 0.4 `WriteRecord` 三种构造数据

INSERT 不需要旧记录，因为撤销就是删除 rid；DELETE/UPDATE 必须保存 old record。`tab_name` 让 SmManager 找表，Rid 让它找位置。

## 1. 归属

transaction 管理一条连接中的事务状态、提交/回滚、写集、隔离级别和当前分支的 MVCC/串行化数据。

| 文件 | 作用 |
|---|---|
| [txn_defs.h](../../src/transaction/txn_defs.h:20) | 状态、隔离级别、WriteRecord、锁标识 |
| [transaction.h](../../src/transaction/transaction.h:26) | Transaction、UndoLog |
| [transaction_manager.h](../../src/transaction/transaction_manager.h:54) | 生命周期和并发接口 |
| [transaction_manager.cpp](../../src/transaction/transaction_manager.cpp:1) | begin/commit/abort/MVCC 实现 |
| [watermark.h](../../src/transaction/watermark.h:23) | 最老活跃读时间戳 |

`transaction_manager.cpp.rmdb2025` 是参考/历史文件，不是当前 CMake 默认实现；`.DS_Store` 不改；CMakeLists 编译 transaction_manager.cpp 与 watermark.cpp。

## 2. 状态与模式

| 变量/枚举 | 作用 |
|---|---|
| `txn_mode_` | true 为显式 BEGIN，false 为单 SQL 隐式事务 |
| `state_` | DEFAULT/GROWING/SHRINKING/COMMITTED/ABORTED |
| `isolation_level_` | READ_COMMITTED/SNAPSHOT/SERIALIZABLE 等 |
| `txn_id_` | 唯一事务标识 |
| `thread_id_` | 创建事务线程 |
| `prev_lsn_` | WAL 事务链上一日志 |
| `start_ts_` | 开始时间戳 |
| `read_ts_` | 快照读取时间 |
| `commit_ts_` | 提交时间，未提交为 INVALID_TS |

同一客户端连接在 rmdb.cpp 保存 txn_id，后续语句由 TransactionManager::get_transaction 取得同一对象。

## 3. WriteRecord

| 字段 | 作用 |
|---|---|
| `wtype_` | INSERT/DELETE/UPDATE |
| `tab_name_` | 定位表和索引 |
| `rid_` | 物理记录位置 |
| `record_` | DELETE/UPDATE 前旧记录 |

`write_set_` 按操作发生顺序追加 WriteRecord；ABORT 必须从 back 逆序撤销。COMMIT 释放记录但不执行撤销。

## 4. Transaction 其他容器

| 变量 | 所有权/作用 |
|---|---|
| `write_set_` | shared deque，基础物理回滚 |
| `lock_set_` | 已持有锁标识 |
| `index_latch_page_set_` | B+树操作中加 latch 的页 |
| `index_deleted_page_set_` | 延后删除的索引页 |
| `undo_logs_` | MVCC 撤销版本，只追加或就地更新 |
| `latch_` | 保护事务级 undo_logs |

## 5. UndoLink/UndoLog

UndoLink 用 `(prev_txn_, prev_log_idx_)` 指向某事务的某条撤销日志。UndoLog 保存是否删除、修改字段、旧元组等。版本链不能保存 vector 元素裸指针，因为 vector 扩容会移动对象；使用事务 ID 和下标保持逻辑引用。

## 6. TransactionManager

| 成员/接口 | 作用 |
|---|---|
| `begin` | 分配 txn_id/时间戳并登记事务 |
| `commit` | 持久化与可见性处理、状态结束 |
| `abort` | 逆序回滚或撤销 MVCC 未提交版本 |
| `txn_map` | txn_id 到 Transaction |
| `next_txn_id_` | 原子 ID 生成器 |
| `concurrency_mode_` | 2PL/BASIC_TO/MVCC |
| `get_visible_record` | 按快照重建当前可见版本 |
| `prepare_insert/update/delete` | 写前冲突和版本登记 |
| `record_versions_` | RecordKey 到版本数组 |
| `mvcc_txns_` | 每事务 MVCC 状态 |
| `active_txns_` | 活跃事务集合 |

## 7. Watermark

`current_reads_` 统计每个 read_ts 的活跃事务数；`watermark_` 是最老活跃读时间。早于 watermark 且不再被任何快照需要的版本才可回收。

## 8. 注意

1. 事务提交前不可把未持久化结果对其他事务可见。
2. ABORT 必须逆序。
3. 表和全部索引一起回滚。
4. `txn_mode_` 与 TransactionState 是不同概念。
5. 不要删除 undo_logs_ 中间元素破坏 UndoLink 下标。
6. 并发模式分支要明确，不能把 2PL 写集逻辑误当完整 MVCC 版本逻辑。

## 9. 显式事务和隐式事务完整流

隐式：语句开始若无活动事务，begin 并设 txn_mode=false；语句成功后自动 commit，失败 abort。

显式：BEGIN 创建/复用事务并设 txn_mode=true；中间语句结束不自动提交；COMMIT/ABORT 后设状态并让下一条 SQL 创建新事务。

`txn_mode_` 只表示自动提交策略，`state_` 才表示事务是否已经结束。

## 10. 物理回滚逐操作

### INSERT 的 WriteRecord

只需 tab_name+rid。回滚时当前 rid 记录仍在表中，用它构造所有索引键，先删除索引项，再删除记录。

### DELETE 的 WriteRecord

必须深拷贝 old_rec。回滚时按原 rid 插回，再按 old_rec 恢复索引。

### UPDATE 的 WriteRecord

保存 old_rec。回滚时读取当前 new_rec 删除新索引键，以 old_rec 覆盖表记录，插入旧键。

如果记录里有指向临时缓冲的浅拷贝，事务结束前数据会失效，所以 RmRecord 拷贝必须深拷贝。

## 11. READ COMMITTED 与 SNAPSHOT 的读取时间

SNAPSHOT 通常整个事务固定 read_ts；READ COMMITTED 每条语句可刷新快照，语句边界由 `enter_statement/leave_statement` 标记。

## 12. MVCC 关键结构

| 结构 | 作用 |
|---|---|
| `RecordKey` | file_id+Rid 唯一定位逻辑记录 |
| `MvccVersion` | 一个提交/未提交版本及删除标记 |
| `MvccTxnState` | 事务级读写集合、依赖、状态 |
| `ReadPredicate` | 可串行化谓词读描述 |
| `record_versions_` | 每记录版本链 |
| `version_info_` | 页/槽到 UndoLink 信息 |

写操作必须经 prepare_xxx 登记，否则 get_visible_record 不知道版本；只物理覆盖表页会破坏快照。

## 13. 事务锁与 latch 区别

事务锁保护逻辑表/记录并跨语句持有；mutex/latch 保护内存结构的短临界区。不能用 `latch_` 替代 SQL 锁，也不应持 TransactionManager 全局 latch 执行磁盘 I/O。

## 14. COMMIT 后清理什么

物理事务：释放 WriteRecord、锁、索引 latch/deleted page 集合。MVCC：分配 commit_ts、把本事务版本标已提交、更新 watermark/依赖、最后释放资源。清理顺序不能早于持久化和版本发布。
