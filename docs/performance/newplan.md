xin 数据库内核重构实施计划
当前仓库状态已确认：
branch: xin
HEAD:   1b5408f86b6aee79a15cdade81709107a83d0644
remote: origin/xin 同样指向 1b5408f
status: clean
我没有继续修改源码，也没有生成计划文档，避免违反“最终改动只落在 xin/src”的限制。下面内容可以直接交给你朋友执行。
一、全局约束
执行过程中必须遵守：
所有代码和测试修改只允许进入 src/。
不修改 Wire v3、SQL 语法、Page 磁盘大小、表文件格式、B+ 树磁盘节点格式和 WAL 记录格式。
不出现 orders、order_line、stock 等表名特判。
不根据固定 SQL、statement id 或列名优化。
COMMIT ACK 前必须满足 durable_lsn >= commit_lsn。
未持久化事务不得对其他事务可见。
保留 AUTO_ABORT、SI、SERIALIZABLE/SSI、FLOAT32 和 crash recovery。
每个阶段独立提交；阶段测试失败不得继续叠加后续重构。
只在 Linux/GCC 环境编译，保持 C++17。
不直接复制 performance 分支文件，只借鉴已经验证的思路。
同一 worktree 只能有一个人写。
二、整体阶段
M0 基线和性能计数
 ↓
M1 BufferPool PageGuard
 ↓
M2 BufferPool 分片页表和非阻塞 I/O
 ↓
M3 Heap 页面锁、空闲页管理、批量插入
 ↓
M4 B+Tree 页面级 latch crabbing
 ↓
M5 MVCC/SSI 提交状态机和索引版本
 ↓
M6 WAL 批量追加、PageLSN、group commit
 ↓
M7 EXEC_BATCH 通用批写
 ↓
M8 恢复、并发、长时间性能验收
Task 0：建立不可变基线
操作
git switch xin
git status --short --branch
git rev-parse HEAD
必须得到：
1b5408f86b6aee79a15cdade81709107a83d0644
构建：
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
基线综合测试：
CLIENTS=32 DATA_SCALE=small \
  bash SQL测试/performance_test/run_local_evaluation.sh quick
