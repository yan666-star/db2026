/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "transaction.h"
#include "watermark.h"
#include "recovery/log_manager.h"
#include "concurrency/lock_manager.h"
#include "system/sm_manager.h"
#include "common/exception.h"

/* 系统采用的并发控制算法，当前题目中要求两阶段封锁并发控制算法 */
enum class ConcurrencyMode { TWO_PHASE_LOCKING = 0, BASIC_TO, MVCC };

/// 版本链中的第一个撤销链接，将表堆元组链接到撤销日志。
struct VersionUndoLink {
    /** 版本链中的下一个版本。 */
    UndoLink prev_;
    bool in_progress_{false};

    friend auto operator==(const VersionUndoLink &a, const VersionUndoLink &b) {
        return a.prev_ == b.prev_ && a.in_progress_ == b.in_progress_;
    }

    friend auto operator!=(const VersionUndoLink &a, const VersionUndoLink &b) { return !(a == b); }

    inline static std::optional<VersionUndoLink> FromOptionalUndoLink(std::optional<UndoLink> undo_link) {
        if (undo_link.has_value()) {
            return VersionUndoLink{*undo_link};
        }
        return std::nullopt;
    }
};

class TransactionManager{
public:
    explicit TransactionManager(LockManager *lock_manager, SmManager *sm_manager,
                             ConcurrencyMode concurrency_mode = ConcurrencyMode::TWO_PHASE_LOCKING) {
        sm_manager_ = sm_manager;
        lock_manager_ = lock_manager;
        concurrency_mode_ = concurrency_mode;
    }
    
    ~TransactionManager() = default;

    Transaction* begin(Transaction* txn, LogManager* log_manager,
                       IsolationLevel isolation_level = IsolationLevel::READ_COMMITTED);

    void commit(Transaction* txn, LogManager* log_manager);

    void abort(Transaction* txn, LogManager* log_manager);

    void release_transaction(Transaction *txn);

    void enter_statement(txn_id_t txn_id);

    void leave_statement();

    std::vector<txn_id_t> begin_static_checkpoint();

    void end_static_checkpoint();

    void advance_next_txn_id(txn_id_t next_txn_id) {
        txn_id_t current = next_txn_id_.load();
        while (current < next_txn_id &&
               !next_txn_id_.compare_exchange_weak(current, next_txn_id)) {
        }
    }

    ConcurrencyMode get_concurrency_mode() { return concurrency_mode_; }

    void set_concurrency_mode(ConcurrencyMode concurrency_mode) { concurrency_mode_ = concurrency_mode; }

    LockManager* get_lock_manager() { return lock_manager_; }

    bool uses_mvcc(const Transaction *txn) const {
        return txn != nullptr && txn->uses_mvcc();
    }

    std::unique_ptr<RmRecord> get_visible_record(
        Transaction *txn, uint64_t file_id, const Rid &rid,
        const RmRecord *physical_record);

    std::unique_ptr<RmRecord> get_latest_committed_record(
        uint64_t file_id, const Rid &rid, const RmRecord *physical_record);

    void register_table_read(Transaction *txn, uint64_t file_id,
                             const std::vector<Condition> &conditions,
                             const std::vector<ColMeta> &columns);

    void register_record_read(Transaction *txn, uint64_t file_id,
                              const Rid &rid);

    void prepare_insert(Transaction *txn, uint64_t file_id, const Rid &rid,
                        const RmRecord &new_record);

    void prepare_update(Transaction *txn, uint64_t file_id, const Rid &rid,
                        const RmRecord &old_record,
                        const RmRecord &new_record,
                        const std::string &table_name = "");

    void prepare_delete(Transaction *txn, uint64_t file_id, const Rid &rid,
                        const RmRecord &old_record,
                        const std::string &table_name = "");

    void check_write_conflict(Transaction *txn, uint64_t file_id,
                              const Rid &rid);

    void check_unique_key_conflict(
        Transaction *txn, uint64_t file_id, const Rid &target_rid,
        const RmRecord &new_record, const std::vector<ColMeta> &index_cols);

    std::unique_lock<std::mutex> acquire_commit_apply_latch();

    /**
     * @description: 获取事务ID为txn_id的事务对象
     * @return {Transaction*} 事务对象的指针
     * @param {txn_id_t} txn_id 事务ID
     */    
    Transaction* get_transaction(txn_id_t txn_id) {
        if(txn_id == INVALID_TXN_ID) return nullptr;

        std::lock_guard<std::mutex> lock(latch_);
        auto it = TransactionManager::txn_map.find(txn_id);
        if (it == TransactionManager::txn_map.end()) {
            return nullptr;
        }
        auto *res = it->second;
        if (res == nullptr ||
            res->get_thread_id() != std::this_thread::get_id()) {
            return nullptr;
        }

        return res;
    }