保存但不要提交：
build/benchmark_report.json
build/*server.log
必须记录：
NewOrder tpmC；
总提交数、abort 数和 abort 比率；
BufferPool fetch/hit/miss；
Heap 表锁等待；
B+ 树 root latch 等待；
WAL fsync 次数、平均 group size；
p50/p95/p99 延迟。
如果 1b5408f 基线测试失败，先定位环境或原有失败，不得开始重构。
Task 1：引入 PageGuard，不改变替换算法
文件
新建：
src/storage/page_guard.h
src/storage/page_guard.cpp
src/test/page_guard_test.cpp
修改：
src/storage/page.h
src/storage/buffer_pool_manager.h
src/storage/buffer_pool_manager.cpp
src/storage/CMakeLists.txt
src/test/CMakeLists.txt
核心接口
class BasicPageGuard {
public:
    BasicPageGuard() = default;
    BasicPageGuard(BasicPageGuard &&other) noexcept;
    BasicPageGuard &operator=(BasicPageGuard &&other) noexcept;
    ~BasicPageGuard();

    Page *get_page();
    PageId page_id() const;
    void mark_dirty();
    void drop();

private:
    BufferPoolManager *bpm_ = nullptr;
    Page *page_ = nullptr;
    frame_id_t frame_id_ = INVALID_FRAME_ID;
    uint64_t generation_ = 0;
    bool dirty_ = false;
};

class ReadPageGuard {
public:
    const Page *get_page() const;
    const char *data() const;
    void drop();
};

class WritePageGuard {
public:
    Page *get_page();
    char *data();
    void mark_dirty();
    void drop();
};
BufferPool 增加：
ReadPageGuard fetch_page_read(PageId page_id);
WritePageGuard fetch_page_write(PageId page_id);
WritePageGuard new_page_guarded(PageId *page_id);
Frame 元数据
不要把运行时锁写入磁盘 Page 数据，新增内存控制块：
enum class FrameState {
    FREE,
    LOADING,
    VALID,
    EVICTING
};

struct FrameControl {
    std::mutex meta_latch;
    std::shared_mutex content_latch;
    std::condition_variable io_cv;
    FrameState state = FrameState::FREE;
    uint64_t generation = 0;
    uint64_t dirty_epoch = 0;
};
正确性规则
PageGuard 构造前已经完成 pin。
ReadPageGuard 持有 shared_lock(content_latch)。
WritePageGuard 持有 unique_lock(content_latch)。
guard 销毁顺序必须是：释放内容锁，再 unpin。
Write guard 只有调用 mark_dirty() 才传播脏页。
move 后原 guard 必须失效，防止双重 unpin。
generation 不一致时禁止对已经复用的 frame 执行 unpin。
暂时保留原始 fetch_page/unpin_page，但标注为 legacy 接口。
必测行为
page_guard_test.cpp 至少覆盖：
guard 析构后 pin count 恢复；
move 不会双重 unpin；
两个 Read guard 可以并发；
Write guard 阻塞 Read/Write；
dirty 标记传播；
被 pin 的 frame 不能成为 victim；
frame 复用后旧 guard 不能污染新页面。
提交
git add src/storage src/test
git commit -m "Add RAII page guards and frame content latches."
Task 2：重写 BufferPool 并发控制
文件
修改：
[buffer_pool_manager.h](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/storage/buffer_pool_manager.h)
[buffer_pool_manager.cpp](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/storage/buffer_pool_manager.cpp)
[lru_replacer.h](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/replacer/lru_replacer.h)
[lru_replacer.cpp](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/replacer/lru_replacer.cpp)
新建：
src/test/buffer_pool_concurrency_test.cpp
页表分片
static constexpr size_t kPageTableShardCount = 64;

struct PageTableShard {
    std::mutex latch;
    std::unordered_map<PageId, frame_id_t, PageIdHash> pages;
};
PageIdHash 必须重写，不能继续使用 fd << 16 | page_no：
size_t PageIdHash::operator()(const PageId &id) const {
    uint64_t value =
        (static_cast<uint64_t>(static_cast<uint32_t>(id.fd)) << 32) |
        static_cast<uint32_t>(id.page_no);
    return std::hash<uint64_t>{}(value);
}
缺页加载流程
锁页表分片
  → 未命中
  → 选择 victim
  → 将 frame 标记为 LOADING、generation++
  → 发布 page_id → frame 映射
  → 释放页表锁
  → 持有 frame 内容写锁
  → 必要时刷旧页
  → 从磁盘读新页
  → state = VALID
  → 唤醒等待者
其他线程命中 LOADING：
不允许重复读盘；
释放页表锁；
等待 io_cv；
state 变成 VALID 后重新校验 generation 和 PageId。
禁止事项
禁止持有页表锁执行 read_page()、write_page()、WAL flush。
禁止 flush_all_pages() 持有全局锁扫描并写盘。
禁止 pin/unpin 每次都无条件改 replacer。
只有 pin count 在：
0 → 1：从 replacer 移除
1 → 0：加入 replacer
时才调用 replacer。
LRU
保留题目要求的 LRU 语义，不直接换随机替换。把 list + unordered_map 改成预分配的 intrusive 双向链表数组，消除热路径内存分配。
Flush
Flush 应：
短暂锁定 frame；
在 ReadPageGuard 下把 4KB 页面复制到栈/临时 buffer；
记录 dirty_epoch；
释放内容锁；
等待 durable_lsn >= page_lsn；
执行磁盘写；
generation、PageId、dirty_epoch 均未变化时才清 dirty。
验收
相同 PageId 的并发 miss 只能触发一次磁盘读。
磁盘读写期间其他 shard 的命中不阻塞。
flush_all_pages() 与并发写不会丢失 dirty。
BufferPool 单测全过。
quick benchmark 不允许比基线下降超过 5%。
提交
git commit -am "Shard buffer metadata and move page IO outside mapping locks."
Task 3：Heap 页面锁和空闲页重构
文件
修改：
[rm_file_handle.h](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/record/rm_file_handle.h)
[rm_file_handle.cpp](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/record/rm_file_handle.cpp)
[rm_scan.cpp](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/record/rm_scan.cpp)
[rm_scan.h](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/record/rm_scan.h)
新建：
src/test/heap_page_concurrency_test.cpp
页面句柄
拆分读写句柄：
class RmPageReadHandle {
    ReadPageGuard guard_;
    const RmPageHdr *page_hdr_;
    const char *bitmap_;
    const char *slots_;
};

class RmPageWriteHandle {
    WritePageGuard guard_;
    RmPageHdr *page_hdr_;
    char *bitmap_;
    char *slots_;
};
禁止返回不拥有 guard 的 RmPageHandle。
去除整表串行
删除常规 DML 对 insert_latch_ 的依赖，替换为：
std::mutex allocation_latch_;
std::mutex free_pages_latch_;
std::vector<page_id_t> free_page_candidates_;
插入算法：
从候选集合取 page_no
  → 释放 free_pages_latch
  → 获取该页 Write guard
  → 重新检查是否有空槽
  → 有空槽则写入
  → 已满则释放页面并重试
  → 没有候选页时才进入 allocation_latch_ 创建新页
页面由“非满变满”或“满变非满”后，先释放 page guard，再更新候选集合，避免 free_pages_latch → page latch 与反向锁序混用。
扫描器
RmScan 不再：
读取 bitmap → 复制所有 RID → unpin → 再次 fetch 同一页
改为：
bool read_next_page_batch(
    page_id_t page_no,
    std::vector<Rid> *rids,
    std::vector<std::unique_ptr<RmRecord>> *records);
一次 Read guard 下完成 bitmap 遍历和记录复制。
INT equality cache
int_equality_caches_ 只能保留为无正式索引时的非 MVCC 慢速兼容路径。正常 DML、MVCC 和 NewOrder 禁止维护该缓存。
批量接口
struct PendingInsert {
    const char *data;
    size_t size;
};

std::vector<Rid> insert_records(
    const std::vector<PendingInsert> &records,
    Context *context,
    const std::string &table_name);
接口必须按目标 Heap page 归组，每页只获取一次写锁。
验收
同表不同页面 INSERT 可并发。
同页并发 INSERT 不会分配重复 slot。
bitmap、num_records、空闲页集合一致。
recovery 指定 RID 插入仍可工作。
Heap 锁等待显著低于基线。
Task 4：B+ 树页面级 latch crabbing
文件
修改：
[ix_index_handle.h](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/index/ix_index_handle.h)
[ix_index_handle.cpp](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/index/ix_index_handle.cpp)
[ix_scan.h](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/index/ix_scan.h)
[ix_scan.cpp](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/index/ix_scan.cpp)
新建：
src/test/ix_concurrency_test.cpp
句柄所有权
IxNodeHandle 不再负责 pin/unpin，只解析页面内容。新增：
struct IxReadNode {
    ReadPageGuard guard;
    IxNodeHandle node;
};

struct IxWriteNode {
    WritePageGuard guard;
    IxNodeHandle node;
};
锁策略
root_latch_ 只保护 root_page_ 的读取和替换。
点查使用逐层共享锁耦合：锁 child 后立即释放 parent。
插入使用写锁 crabbing。
如果 child 对当前操作安全，释放此前所有祖先锁。
bool IxNodeHandle::is_safe(Operation op) const {
    if (op == Operation::INSERT) {
        return get_size() < get_max_size() - 1;
    }
    return get_size() > get_min_size();
}
当前实现本身采用 lazy deletion，可以先继续保留，不要一开始恢复复杂递归 merge。
Split 锁顺序
树路径：父节点先于子节点。
同层兄弟：page_no 小的先锁。
叶链更新始终从左到右。
不允许持有 B+ 树页面锁进入 Heap。
不允许持有 Heap 页面锁进入 B+ 树。
事务先生成 Heap/Index 操作列表，再分别应用，避免跨子系统嵌套锁。
Range scan
IxScan 每次只在 Read guard 下复制当前叶的一批 RID 和 next_leaf，随后释放锁。不能保存裸页面指针，也不能在两个独立 root lock 下分别计算 lower/upper 后长期使用不受保护的位置。
批量插入
void insert_entries_batch(
    std::vector<std::pair<std::vector<char>, Rid>> entries,
    Transaction *txn);
要求：
按 key 排序；
相邻 key 尽量复用根到叶路径；
同一叶节点只加锁一次；
批内先检查重复 key；
不改变唯一索引语义。
验收
并发点查与不相关叶插入不竞争 root 独占锁。
连续插入触发多层 split 后所有 key 可查。
range scan 无重复、无越界、无失效 PageId。
root split 与并发读无崩溃。
运行随机插入/删除/查询后，对照 std::map 结果。
Task 5：重写 MVCC 和 SSI 提交状态机
文件
修改：
[transaction_manager.h](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/transaction/transaction_manager.h)
[transaction_manager.cpp](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/transaction/transaction_manager.cpp)
[transaction.h](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/transaction/transaction.h)
[txn_defs.h](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/transaction/txn_defs.h)
新建：
src/transaction/index_version_store.h
src/transaction/index_version_store.cpp
src/test/mvcc_commit_visibility_test.cpp
src/test/mvcc_index_visibility_test.cpp
状态机
enum class MvccCommitState {
    ACTIVE,
    VALIDATING,
    APPLYING,
    DURABLE,
    VISIBLE,
    ABORTED
};
只允许读取 VISIBLE 版本。
正确提交顺序
1. 获取当前事务的写集合
2. 按 RecordKey 排序
3. 检查写写冲突和唯一键冲突
4. 检查 SSI dangerous structure
5. 分配 commit_ts，但不发布
6. 状态改为 APPLYING
7. 应用 Heap 和 Index 物理变更
8. 追加 COMMIT WAL，得到 commit_lsn
9. force_flush_up_to(commit_lsn)
10. 状态改为 DURABLE
11. 在相关 MVCC shard 内发布所有版本
12. 状态改为 VISIBLE
13. 释放写意向和 SSI 元数据
14. 返回 COMMIT ACK
禁止继续采用“先写 commit_ts，释放锁，然后做物理 Phase-2”的模式。
写意向
struct WriteIntent {
    txn_id_t owner;
    std::condition_variable cv;
};

std::array<WriteIntentShard, 256> write_intent_shards_;
Key 必须是实际 RecordKey(file_id, RID)，不能使用 prepared plan 谓词哈希代替真实记录锁。
等待规则采用 wait-die：
较老事务可以等待较新事务；
较新事务遇到较老事务立即 abort；
等待必须有事务结束通知；
不允许持有 MVCC shard 锁等待。
SSI
拆分为：
点读：RecordKey → readers
索引范围读：index_id + [lower, upper] → readers
无法索引化的复杂谓词：file_id → table-level SIREAD
写入只检查相同 RecordKey、相交索引范围或同表粗粒度 SIREAD，不能扫描所有事务的所有 predicate。
索引历史
为保证旧快照仍能通过旧 key 找到记录：
B+ 树保留最新物理映射；
key-changing UPDATE/DELETE 在 IndexVersionStore 保存旧 key→RID；
snapshot index scan 合并物理 B+ 树结果与仍可能被活动快照看到的历史映射；
watermark 前禁止清理历史；
普通 INSERT 和非索引列 UPDATE 不进入该慢路径；
恢复后没有旧活动快照，只需恢复最新物理索引。
必测故障点
在以下位置强制 kill：
Heap 应用前；
Heap 应用后、索引应用前；
索引应用后、COMMIT WAL 前；
COMMIT WAL write 后、fsync 前；
fsync 后、VISIBLE 前；
VISIBLE 后、ACK 前。
恢复结果必须只由 durable COMMIT 决定。
Task 6：WAL、PageLSN 和 group commit
文件
修改：
[log_manager.h](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/recovery/log_manager.h)
[log_manager.cpp](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/recovery/log_manager.cpp)
[log_recovery.cpp](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/recovery/log_recovery.cpp)
Heap/B+ 树所有修改页面的位置
新建：
src/test/wal_page_lsn_test.cpp
src/test/group_commit_test.cpp
批量追加接口
保持 WAL 磁盘格式不变：
std::vector<lsn_t> add_logs_to_buffer(
    const std::vector<LogRecord *> &records);
一个事务的一批行日志只获取一次 WAL mutex。
PageLSN
每次修改 Heap/B+ 页面都必须：
page->set_page_lsn(change_lsn);
guard.mark_dirty();
刷脏页前必须：
log_manager->force_flush_up_to(page->get_page_lsn());
不能只调用 flush_log_to_disk(false) 后立即写数据页。
Group commit
第一个等待者成为 leader；
其他事务追加 COMMIT 后成为 follower；
leader 一次 write + fsync；
每个 follower 独立检查 durable_lsn >= own_commit_lsn；
不允许仅因为 leader 完成就全部返回；
先保留可配置窗口，默认值用基准决定，不能写 TPC-C 特判。
验收
test_log_manager_durability 全过；
一次 fsync 能覆盖多个 commit；
kill 后不会出现 durable COMMIT 丢失；
未提交但已经写过数据页的事务能够被 UNDO。
Task 7：把 EXEC_BATCH 变成真正的通用批写
文件
修改：
[request_dispatcher.cpp](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/network/request_dispatcher.cpp)
[sql_execution_service.h](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/execution/sql_execution_service.h)
[sql_execution_service.cpp](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/execution/sql_execution_service.cpp)
[executor_insert.h](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/execution/executor_insert.h)
[execution_manager.cpp](D:/DMS-DESIGN/db2026/.worktrees/finals-wire-ssi/src/execution/execution_manager.cpp)
新建：
src/execution/executor_batch_insert.h
src/test/batch_insert_test.cpp
识别规则
只允许基于通用结构识别：
同一个 explicit transaction；
连续操作；
相同 prepared statement；
plan tag 为 INSERT；
schema 和参数类型一致。
不能检查 SQL 文本、表名、列名或 transaction 名称。
接口
void execute_prepared_batch(
    const wire::PreparedStatement &statement,
    const std::vector<std::vector<TypedValue>> &parameter_rows,
    ResultSink &sink);
执行流程：
一次绑定和类型检查
  → 构造全部记录
  → 批内唯一键去重
  → 查询已有唯一索引
  → 批量申请 Heap slot
  → 一次追加行 WAL
  → 按 Heap page 批写
  → 按 index/key 排序
  → B+Tree 批量插入
  → 合并为事务写集合
若第 N 条失败：
整个显式事务进入失败状态；
已执行记录由 write set/UNDO 回滚；
协议只返回合法的 TRANSACTION_ABORT 或 batch failure；
不得返回前 N−1 条成功后继续执行。
验收
单行 INSERT 行为不变；
5～15 行批量 INSERT 只锁少量 Heap page；
同一叶 B+ 树只遍历/加锁一次；
batch 内重复唯一键正确失败；
AUTO_ABORT 响应符合 Wire v3。
Task 8：最终验收
每个阶段都运行：
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
完整本地门槛：
CLIENTS=32 DATA_SCALE=small \
  bash SQL测试/performance_test/run_local_evaluation.sh full
另外分别运行：
python3 SQL测试/performance_test/run_acid_tests.py \
  --start-server --quick --crash-check \
  --isolation snapshot --build-dir build \
  --db-dir build/final_acid_si

python3 SQL测试/performance_test/run_acid_tests.py \
  --start-server --quick --crash-check \
  --isolation serializable --build-dir build \
  --db-dir build/final_acid_ser
最后检查：
git status --short
git diff --check 1b5408f86b6aee79a15cdade81709107a83d0644
git diff --name-only 1b5408f86b6aee79a15cdade81709107a83d0644
最后一条输出必须全部位于：
src/
性能保留门槛
阶段	最低要求
PageGuard	正确性全过，性能下降不超过 5%
BufferPool	BufferPool 全局锁等待下降至少 70%
Heap	同表 INSERT 不再全部串行
B+Tree	无 split 时不获取 root 独占锁
MVCC	不可见提交为 0，abort 比率显著下降
WAL	durable_lsn 正确，平均 group size 大于 1
Batch INSERT	5～15 行写入不再执行 5～15 次完整存储路径
最终	本地 NewOrder 至少先达到 1b 的 3 倍，再以 10 倍为优化目标


不要把“本地 10 倍”写成正式评测保证；正式服务器、磁盘和数据规模不同。
推荐提交顺序
1. Add RAII page guards and frame content latches.
2. Shard buffer metadata and move page IO outside mapping locks.
3. Replace heap table latch with page-level synchronization.
4. Add generic heap batch insertion.
5. Add B+ tree latch crabbing and guarded scans.
6. Introduce durable-before-visible MVCC commit states.
7. Index SSI reads and preserve snapshot index history.
8. Batch WAL appends and enforce PageLSN.
9. Execute repeated prepared inserts through the batch storage path.
10. Add crash, concurrency and recovery regressions.
每个提交通过自身测试后才能进入下一个；不要最后一次性调试 BufferPool、B+ 树、MVCC 和 WAL。