    static std::unordered_map<txn_id_t, Transaction *> txn_map;     // 全局事务表，存放事务ID与事务对象的映射关系
    std::shared_mutex txn_map_mutex_;
    /** ------------------------以下函数仅可能在MVCC当中使用------------------------------------------*/

    /**
    * @brief 更新一个撤销链接，该链接将表堆元组与第一个撤销日志连接起来。
    * 在更新之前，将调用 `check` 函数以确保有效性。
    */
    bool UpdateUndoLink(Rid rid, std::optional<UndoLink> prev_link,
                        std::function<bool(std::optional<UndoLink>)> &&check = nullptr);

    /**
     * @brief 更新一个撤销链接，该链接将表堆元组与第一个撤销日志连接起来。
     * 在更新之前，将调用 `check` 函数以确保有效性。
     */
    bool UpdateVersionLink(Rid rid, std::optional<VersionUndoLink> prev_version,
                           std::function<bool(std::optional<VersionUndoLink>)> &&check = nullptr);

    /** @brief 获取表堆元组的第一个撤销日志。 */
    std::optional<UndoLink> GetUndoLink(Rid rid);

    /** @brief 获取表堆元组的第一个撤销日志。*/
    std::optional<VersionUndoLink> GetVersionLink(Rid rid);

    /** @brief 访问事务撤销日志缓冲区并获取撤销日志。如果事务不存在，返回 nullopt。
     * 如果索引超出范围仍然会抛出异常。 */
    std::optional<UndoLog> GetUndoLogOptional(UndoLink link);

    /** @brief 访问事务撤销日志缓冲区并获取撤销日志。除非访问当前事务缓冲区，
     * 否则应该始终调用此函数以获取撤销日志，而不是手动检索事务 shared_ptr 并访问缓冲区。 */
    UndoLog GetUndoLog(UndoLink link);

    /** @brief 获取系统中的最低读时间戳。 */
    timestamp_t GetWatermark();

    /** @brief 垃圾回收。仅在所有事务都未访问时调用。 */
    void GarbageCollection();

    /**
     * @brief 在静态检查点前物理应用所有已提交的 MVCC 删除。
     * MVCC 删除在提交时只移除索引项，物理记录留待 GC 回收；若检查点把
     * 仍含这些记录的页刷盘，恢复从检查点开始重放时删除日志已在检查点之前，
     * 被删除的行会“复活”。必须在 begin_static_checkpoint() 静默所有事务后、
     * 刷脏页之前调用。
     */
    void apply_committed_deletes_for_checkpoint();

    struct PageVersionInfo {
        std::shared_mutex mutex_;
        /** 存储所有槽的先前版本信息。注意：不要使用 `[x]` 来访问它，因为
         * 即使不存在也会创建新元素。请使用 `find` 来代替。
         */
        std::unordered_map<slot_offset_t, VersionUndoLink> prev_version_;
    };

    /** 保护版本信息 */
    std::shared_mutex version_info_mutex_;
    /** 存储表堆中每个元组的先前版本。 */
    std::unordered_map<page_id_t, std::shared_ptr<PageVersionInfo>> version_info_;


private:
    void finish_transaction(Transaction *txn);
    void check_commit_conflict(Transaction *txn);
    void check_commit_conflict_under_latch(Transaction *txn);
    void commit_mvcc(Transaction *txn);
    void abort_mvcc(Transaction *txn);

    struct RecordKey {
        uint64_t file_id;
        Rid rid;

        bool operator==(const RecordKey &other) const {
            return file_id == other.file_id && rid == other.rid;
        }
    };

    struct RecordKeyHash {
        size_t operator()(const RecordKey &key) const {
            size_t seed = std::hash<uint64_t>()(key.file_id);
            seed ^= std::hash<int>()(key.rid.page_no) + 0x9e3779b9 +
                    (seed << 6) + (seed >> 2);
            seed ^= std::hash<int>()(key.rid.slot_no) + 0x9e3779b9 +
                    (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    struct MvccVersion {
        txn_id_t owner = INVALID_TXN_ID;
        timestamp_t commit_ts = INVALID_TS;
        bool before_deleted = true;
        std::vector<char> before;
        bool deleted = false;
        std::vector<char> data;
        std::string table_name;
    };

    struct ReadPredicate {
        uint64_t file_id;
        std::vector<Condition> conditions;
        std::vector<ColMeta> columns;
    };

    struct MvccTxnState {
        IsolationLevel isolation_level = IsolationLevel::READ_COMMITTED;
        timestamp_t start_ts = 0;
        timestamp_t commit_ts = INVALID_TS;
        bool aborted = false;
        bool cleanup_done = false;
        std::vector<ReadPredicate> predicates;
        std::unordered_set<RecordKey, RecordKeyHash> read_records;
        std::unordered_set<RecordKey, RecordKeyHash> write_records;
        std::unordered_set<txn_id_t> incoming_rw;
        std::unordered_set<txn_id_t> outgoing_rw;
    };

    void prepare_write(Transaction *txn, uint64_t file_id, const Rid &rid,
                       const RmRecord *old_record,
                       const RmRecord *new_record, bool deleted,
                       const std::string &table_name = "");
    bool add_rw_dependency(txn_id_t reader, txn_id_t writer);
    bool dependency_forms_dangerous_structure(txn_id_t reader,
                                              txn_id_t writer) const;
    bool predicate_matches(const ReadPredicate &predicate,
                           const std::vector<char> &record) const;
    bool predicate_affected(const ReadPredicate &predicate,
                            const MvccVersion &version) const;
    bool transactions_overlap(const MvccTxnState &left,
                              const MvccTxnState &right) const;
    bool mvcc_txn_aborted(txn_id_t txn_id) const;
    void mark_mvcc_txn_aborted(txn_id_t txn_id);
    void check_physical_before(Transaction *txn, const std::string &table_name,
                               const Rid &rid, const RmRecord *before_record);
    void validate_pending_physical_before(Transaction *txn);
    RecordKey make_record_key(const std::string &table_name, const Rid &rid) const;
    WriteRecord *first_mutating_write_record(Transaction *txn,
                                             const RecordKey &key) const;
    bool write_record_is_insert_only(Transaction *txn,
                                     const RecordKey &key) const;
    bool has_multiple_mutating_writes(Transaction *txn,
                                       const RecordKey &key) const;
    MvccVersion *find_own_pending_version(std::vector<MvccVersion> &history,
                                           txn_id_t txn_id) const;
    bool wait_for_pending_writer(Transaction *txn, txn_id_t writer,
                                 std::unique_lock<std::mutex> &lock);
    void remove_dependencies(txn_id_t txn_id);

    ConcurrencyMode concurrency_mode_;      // 事务使用的并发控制算法，目前只需要考虑2PL
    std::atomic<txn_id_t> next_txn_id_{0};  // 用于分发事务ID
    std::atomic<timestamp_t> next_timestamp_{0};    // 用于分发事务时间戳
    std::mutex latch_;  // 用于txn_map的并发
    SmManager *sm_manager_;
    LockManager *lock_manager_;

    std::mutex checkpoint_latch_;
    std::mutex checkpoint_serial_latch_;
    std::condition_variable checkpoint_cv_;
    bool checkpoint_in_progress_ = false;
    size_t active_statements_ = 0;
    std::unordered_set<txn_id_t> active_txns_;

    std::atomic<timestamp_t> last_commit_ts_{0};    // 最后提交的时间戳,仅用于MVCC
    Watermark running_txns_{0};             // 存储所有正在运行事务的读取时间戳，以便于垃圾回收，仅用于MVCC
    std::atomic<uint64_t> mvcc_commit_count_{0};    // 用于按周期触发MVCC垃圾回收

    // Lock ordering: commit_apply_latch_ -> (file insert_latch_ / index latches)
    // and commit_apply_latch_ -> mvcc_latch_. mvcc_latch_ is a LEAF lock: no
    // file-handle or index operation may be invoked while holding it, because
    // inserts acquire the file insert_latch_ first and then mvcc_latch_ (via
    // prepare_insert); calling back into the file layer under mvcc_latch_
    // deadlocks (ABBA).
    std::mutex commit_apply_latch_;
    mutable std::mutex mvcc_latch_;
    std::condition_variable mvcc_cv_;
    std::unordered_map<RecordKey, std::vector<MvccVersion>, RecordKeyHash> record_versions_;
    std::unordered_map<uint64_t, std::unordered_set<RecordKey, RecordKeyHash>>
        mvcc_unique_conflict_keys_by_file_;
    std::unordered_map<txn_id_t, MvccTxnState> mvcc_txns_;
};
