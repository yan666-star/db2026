/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "transaction_manager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include "common/context.h"
#include "common/perf_counters.h"
#include "execution/execution_eval.h"
#include "record/rm_file_handle.h"
#include "system/sm_manager.h"

std::unordered_map<txn_id_t, Transaction *> TransactionManager::txn_map = {};

namespace {

struct PerfDiagStats {
    std::atomic<uint64_t> abort_total{0};
    std::atomic<uint64_t> abort_mvcc{0};
    std::atomic<uint64_t> abort_physical_rollback{0};
    std::atomic<uint64_t> abort_entered_mvcc_commit{0};
    std::atomic<uint64_t> abort_checkpoint_flush{0};
    std::atomic<uint64_t> abort_checkpoint_flush_skipped{0};
    std::atomic<uint64_t> abort_log_force_flush{0};
    std::atomic<uint64_t> abort_log_force_flush_skipped{0};
    std::atomic<uint64_t> write_conflict_pending{0};
    std::atomic<uint64_t> write_conflict_committed_after_start{0};
    std::atomic<uint64_t> unique_conflict_pending{0};
    std::atomic<uint64_t> unique_conflict_committed_after_start{0};
    std::atomic<uint64_t> prepare_conflict_pending{0};
    std::atomic<uint64_t> prepare_conflict_committed_after_start{0};
    std::atomic<uint64_t> commit_conflict_pending{0};
    std::atomic<uint64_t> commit_conflict_committed_after_start{0};
    std::atomic<uint64_t> pending_waits{0};
    std::atomic<uint64_t> pending_wait_resolved{0};
    std::atomic<uint64_t> pending_wait_resolved_committed{0};
    std::atomic<uint64_t> pending_wait_resolved_aborted{0};
    std::atomic<uint64_t> pending_wait_resolved_gone{0};
    std::atomic<uint64_t> pending_wait_timeout{0};
    std::atomic<uint64_t> abort_checkpoint_flush_us{0};
    std::atomic<uint64_t> abort_log_force_flush_us{0};
    std::atomic<uint64_t> si_admission_waits{0};
    std::atomic<uint64_t> si_admission_wait_us{0};
};

PerfDiagStats &perf_diag_stats() {
    static PerfDiagStats stats;
    return stats;
}

bool perf_diag_enabled() {
    static bool enabled = [] {
        const char *value = std::getenv("RMDB_PERF_DIAG");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

size_t si_max_active() {
    static size_t limit = [] {
        // 0 表示不施加人为并发上限。决赛排名固定 32 客户端，默认串行化
        // SI 会把正确的并发负载退化为单连接吞吐；仅在诊断热点冲突时才
        // 通过 RMDB_SI_MAX_ACTIVE 显式启用 admission control。
        constexpr size_t kDefaultLimit = 0;
        const char *value = std::getenv("RMDB_SI_MAX_ACTIVE");
        if (value == nullptr || value[0] == '\0') {
            return kDefaultLimit;
        }
        char *end = nullptr;
        unsigned long long parsed = std::strtoull(value, &end, 10);
        if (end == value || *end != '\0') {
            return kDefaultLimit;
        }
        return static_cast<size_t>(parsed);
    }();
    return limit;
}

void print_perf_diag() {
    if (!perf_diag_enabled()) {
        return;
    }
    static std::mutex print_latch;
    std::lock_guard<std::mutex> print_guard(print_latch);
    auto &s = perf_diag_stats();
    auto &shared = rmdb_perf::shared_counters();
    std::cerr << "RMDB_PERF_DIAG "
              << "abort_total=" << s.abort_total.load()
              << " abort_mvcc=" << s.abort_mvcc.load()
              << " abort_physical_rollback=" << s.abort_physical_rollback.load()
              << " abort_entered_mvcc_commit="
              << s.abort_entered_mvcc_commit.load()
              << " abort_checkpoint_flush=" << s.abort_checkpoint_flush.load()
              << " abort_checkpoint_flush_skipped="
              << s.abort_checkpoint_flush_skipped.load()
              << " abort_log_force_flush=" << s.abort_log_force_flush.load()
              << " abort_log_force_flush_skipped="
              << s.abort_log_force_flush_skipped.load()
              << " write_conflict_pending="
              << s.write_conflict_pending.load()
              << " write_conflict_committed_after_start="
              << s.write_conflict_committed_after_start.load()
              << " unique_conflict_pending="
              << s.unique_conflict_pending.load()
              << " unique_conflict_committed_after_start="
              << s.unique_conflict_committed_after_start.load()
              << " prepare_conflict_pending="
              << s.prepare_conflict_pending.load()
              << " prepare_conflict_committed_after_start="
              << s.prepare_conflict_committed_after_start.load()
              << " commit_conflict_pending="
              << s.commit_conflict_pending.load()
              << " commit_conflict_committed_after_start="
              << s.commit_conflict_committed_after_start.load()
              << " pending_waits=" << s.pending_waits.load()
              << " pending_wait_resolved="
              << s.pending_wait_resolved.load()
              << " pending_wait_resolved_committed="
              << s.pending_wait_resolved_committed.load()
              << " pending_wait_resolved_aborted="
              << s.pending_wait_resolved_aborted.load()
              << " pending_wait_resolved_gone="
              << s.pending_wait_resolved_gone.load()
              << " pending_wait_timeout="
              << s.pending_wait_timeout.load()
              << " abort_checkpoint_flush_us="
              << s.abort_checkpoint_flush_us.load()
              << " abort_log_force_flush_us="
              << s.abort_log_force_flush_us.load()
              << " si_admission_waits=" << s.si_admission_waits.load()
              << " si_admission_wait_us=" << s.si_admission_wait_us.load()
              << " buffer_fetches=" << shared.buffer_fetches.load()
              << " buffer_hits=" << shared.buffer_hits.load()
              << " buffer_misses=" << shared.buffer_misses.load()
              << " buffer_latch_acquires="
              << shared.buffer_latch_acquires.load()
              << " buffer_latch_wait_us="
              << shared.buffer_latch_wait_us.load()
              << " buffer_frame_latch_acquires="
              << shared.buffer_frame_latch_acquires.load()
              << " buffer_frame_latch_wait_us="
              << shared.buffer_frame_latch_wait_us.load()
              << " commit_apply_read_acquires="
              << shared.commit_apply_read_acquires.load()
              << " commit_apply_read_wait_us="
              << shared.commit_apply_read_wait_us.load()
              << " commit_apply_write_acquires="
              << shared.commit_apply_write_acquires.load()
              << " commit_apply_write_wait_us="
              << shared.commit_apply_write_wait_us.load()
              << " mvcc_latch_acquires="
              << shared.mvcc_latch_acquires.load()
              << " mvcc_latch_wait_us="
              << shared.mvcc_latch_wait_us.load()
              << std::endl;
}

void ensure_perf_diag_registered() {
    static bool registered = [] {
        if (perf_diag_enabled()) {
            std::atexit(print_perf_diag);
        }
        return true;
    }();
    (void)registered;
}

void add_perf_diag_us(std::atomic<uint64_t> &counter,
                      std::chrono::steady_clock::time_point start) {
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    counter.fetch_add(static_cast<uint64_t>(elapsed),
                      std::memory_order_relaxed);
}

uint64_t perf_diag_interval() {
    static uint64_t interval = [] {
        const char *value = std::getenv("RMDB_PERF_DIAG_INTERVAL");
        if (value == nullptr || value[0] == '\0') {
            return uint64_t{1000};
        }
        char *end = nullptr;
        unsigned long long parsed = std::strtoull(value, &end, 10);
        if (end == value || parsed == 0) {
            return uint64_t{1000};
        }
        return static_cast<uint64_t>(parsed);
    }();
    return interval;
}

void maybe_print_perf_diag(uint64_t abort_count) {
    if (!perf_diag_enabled()) {
        return;
    }
    uint64_t interval = perf_diag_interval();
    if (interval != 0 && abort_count % interval == 0) {
        print_perf_diag();
    }
}

std::chrono::microseconds pending_writer_wait_budget() {
    static auto budget = [] {
        const char *value = std::getenv("RMDB_PENDING_WAIT_US");
        if (value == nullptr || value[0] == '\0') {
            return std::chrono::microseconds(500);
        }
        char *end = nullptr;
        unsigned long long parsed = std::strtoull(value, &end, 10);
        if (end == value) {
            return std::chrono::microseconds(500);
        }
        return std::chrono::microseconds(parsed);
    }();
    return budget;
}

}  // namespace

static void clear_write_set(Transaction *txn) {
    auto write_set = txn->get_write_set();
    for (auto it = write_set->begin(); it != write_set->end();) {
        delete *it;
        it = write_set->erase(it);
    }
}

std::unique_lock<std::mutex>
TransactionManager::acquire_commit_apply_latch() {
    return rmdb_perf::lock_commit_apply_read(commit_apply_latch_);
}

Transaction *TransactionManager::begin(Transaction *txn, LogManager *log_manager,
                                       IsolationLevel isolation_level) {
    if (txn == nullptr) {
        txn_id_t txn_id = next_txn_id_.fetch_add(1);
        txn = new Transaction(txn_id, isolation_level);
        {
            std::lock_guard<std::mutex> lock(latch_);
            txn_map[txn_id] = txn;
        }
        if (txn->uses_mvcc()) {
            // Read the snapshot timestamp and register the transaction under
            // the same latch acquisition: otherwise GC could compute a
            // watermark that misses this transaction and prune versions its
            // snapshot still needs.
            auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
            timestamp_t start_ts = last_commit_ts_.load();
            txn->set_start_ts(start_ts);
            txn->set_read_ts(start_ts);
            auto &state = mvcc_txns_[txn_id];
            state.isolation_level = isolation_level;
            state.start_ts = start_ts;
        }
        {
            std::lock_guard<std::mutex> lock(checkpoint_latch_);
            active_txns_.insert(txn_id);
        }
        if (log_manager != nullptr) {
            BeginLogRecord begin_log(txn_id);
            lsn_t lsn = log_manager->add_log_to_buffer(&begin_log);
            txn->set_prev_lsn(lsn);
        }
    }
    txn->set_state(TransactionState::GROWING);
    return txn;
}

void TransactionManager::ensure_snapshot_admission(Transaction *txn) {
    if (txn == nullptr) {
        return;
    }
    if (!admit_snapshot_transaction(txn->get_transaction_id(),
                                    txn->get_isolation_level())) {
        return;
    }

    // Snapshot isolation establishes its snapshot on the first actual data
    // access, not merely when BEGIN is acknowledged. Transactions waiting in
    // the admission queue must not retain a timestamp that became stale while
    // earlier admitted transactions committed.
    auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
    timestamp_t start_ts = last_commit_ts_.load();
    txn->set_start_ts(start_ts);
    txn->set_read_ts(start_ts);
    auto state_it = mvcc_txns_.find(txn->get_transaction_id());
    if (state_it != mvcc_txns_.end()) {
        state_it->second.start_ts = start_ts;
    }
}

bool TransactionManager::admit_snapshot_transaction(
    txn_id_t txn_id, IsolationLevel isolation_level) {
    size_t limit = si_max_active();
    if (isolation_level != IsolationLevel::SNAPSHOT_ISOLATION || limit == 0) {
        return false;
    }

    auto wait_start = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(si_admission_latch_);
    if (admitted_snapshot_txns_.find(txn_id) !=
        admitted_snapshot_txns_.end()) {
        return false;
    }
    snapshot_admission_waiters_.push_back(txn_id);
    bool waited = admitted_snapshot_txns_.size() >= limit ||
                  snapshot_admission_waiters_.front() != txn_id;
    si_admission_cv_.wait(lock, [&] {
        return admitted_snapshot_txns_.size() < limit &&
               snapshot_admission_waiters_.front() == txn_id;
    });
    snapshot_admission_waiters_.pop_front();
    admitted_snapshot_txns_.insert(txn_id);
    si_admission_cv_.notify_all();
    if (waited && perf_diag_enabled()) {
        perf_diag_stats().si_admission_waits.fetch_add(
            1, std::memory_order_relaxed);
        add_perf_diag_us(perf_diag_stats().si_admission_wait_us, wait_start);
    }
    return true;
}

void TransactionManager::release_snapshot_admission(txn_id_t txn_id) {
    bool released = false;
    {
        std::lock_guard<std::mutex> lock(si_admission_latch_);
        released = admitted_snapshot_txns_.erase(txn_id) != 0;
    }
    if (released) {
        si_admission_cv_.notify_all();
    }
}

static void update_indexes(SmManager *sm_manager,
                           const std::string &table_name,
                           const RmRecord &old_record,
                           const RmRecord &new_record,
                           const Rid &rid, Transaction *txn) {
    auto &tab = sm_manager->db_.get_table(table_name);
    for (auto &index_meta : tab.indexes) {
        bool key_changed = false;
        for (const auto &col : index_meta.cols) {
            if (memcmp(old_record.data + col.offset,
                       new_record.data + col.offset, col.len) != 0) {
                key_changed = true;
                break;
            }
        }
        if (!key_changed) {
            continue;
        }
        auto index_name =
            sm_manager->get_ix_manager()->get_index_name(table_name,
                                                         index_meta.cols);
        auto index_handle = sm_manager->ihs_.at(index_name).get();
        std::vector<char> old_key(index_meta.col_tot_len);
        std::vector<char> new_key(index_meta.col_tot_len);
        int key_offset = 0;
        for (int i = 0; i < index_meta.col_num; ++i) {
            memcpy(old_key.data() + key_offset,
                   old_record.data + index_meta.cols[i].offset,
                   index_meta.cols[i].len);
            memcpy(new_key.data() + key_offset,
                   new_record.data + index_meta.cols[i].offset,
                   index_meta.cols[i].len);
            key_offset += index_meta.cols[i].len;
        }
        index_handle->delete_entry(old_key.data(), txn);
        index_handle->insert_entry(new_key.data(), rid, txn);
    }
}

static void delete_indexes(SmManager *sm_manager,
                           const std::string &table_name,
                           const RmRecord &record,
                           const Rid &rid, Transaction *txn) {
    auto &tab = sm_manager->db_.get_table(table_name);
    for (auto &index_meta : tab.indexes) {
        auto index_name =
            sm_manager->get_ix_manager()->get_index_name(table_name,
                                                         index_meta.cols);
        auto index_handle = sm_manager->ihs_.at(index_name).get();
        std::vector<char> key(index_meta.col_tot_len);
        int key_offset = 0;
        for (int i = 0; i < index_meta.col_num; ++i) {
            memcpy(key.data() + key_offset,
                   record.data + index_meta.cols[i].offset,
                   index_meta.cols[i].len);
            key_offset += index_meta.cols[i].len;
        }
        std::vector<Rid> indexed_rids;
        if (index_handle->get_value(key.data(), &indexed_rids, txn) &&
            std::find(indexed_rids.begin(), indexed_rids.end(), rid) !=
                indexed_rids.end()) {
            index_handle->delete_entry(key.data(), txn);
        }
    }
}

void TransactionManager::commit(Transaction *txn, LogManager *log_manager) {
    if (txn == nullptr || txn->get_state() == TransactionState::COMMITTED ||
        txn->get_state() == TransactionState::ABORTED) {
        return;
    }

    if (txn->uses_mvcc()) {
        try {
            commit_mvcc(txn);
        } catch (const TransactionAbortException &) {
            abort(txn, log_manager);
            throw;
        }
        if ((mvcc_commit_count_.fetch_add(1) & 0xFFu) == 0) {
            GarbageCollection();
        }
    }

    if (log_manager != nullptr) {
        CommitLogRecord commit_log(txn->get_transaction_id());
        commit_log.prev_lsn_ = txn->get_prev_lsn();
        lsn_t lsn = log_manager->add_log_to_buffer(&commit_log);
        txn->set_prev_lsn(lsn);
        log_manager->force_flush_up_to(lsn);
    }

    auto lock_set = *txn->get_lock_set();
    for (const auto &lock_id : lock_set) {
        lock_manager_->unlock(txn, lock_id);
    }
    txn->get_lock_set()->clear();
    clear_write_set(txn);
    txn->set_state(TransactionState::COMMITTED);
    finish_transaction(txn);
}

void TransactionManager::abort(Transaction *txn, LogManager *log_manager) {
    if (txn == nullptr || txn->get_state() == TransactionState::COMMITTED ||
        txn->get_state() == TransactionState::ABORTED) {
        return;
    }

    ensure_perf_diag_registered();
    if (perf_diag_enabled()) {
        auto &stats = perf_diag_stats();
        uint64_t abort_count =
            stats.abort_total.fetch_add(1, std::memory_order_relaxed) + 1;
        if (txn->uses_mvcc()) {
            stats.abort_mvcc.fetch_add(1, std::memory_order_relaxed);
        }
        maybe_print_perf_diag(abort_count);
    }

    Context context(lock_manager_, log_manager, txn);
    auto write_set = txn->get_write_set();
    bool did_physical_rollback = false;
    bool entered_mvcc_commit =
        txn->uses_mvcc() && txn->get_commit_ts() != INVALID_TS;
    if (perf_diag_enabled() && entered_mvcc_commit) {
        perf_diag_stats().abort_entered_mvcc_commit.fetch_add(
            1, std::memory_order_relaxed);
    }
    while (!write_set->empty()) {
        WriteRecord *write_record = write_set->back();
        if (!txn->uses_mvcc() ||
            (write_record->GetWriteType() != WType::UPDATE_TUPLE &&
             write_record->GetWriteType() != WType::DELETE_TUPLE)) {
            sm_manager_->rollback(write_record, &context);
            did_physical_rollback = true;
        }
        write_set->pop_back();
        delete write_record;
    }

    // UPDATE/DELETE are only pending versions before MVCC commit. If this
    // transaction never entered commit application and had no physical work
    // to roll back, the table and indexes still contain the pre-transaction
    // state. The ABORT record remains ordered in the log buffer, but it does
    // not need an individual fsync: if it is lost in a crash, recovery treats
    // the transaction as a loser and the already-correct physical state is
    // unchanged. INSERT/non-MVCC/commit-apply aborts retain the durable path.
    bool volatile_only_mvcc_abort =
        txn->uses_mvcc() && !did_physical_rollback && !entered_mvcc_commit;

    if (log_manager != nullptr) {
        if (!txn->uses_mvcc() || did_physical_rollback ||
            entered_mvcc_commit) {
            // Rollback operations are not represented by compensation log
            // records in this framework. Make the restored table/index state
            // durable before the ABORT record says recovery may skip this txn.
            if (perf_diag_enabled()) {
                perf_diag_stats().abort_physical_rollback.fetch_add(
                    did_physical_rollback ? 1 : 0, std::memory_order_relaxed);
                perf_diag_stats().abort_checkpoint_flush.fetch_add(
                    1, std::memory_order_relaxed);
            }
            auto flush_start = std::chrono::steady_clock::now();
            sm_manager_->flush_for_checkpoint();
            if (perf_diag_enabled()) {
                add_perf_diag_us(perf_diag_stats().abort_checkpoint_flush_us,
                                 flush_start);
            }
        } else if (perf_diag_enabled()) {
            perf_diag_stats().abort_checkpoint_flush_skipped.fetch_add(
                1, std::memory_order_relaxed);
        }
        AbortLogRecord abort_log(txn->get_transaction_id());
        abort_log.prev_lsn_ = txn->get_prev_lsn();
        lsn_t lsn = log_manager->add_log_to_buffer(&abort_log);
        txn->set_prev_lsn(lsn);
        if (volatile_only_mvcc_abort) {
            if (perf_diag_enabled()) {
                perf_diag_stats().abort_log_force_flush_skipped.fetch_add(
                    1, std::memory_order_relaxed);
            }
        } else {
            auto log_flush_start = std::chrono::steady_clock::now();
            log_manager->flush_log_to_disk(true);
            if (perf_diag_enabled()) {
                perf_diag_stats().abort_log_force_flush.fetch_add(
                    1, std::memory_order_relaxed);
                add_perf_diag_us(perf_diag_stats().abort_log_force_flush_us,
                                 log_flush_start);
            }
        }
    }

    auto lock_set = *txn->get_lock_set();
    for (const auto &lock_id : lock_set) {
        lock_manager_->unlock(txn, lock_id);
    }
    txn->get_lock_set()->clear();
    if (txn->uses_mvcc()) {
        abort_mvcc(txn);
    }
    txn->set_state(TransactionState::ABORTED);
    finish_transaction(txn);
}

namespace {

std::vector<char> copy_record(const RmRecord *record) {
    if (record == nullptr) {
        return {};
    }
    return std::vector<char>(record->data, record->data + record->size);
}

std::unique_ptr<RmRecord> make_record(const std::vector<char> &data) {
    if (data.empty()) {
        return nullptr;
    }
    return std::make_unique<RmRecord>(
        static_cast<int>(data.size()), const_cast<char *>(data.data()));
}

}

std::unique_ptr<RmRecord> TransactionManager::get_visible_record(
    Transaction *txn, uint64_t file_id, const Rid &rid,
    const RmRecord *physical_record) {
    if (!uses_mvcc(txn)) {
        return physical_record == nullptr
                   ? nullptr
                   : std::make_unique<RmRecord>(*physical_record);
    }

    auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
    auto owned_physical = physical_record == nullptr
                              ? nullptr
                              : std::make_unique<RmRecord>(*physical_record);
    return resolve_snapshot_record_under_latch(
        txn, file_id, rid, std::move(owned_physical));
}

std::unique_ptr<RmRecord>
TransactionManager::resolve_snapshot_record_under_latch(
    Transaction *txn, uint64_t file_id, const Rid &rid,
    std::unique_ptr<RmRecord> physical_record) {
    RecordKey key{file_id, rid};
    auto history_it = record_versions_.find(key);
    if (history_it == record_versions_.end()) {
        // Loaded / never-versioned rows are implicitly committed at ts=0.
        // A read does not need to materialize that baseline in the global
        // version map: prepare_write() creates it from old_record on the first
        // UPDATE/DELETE. Avoiding read-only entries keeps large scans from
        // permanently growing record_versions_ and every later GC pass.
        return physical_record;
    }

    const auto &history = history_it->second;
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->owner == txn->get_transaction_id() &&
            it->commit_ts == INVALID_TS) {
            return it->deleted ? nullptr : make_record(it->data);
        }
    }

    const MvccVersion *visible = nullptr;
    for (const auto &version : history) {
        if (version.commit_ts != INVALID_TS &&
            version.commit_ts <= txn->get_start_ts() &&
            (visible == nullptr || version.commit_ts > visible->commit_ts)) {
            visible = &version;
        }
    }
    if (visible == nullptr || visible->deleted) {
        return nullptr;
    }
    return make_record(visible->data);
}

std::unique_ptr<RmRecord> TransactionManager::get_latest_committed_record(
    uint64_t file_id, const Rid &rid, const RmRecord *physical_record) {
    auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
    auto owned_physical = physical_record == nullptr
                              ? nullptr
                              : std::make_unique<RmRecord>(*physical_record);
    return resolve_latest_record_under_latch(
        file_id, rid, std::move(owned_physical));
}

std::unique_ptr<RmRecord>
TransactionManager::resolve_latest_record_under_latch(
    uint64_t file_id, const Rid &rid,
    std::unique_ptr<RmRecord> physical_record) {
    RecordKey key{file_id, rid};
    auto history_it = record_versions_.find(key);
    if (history_it == record_versions_.end()) {
        return physical_record;
    }

    const MvccVersion *latest = nullptr;
    bool has_pending_insert = false;
    for (const auto &version : history_it->second) {
        if (version.commit_ts == INVALID_TS) {
            if (version.before_deleted && !version.deleted) {
                has_pending_insert = true;
            }
            continue;
        }
        if (latest == nullptr || version.commit_ts > latest->commit_ts) {
            latest = &version;
        }
    }

    // A committed delete hides the row regardless of the physical slot state
    // (MVCC delete removes index entries but may leave the physical record).
    if (latest != nullptr && latest->deleted) {
        return nullptr;
    }
    if (latest == nullptr && has_pending_insert) {
        return nullptr;
    }

    // The physical record is the authoritative latest-committed state: MVCC
    // commits and non-MVCC (READ COMMITTED) writes both land there, while the
    // version history is only updated by MVCC. Returning the version data here
    // would mask non-MVCC updates and cause lost updates once a row has an MVCC
    // history entry, so prefer the physical record.
    if (physical_record != nullptr) {
        return physical_record;
    }
    if (latest == nullptr) {
        return nullptr;
    }
    return make_record(latest->data);
}

void TransactionManager::filter_visible_records(
    Transaction *txn, uint64_t file_id, std::vector<Rid> &rids,
    std::vector<std::unique_ptr<RmRecord>> &records) {
    if (rids.size() != records.size()) {
        throw InternalError("RID/record batch size mismatch");
    }

    auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
    if (record_versions_.empty()) {
        // Recovery has already materialized the committed heap state and the
        // in-memory version directory starts empty after restart.  Every
        // occupied physical record is therefore visible, so avoid one failed
        // hash lookup per row during post-crash aggregate/partition scans.
        // The first MVCC version automatically restores the normal path.
        return;
    }
    const bool snapshot = uses_mvcc(txn);
    size_t visible_count = 0;
    for (size_t index = 0; index < records.size(); ++index) {
        std::unique_ptr<RmRecord> visible =
            snapshot
                ? resolve_snapshot_record_under_latch(
                      txn, file_id, rids[index], std::move(records[index]))
                : resolve_latest_record_under_latch(
                      file_id, rids[index], std::move(records[index]));
        if (visible == nullptr) {
            continue;
        }
        rids[visible_count] = rids[index];
        records[visible_count] = std::move(visible);
        visible_count++;
    }
    rids.resize(visible_count);
    records.resize(visible_count);
}

bool TransactionManager::predicate_matches(
    const ReadPredicate &predicate, const std::vector<char> &record) const {
    if (record.empty()) {
        return false;
    }
    RmRecord tuple(static_cast<int>(record.size()),
                   const_cast<char *>(record.data()));
    return eval_conditions(tuple, predicate.conditions, predicate.columns);
}

bool TransactionManager::predicate_affected(
    const ReadPredicate &predicate, const MvccVersion &version) const {
    bool before_matches =
        !version.before_deleted && predicate_matches(predicate, version.before);
    bool after_matches =
        !version.deleted && predicate_matches(predicate, version.data);
    return before_matches || after_matches;
}

bool TransactionManager::transactions_overlap(
    const MvccTxnState &left, const MvccTxnState &right) const {
    return (left.commit_ts == INVALID_TS || left.commit_ts > right.start_ts) &&
           (right.commit_ts == INVALID_TS || right.commit_ts > left.start_ts);
}

bool TransactionManager::add_rw_dependency(txn_id_t reader, txn_id_t writer) {
    if (reader == writer) {
        return false;
    }
    auto reader_it = mvcc_txns_.find(reader);
    auto writer_it = mvcc_txns_.find(writer);
    if (reader_it == mvcc_txns_.end() || writer_it == mvcc_txns_.end() ||
        reader_it->second.aborted || writer_it->second.aborted ||
        reader_it->second.isolation_level != IsolationLevel::SERIALIZABLE ||
        writer_it->second.isolation_level != IsolationLevel::SERIALIZABLE ||
        !transactions_overlap(reader_it->second, writer_it->second)) {
        return false;
    }
    bool inserted = reader_it->second.outgoing_rw.insert(writer).second;
    writer_it->second.incoming_rw.insert(reader);
    return inserted;
}

void TransactionManager::check_new_rw_dependency_or_abort(
    Transaction *current_txn, txn_id_t reader, txn_id_t writer) {
    if (!add_rw_dependency(reader, writer) ||
        !dependency_forms_dangerous_structure(reader, writer)) {
        return;
    }

    // 决赛 SSI 规范固定选择“当前语句所属事务”为 victim。这里不能改为
    // 中止 pivot、最年轻事务或尚未提交的其他事务，也不能把判定推迟到
    // COMMIT。调用方持有 mvcc_latch_，先标记再抛出；网络服务层捕获后会
    // 同步完成完整回滚，随后才能发送 TRANSACTION_ABORT。
    txn_id_t victim = current_txn->get_transaction_id();
    mark_mvcc_txn_aborted(victim);
    throw TransactionAbortException(victim,
                                    AbortReason::SERIALIZATION_FAILURE);
}

bool TransactionManager::dependency_forms_dangerous_structure(
    txn_id_t reader, txn_id_t writer) const {
    for (const auto &[pivot_id, pivot] : mvcc_txns_) {
        if (pivot.aborted) {
            continue;
        }
        for (txn_id_t tin_id : pivot.incoming_rw) {
            auto tin_it = mvcc_txns_.find(tin_id);
            if (tin_it == mvcc_txns_.end() || tin_it->second.aborted) {
                continue;
            }
            for (txn_id_t tout_id : pivot.outgoing_rw) {
                auto tout_it = mvcc_txns_.find(tout_id);
                if (tout_it == mvcc_txns_.end() || tout_it->second.aborted) {
                    continue;
                }
                bool contains_new_dependency =
                    (tin_id == reader && pivot_id == writer) ||
                    (pivot_id == reader && tout_id == writer);
                if (!contains_new_dependency) {
                    continue;
                }
                if (!transactions_overlap(tin_it->second, pivot) ||
                    !transactions_overlap(pivot, tout_it->second)) {
                    continue;
                }
                if (tin_id == tout_id ||
                    (tout_it->second.commit_ts != INVALID_TS &&
                     (tin_it->second.commit_ts == INVALID_TS ||
                      tout_it->second.commit_ts < tin_it->second.commit_ts))) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool TransactionManager::mvcc_txn_aborted(txn_id_t txn_id) const {
    auto it = mvcc_txns_.find(txn_id);
    return it != mvcc_txns_.end() && it->second.aborted;
}

void TransactionManager::mark_mvcc_txn_aborted(txn_id_t txn_id) {
    auto it = mvcc_txns_.find(txn_id);
    if (it != mvcc_txns_.end()) {
        it->second.aborted = true;
    }
    mvcc_cv_.notify_all();
}

bool TransactionManager::wait_for_pending_writer(
    Transaction *txn, txn_id_t writer, std::unique_lock<std::mutex> &lock) {
    if (txn == nullptr || writer == txn->get_transaction_id()) {
        return true;
    }
    auto budget = pending_writer_wait_budget();
    if (budget.count() <= 0) {
        return false;
    }
    if (perf_diag_enabled()) {
        perf_diag_stats().pending_waits.fetch_add(1, std::memory_order_relaxed);
    }
    bool resolved = mvcc_cv_.wait_for(lock, budget, [&] {
        auto writer_it = mvcc_txns_.find(writer);
        return writer_it == mvcc_txns_.end() || writer_it->second.aborted ||
               writer_it->second.commit_ts != INVALID_TS;
    });
    if (perf_diag_enabled()) {
        if (resolved) {
            perf_diag_stats().pending_wait_resolved.fetch_add(
                1, std::memory_order_relaxed);
            auto writer_it = mvcc_txns_.find(writer);
            if (writer_it == mvcc_txns_.end()) {
                perf_diag_stats().pending_wait_resolved_gone.fetch_add(
                    1, std::memory_order_relaxed);
            } else if (writer_it->second.aborted) {
                perf_diag_stats().pending_wait_resolved_aborted.fetch_add(
                    1, std::memory_order_relaxed);
            } else {
                perf_diag_stats().pending_wait_resolved_committed.fetch_add(
                    1, std::memory_order_relaxed);
            }
        } else {
            perf_diag_stats().pending_wait_timeout.fetch_add(
                1, std::memory_order_relaxed);
        }
    }
    return resolved;
}

void TransactionManager::check_physical_before(
    Transaction *txn, const std::string &table_name, const Rid &rid,
    const RmRecord *before_record) {
    auto fh_it = sm_manager_->fhs_.find(table_name);
    if (fh_it == sm_manager_->fhs_.end()) {
        return;
    }
    RmFileHandle *file_handle = fh_it->second.get();
    bool exists = file_handle->record_exists(rid);
    if (!exists) {
        if (before_record != nullptr) {
            mark_mvcc_txn_aborted(txn->get_transaction_id());
            throw TransactionAbortException(txn->get_transaction_id(),
                                            AbortReason::WRITE_CONFLICT);
        }
        return;
    }
    if (before_record == nullptr) {
        mark_mvcc_txn_aborted(txn->get_transaction_id());
        throw TransactionAbortException(txn->get_transaction_id(),
                                        AbortReason::WRITE_CONFLICT);
    }
    std::unique_ptr<RmRecord> physical;
    try {
        physical = file_handle->get_record(rid, nullptr);
    } catch (const RecordNotFoundError &) {
        mark_mvcc_txn_aborted(txn->get_transaction_id());
        throw TransactionAbortException(txn->get_transaction_id(),
                                        AbortReason::WRITE_CONFLICT);
    }
    int record_size = file_handle->get_file_hdr().record_size;
    if (before_record->size != record_size ||
        memcmp(physical->data, before_record->data, record_size) != 0) {
        mark_mvcc_txn_aborted(txn->get_transaction_id());
        throw TransactionAbortException(txn->get_transaction_id(),
                                        AbortReason::WRITE_CONFLICT);
    }
}

TransactionManager::RecordKey TransactionManager::make_record_key(
    const std::string &table_name, const Rid &rid) const {
    auto fh_it = sm_manager_->fhs_.find(table_name);
    if (fh_it == sm_manager_->fhs_.end()) {
        return RecordKey{0, rid};
    }
    return RecordKey{fh_it->second->GetMvccFileId(), rid};
}

WriteRecord *TransactionManager::first_mutating_write_record(
    Transaction *txn, const RecordKey &key) const {
    if (txn == nullptr) {
        return nullptr;
    }
    for (auto *write_record : *txn->get_write_set()) {
        if (write_record->GetWriteType() != WType::UPDATE_TUPLE &&
            write_record->GetWriteType() != WType::DELETE_TUPLE) {
            continue;
        }
        if (make_record_key(write_record->GetTableName(),
                            write_record->GetRid()) == key) {
            return write_record;
        }
    }
    return nullptr;
}

bool TransactionManager::write_record_is_insert_only(Transaction *txn,
                                                     const RecordKey &key) const {
    if (txn == nullptr) {
        return false;
    }
    bool has_write = false;
    for (auto *write_record : *txn->get_write_set()) {
        if (make_record_key(write_record->GetTableName(),
                            write_record->GetRid()) == key) {
            has_write = true;
            if (write_record->GetWriteType() == WType::UPDATE_TUPLE ||
                write_record->GetWriteType() == WType::DELETE_TUPLE) {
                return false;
            }
        }
    }
    return has_write;
}

bool TransactionManager::has_multiple_mutating_writes(
    Transaction *txn, const RecordKey &key) const {
    if (txn == nullptr) {
        return false;
    }
    int mutating = 0;
    for (auto *write_record : *txn->get_write_set()) {
        if (write_record->GetWriteType() != WType::UPDATE_TUPLE &&
            write_record->GetWriteType() != WType::DELETE_TUPLE) {
            continue;
        }
        if (make_record_key(write_record->GetTableName(),
                            write_record->GetRid()) == key &&
            ++mutating > 1) {
            return true;
        }
    }
    return false;
}

TransactionManager::MvccVersion *TransactionManager::find_own_pending_version(
    std::vector<MvccVersion> &history, txn_id_t txn_id) const {
    for (auto &version : history) {
        if (version.owner == txn_id && version.commit_ts == INVALID_TS) {
            return &version;
        }
    }
    return nullptr;
}

void TransactionManager::validate_pending_physical_before(Transaction *txn) {
    auto state_it = mvcc_txns_.find(txn->get_transaction_id());
    if (state_it == mvcc_txns_.end()) {
        return;
    }

    for (const auto &key : state_it->second.write_records) {
        if (write_record_is_insert_only(txn, key)) {
            continue;
        }

        WriteRecord *mutating = first_mutating_write_record(txn, key);
        if (has_multiple_mutating_writes(txn, key)) {
            if (mutating == nullptr) {
                mark_mvcc_txn_aborted(txn->get_transaction_id());
                throw TransactionAbortException(txn->get_transaction_id(),
                                                AbortReason::WRITE_CONFLICT);
            }
            check_physical_before(txn, mutating->GetTableName(), key.rid,
                                  &mutating->GetRecord());
            continue;
        }

        auto history_it = record_versions_.find(key);
        MvccVersion *own_pending = nullptr;
        if (history_it != record_versions_.end()) {
            own_pending = find_own_pending_version(history_it->second,
                                                   txn->get_transaction_id());
        }
        if (own_pending != nullptr && !own_pending->before.empty() &&
            !own_pending->table_name.empty()) {
            RmRecord before(static_cast<int>(own_pending->before.size()),
                            const_cast<char *>(own_pending->before.data()));
            check_physical_before(txn, own_pending->table_name, key.rid,
                                  &before);
            continue;
        }

        if (mutating != nullptr) {
            check_physical_before(txn, mutating->GetTableName(), key.rid,
                                  &mutating->GetRecord());
            continue;
        }

        mark_mvcc_txn_aborted(txn->get_transaction_id());
        throw TransactionAbortException(txn->get_transaction_id(),
                                        AbortReason::WRITE_CONFLICT);
    }
}

void TransactionManager::register_table_read(
    Transaction *txn, uint64_t file_id,
    const std::vector<Condition> &conditions,
    const std::vector<ColMeta> &columns) {
    if (txn == nullptr ||
        txn->get_isolation_level() != IsolationLevel::SERIALIZABLE) {
        return;
    }

    auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
    auto state_it = mvcc_txns_.find(txn->get_transaction_id());
    if (state_it == mvcc_txns_.end()) {
        return;
    }

    ReadPredicate predicate{file_id, conditions, columns};
    state_it->second.predicates.push_back(predicate);
    for (const auto &[key, history] : record_versions_) {
        if (key.file_id != file_id) {
            continue;
        }
        for (const auto &version : history) {
            if (version.owner == INVALID_TXN_ID ||
                version.owner == txn->get_transaction_id()) {
                continue;
            }
            if (version.commit_ts != INVALID_TS &&
                version.commit_ts <= txn->get_start_ts()) {
                continue;
            }
            if (predicate_affected(predicate, version)) {
                txn_id_t reader = txn->get_transaction_id();
                txn_id_t writer = version.owner;
                check_new_rw_dependency_or_abort(txn, reader, writer);
            }
        }
    }
}

void TransactionManager::register_record_read(
    Transaction *txn, uint64_t file_id, const Rid &rid) {
    if (txn == nullptr ||
        txn->get_isolation_level() != IsolationLevel::SERIALIZABLE) {
        return;
    }
    auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
    txn_id_t reader = txn->get_transaction_id();
    auto state_it = mvcc_txns_.find(reader);
    if (state_it == mvcc_txns_.end()) {
        return;
    }

    RecordKey key{file_id, rid};
    state_it->second.read_records.insert(key);

    // 点读也必须识别“本快照之后已经提交”以及“仍在执行”的写者。仅仅
    // 记录 read_records 会漏掉先写后读的历史，导致同一组并发事务因
    // 语句调度顺序不同而得到不同的 SSI 结果。
    auto history_it = record_versions_.find(key);
    if (history_it == record_versions_.end()) {
        return;
    }
    std::unordered_set<txn_id_t> checked_writers;
    for (const auto &version : history_it->second) {
        txn_id_t writer = version.owner;
        if (writer == INVALID_TXN_ID || writer == reader ||
            !checked_writers.insert(writer).second) {
            continue;
        }
        if (version.commit_ts != INVALID_TS &&
            version.commit_ts <= txn->get_start_ts()) {
            continue;
        }
        check_new_rw_dependency_or_abort(txn, reader, writer);
    }
}

void TransactionManager::prepare_insert(
    Transaction *txn, uint64_t file_id, const Rid &rid,
    const RmRecord &new_record) {
    prepare_write(txn, file_id, rid, nullptr, &new_record, false);
}

void TransactionManager::prepare_update(
    Transaction *txn, uint64_t file_id, const Rid &rid,
    const RmRecord &old_record, const RmRecord &new_record,
    const std::string &table_name) {
    prepare_write(txn, file_id, rid, &old_record, &new_record, false,
                  table_name);
}

void TransactionManager::prepare_delete(
    Transaction *txn, uint64_t file_id, const Rid &rid,
    const RmRecord &old_record, const std::string &table_name) {
    prepare_write(txn, file_id, rid, &old_record, nullptr, true,
                  table_name);
}

void TransactionManager::check_write_conflict(
    Transaction *txn, uint64_t file_id, const Rid &rid) {
    if (!uses_mvcc(txn)) {
        return;
    }

    auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
    RecordKey key{file_id, rid};
    while (true) {
        auto history_it = record_versions_.find(key);
        if (history_it == record_versions_.end()) {
            return;
        }

        timestamp_t latest_commit = 0;
        txn_id_t pending_owner = INVALID_TXN_ID;
        for (const auto &version : history_it->second) {
            if (version.commit_ts == INVALID_TS) {
                if (version.owner != txn->get_transaction_id()) {
                    if (mvcc_txn_aborted(version.owner)) {
                        continue;
                    }
                    pending_owner = version.owner;
                    break;
                }
            } else {
                latest_commit = std::max(latest_commit, version.commit_ts);
            }
        }

        if (pending_owner != INVALID_TXN_ID) {
            if (wait_for_pending_writer(txn, pending_owner, lock)) {
                continue;
            }
            if (perf_diag_enabled()) {
                perf_diag_stats().write_conflict_pending.fetch_add(
                    1, std::memory_order_relaxed);
            }
            mark_mvcc_txn_aborted(txn->get_transaction_id());
            throw TransactionAbortException(
                txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
        }

        if (latest_commit > txn->get_start_ts()) {
            if (perf_diag_enabled()) {
                perf_diag_stats().write_conflict_committed_after_start.fetch_add(
                    1, std::memory_order_relaxed);
            }
            mark_mvcc_txn_aborted(txn->get_transaction_id());
            throw TransactionAbortException(
                txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
        }
        return;
    }
}

bool TransactionManager::has_stale_write_target(
    Transaction *txn, uint64_t file_id,
    const std::function<bool(const RmRecord &)> &matches) {
    if (!uses_mvcc(txn)) {
        return false;
    }

    auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
    for (const auto &[key, history] : record_versions_) {
        if (key.file_id != file_id) {
            continue;
        }

        timestamp_t latest_commit = 0;
        const MvccVersion *snapshot_visible = nullptr;
        for (const auto &version : history) {
            if (version.commit_ts == INVALID_TS) {
                continue;
            }
            latest_commit = std::max(latest_commit, version.commit_ts);
            if (version.commit_ts <= txn->get_start_ts() &&
                (snapshot_visible == nullptr ||
                 version.commit_ts > snapshot_visible->commit_ts)) {
                snapshot_visible = &version;
            }
        }

        if (latest_commit <= txn->get_start_ts() ||
            snapshot_visible == nullptr || snapshot_visible->deleted ||
            snapshot_visible->data.empty()) {
            continue;
        }

        RmRecord record(static_cast<int>(snapshot_visible->data.size()));
        memcpy(record.data, snapshot_visible->data.data(),
               snapshot_visible->data.size());
        if (matches(record)) {
            return true;
        }
    }
    return false;
}

void TransactionManager::check_unique_key_conflict(
    Transaction *txn, uint64_t file_id, const Rid &target_rid,
    const RmRecord &new_record, const std::vector<ColMeta> &index_cols) {
    if (!uses_mvcc(txn)) {
        return;
    }

    auto same_key = [&](const std::vector<char> &data) {
        if (data.empty()) {
            return false;
        }
        for (const auto &col : index_cols) {
            if (memcmp(data.data() + col.offset,
                       new_record.data + col.offset, col.len) != 0) {
                return false;
            }
        }
        return true;
    };

    auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
    auto candidate_it = mvcc_unique_conflict_keys_by_file_.find(file_id);
    if (candidate_it == mvcc_unique_conflict_keys_by_file_.end()) {
        return;
    }

    // The full validation below can reject only an uncommitted version owned
    // by another writer, or a committed version newer than this snapshot. If
    // neither class can exist, walking every RID/history would produce the
    // same successful result. This is only a sufficient fast-path condition;
    // any uncertainty falls through to the unchanged candidate validation.
    bool has_other_pending_writer = false;
    for (const auto &[other_txn_id, state] : mvcc_txns_) {
        if (other_txn_id == txn->get_transaction_id() || state.aborted ||
            state.commit_ts != INVALID_TS || state.write_records.empty()) {
            continue;
        }
        has_other_pending_writer = true;
        break;
    }
    if (!has_other_pending_writer &&
        last_commit_ts_.load() <= txn->get_start_ts()) {
        return;
    }

    for (const auto &key : candidate_it->second) {
        if (key.rid == target_rid) {
            continue;
        }
        auto history_it = record_versions_.find(key);
        if (history_it == record_versions_.end()) {
            continue;
        }
        const auto &history = history_it->second;

        const MvccVersion *latest_committed = nullptr;
        for (const auto &version : history) {
            if (version.commit_ts == INVALID_TS) {
                if (version.owner != txn->get_transaction_id() &&
                    !mvcc_txn_aborted(version.owner) &&
                    !version.deleted && same_key(version.data)) {
                    if (perf_diag_enabled()) {
                        perf_diag_stats().unique_conflict_pending.fetch_add(
                            1, std::memory_order_relaxed);
                    }
                    mark_mvcc_txn_aborted(txn->get_transaction_id());
                    throw TransactionAbortException(
                        txn->get_transaction_id(),
                        AbortReason::WRITE_CONFLICT);
                }
                continue;
            }
            if (latest_committed == nullptr ||
                version.commit_ts > latest_committed->commit_ts) {
                latest_committed = &version;
            }
        }

        if (latest_committed != nullptr &&
            latest_committed->commit_ts > txn->get_start_ts() &&
            !latest_committed->deleted &&
            same_key(latest_committed->data)) {
            if (perf_diag_enabled()) {
                perf_diag_stats()
                    .unique_conflict_committed_after_start.fetch_add(
                        1, std::memory_order_relaxed);
            }
            mark_mvcc_txn_aborted(txn->get_transaction_id());
            throw TransactionAbortException(
                txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
        }
    }
}

void TransactionManager::prepare_write(
    Transaction *txn, uint64_t file_id, const Rid &rid,
    const RmRecord *old_record, const RmRecord *new_record, bool deleted,
    const std::string &table_name) {
    if (!uses_mvcc(txn)) {
        return;
    }

    auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
    RecordKey key{file_id, rid};

    MvccVersion *own_pending = nullptr;
    while (true) {
        auto &history = record_versions_[key];
        // Mark the chain before creating a baseline or running validation that
        // may throw.  That keeps the worklist complete even on conflict paths.
        gc_dirty_keys_.insert(key);
        if (history.empty() && old_record != nullptr) {
            MvccVersion baseline;
            baseline.commit_ts = 0;
            baseline.deleted = false;
            baseline.data = copy_record(old_record);
            history.push_back(std::move(baseline));
        }

        own_pending = nullptr;
        timestamp_t latest_commit = 0;
        txn_id_t pending_owner = INVALID_TXN_ID;
        for (auto &version : history) {
            if (version.commit_ts == INVALID_TS) {
                if (version.owner != txn->get_transaction_id()) {
                    if (mvcc_txn_aborted(version.owner)) {
                        continue;
                    }
                    pending_owner = version.owner;
                    break;
                }
                own_pending = &version;
            } else {
                latest_commit = std::max(latest_commit, version.commit_ts);
            }
        }

        if (pending_owner != INVALID_TXN_ID) {
            if (wait_for_pending_writer(txn, pending_owner, lock)) {
                continue;
            }
            if (perf_diag_enabled()) {
                perf_diag_stats().prepare_conflict_pending.fetch_add(
                    1, std::memory_order_relaxed);
            }
            mark_mvcc_txn_aborted(txn->get_transaction_id());
            throw TransactionAbortException(
                txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
        }

        if (own_pending == nullptr && latest_commit > txn->get_start_ts()) {
            if (perf_diag_enabled()) {
                perf_diag_stats()
                    .prepare_conflict_committed_after_start.fetch_add(
                        1, std::memory_order_relaxed);
            }
            mark_mvcc_txn_aborted(txn->get_transaction_id());
            throw TransactionAbortException(
                txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
        }
        break;
    }

    auto &history = record_versions_[key];
    if (own_pending == nullptr && old_record != nullptr &&
        !table_name.empty()) {
        check_physical_before(txn, table_name, rid, old_record);
    }

    // 先在栈上构造本次写入将产生的版本，用它进行 SSI 谓词/记录冲突
    // 判断。危险结构检查必须早于 pending version 的安装或修改。
    MvccVersion prospective;
    if (own_pending == nullptr) {
        prospective.owner = txn->get_transaction_id();
        prospective.before_deleted = old_record == nullptr;
        prospective.before = copy_record(old_record);
        prospective.deleted = deleted;
        prospective.data = copy_record(new_record);
        prospective.table_name = table_name;
    } else {
        prospective = *own_pending;
        if (prospective.before_deleted && prospective.before.empty() &&
            !prospective.deleted && old_record != nullptr) {
            prospective.before = prospective.data;
        }
        prospective.deleted = deleted;
        prospective.data = copy_record(new_record);
        if (!table_name.empty()) {
            prospective.table_name = table_name;
        }
    }

    if (txn->get_isolation_level() == IsolationLevel::SERIALIZABLE) {
        for (auto &[reader_id, reader] : mvcc_txns_) {
            if (reader_id == txn->get_transaction_id() || reader.aborted ||
                reader.isolation_level != IsolationLevel::SERIALIZABLE ||
                (reader.commit_ts != INVALID_TS &&
                 reader.commit_ts <= txn->get_start_ts())) {
                continue;
            }

            bool affected = reader.read_records.count(key) != 0;
            if (!affected) {
                for (const auto &predicate : reader.predicates) {
                    if (predicate.file_id == file_id &&
                        predicate_affected(predicate, prospective)) {
                        affected = true;
                        break;
                    }
                }
            }
            if (affected) {
                txn_id_t writer_id = txn->get_transaction_id();
                check_new_rw_dependency_or_abort(txn, reader_id, writer_id);
            }
        }
    }

    if (own_pending == nullptr) {
        history.push_back(std::move(prospective));
        own_pending = &history.back();
    } else {
        *own_pending = std::move(prospective);
    }

    auto state_it = mvcc_txns_.find(txn->get_transaction_id());
    if (state_it != mvcc_txns_.end()) {
        state_it->second.write_records.insert(key);
    }
    mvcc_unique_conflict_keys_by_file_[file_id].insert(key);
}

void TransactionManager::check_commit_conflict_under_latch(Transaction *txn) {
    if (!uses_mvcc(txn)) {
        return;
    }

    auto state_it = mvcc_txns_.find(txn->get_transaction_id());
    if (state_it == mvcc_txns_.end()) {
        return;
    }

    for (const auto &key : state_it->second.write_records) {
        auto history_it = record_versions_.find(key);
        if (history_it == record_versions_.end()) {
            if (write_record_is_insert_only(txn, key)) {
                continue;
            }
            WriteRecord *mutating = first_mutating_write_record(txn, key);
            if (mutating != nullptr) {
                check_physical_before(txn, mutating->GetTableName(), key.rid,
                                      &mutating->GetRecord());
            } else {
                mark_mvcc_txn_aborted(txn->get_transaction_id());
                throw TransactionAbortException(txn->get_transaction_id(),
                                                AbortReason::WRITE_CONFLICT);
            }
            continue;
        }
        for (const auto &version : history_it->second) {
            if (version.commit_ts == INVALID_TS) {
                if (version.owner != txn->get_transaction_id() &&
                    !mvcc_txn_aborted(version.owner)) {
                    if (perf_diag_enabled()) {
                        perf_diag_stats().commit_conflict_pending.fetch_add(
                            1, std::memory_order_relaxed);
                    }
                    mark_mvcc_txn_aborted(txn->get_transaction_id());
                    throw TransactionAbortException(
                        txn->get_transaction_id(),
                        AbortReason::WRITE_CONFLICT);
                }
            } else if (version.commit_ts > txn->get_start_ts() &&
                       version.owner != txn->get_transaction_id()) {
                if (perf_diag_enabled()) {
                    perf_diag_stats()
                        .commit_conflict_committed_after_start.fetch_add(
                            1, std::memory_order_relaxed);
                }
                mark_mvcc_txn_aborted(txn->get_transaction_id());
                throw TransactionAbortException(
                    txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
            }
        }
    }

    validate_pending_physical_before(txn);
}

void TransactionManager::check_commit_conflict(Transaction *txn) {
    if (!uses_mvcc(txn)) {
        return;
    }

    auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
    check_commit_conflict_under_latch(txn);
}

void TransactionManager::commit_mvcc(Transaction *txn) {
    if (!uses_mvcc(txn)) {
        return;
    }

    struct PhysicalOp {
        bool is_delete;
        std::string table_name;
        Rid rid;
        std::vector<char> before;
        std::vector<char> after;
    };
    std::vector<PhysicalOp> ops;

    // commit_apply_latch_ serializes commit application so that two commits
    // touching the same rid land in commit_ts order, while mvcc_latch_ stays a
    // leaf lock (no file/index calls under it — see lock-ordering note in the
    // header). Deadlock otherwise: inserts hold the file insert_latch_ and then
    // take mvcc_latch_ via prepare_insert, while the old code held mvcc_latch_
    // and took insert_latch_ via update_record.
    auto apply_lock =
        rmdb_perf::lock_commit_apply_write(commit_apply_latch_);
    {
        auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
        check_commit_conflict_under_latch(txn);
        timestamp_t commit_ts = last_commit_ts_.fetch_add(1) + 1;
        txn->set_commit_ts(commit_ts);
        auto state_it = mvcc_txns_.find(txn->get_transaction_id());
        if (state_it != mvcc_txns_.end()) {
            state_it->second.commit_ts = commit_ts;
            for (const auto &key : state_it->second.write_records) {
                auto history_it = record_versions_.find(key);
                MvccVersion *own_pending = nullptr;
                if (history_it != record_versions_.end()) {
                    own_pending = find_own_pending_version(
                        history_it->second, txn->get_transaction_id());
                }
                if (own_pending == nullptr) {
                    if (write_record_is_insert_only(txn, key)) {
                        continue;
                    }
                    mark_mvcc_txn_aborted(txn->get_transaction_id());
                    throw TransactionAbortException(
                        txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
                }

                if (write_record_is_insert_only(txn, key)) {
                    own_pending->commit_ts = commit_ts;
                    continue;
                }

                std::vector<char> before = own_pending->before;
                if (has_multiple_mutating_writes(txn, key)) {
                    WriteRecord *mutating =
                        first_mutating_write_record(txn, key);
                    if (mutating != nullptr && mutating->GetRecord().size > 0) {
                        before = copy_record(&mutating->GetRecord());
                    }
                } else if (before.empty()) {
                    WriteRecord *mutating =
                        first_mutating_write_record(txn, key);
                    if (mutating != nullptr && mutating->GetRecord().size > 0) {
                        before = copy_record(&mutating->GetRecord());
                    }
                }
                std::string table_name = own_pending->table_name;
                if (table_name.empty()) {
                    WriteRecord *mutating =
                        first_mutating_write_record(txn, key);
                    if (mutating != nullptr) {
                        table_name = mutating->GetTableName();
                    }
                }
                if (before.empty() || table_name.empty()) {
                    mark_mvcc_txn_aborted(txn->get_transaction_id());
                    throw TransactionAbortException(
                        txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
                }
                if (!own_pending->deleted && own_pending->data.empty()) {
                    mark_mvcc_txn_aborted(txn->get_transaction_id());
                    throw TransactionAbortException(
                        txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
                }
                ops.push_back(PhysicalOp{own_pending->deleted, table_name,
                                         key.rid, before, own_pending->data});
                own_pending->commit_ts = commit_ts;
            }
        }
    }
    mvcc_cv_.notify_all();

    for (size_t op_index = 0; op_index < ops.size(); ++op_index) {
        auto &op = ops[op_index];
        RmRecord before(static_cast<int>(op.before.size()),
                        const_cast<char *>(op.before.data()));
        try {
            check_physical_before(txn, op.table_name, op.rid, &before);
            if (op.is_delete) {
                delete_indexes(sm_manager_, op.table_name, before, op.rid, txn);
                auto file_handle = sm_manager_->fhs_.at(op.table_name).get();
                if (file_handle->record_exists(op.rid)) {
                    file_handle->delete_record(op.rid, nullptr);
                }
            } else {
                auto file_handle = sm_manager_->fhs_.at(op.table_name).get();
                RmRecord after(static_cast<int>(op.after.size()),
                               const_cast<char *>(op.after.data()));
                file_handle->update_record(op.rid, after.data, nullptr);
                update_indexes(sm_manager_, op.table_name, before, after, op.rid,
                               txn);
            }
        } catch (const TransactionAbortException &) {
            Context rollback_context(lock_manager_, nullptr, txn);
            for (size_t rollback_index = op_index; rollback_index > 0;
                 --rollback_index) {
                const auto &applied = ops[rollback_index - 1];
                RmRecord applied_before(
                    static_cast<int>(applied.before.size()),
                    const_cast<char *>(applied.before.data()));
                Rid applied_rid = applied.rid;
                if (applied.is_delete) {
                    sm_manager_->rollback_delete(applied.table_name,
                                                 applied_rid, applied_before,
                                                 &rollback_context);
                } else {
                    sm_manager_->rollback_update(applied.table_name,
                                                 applied_rid, applied_before,
                                                 &rollback_context);
                }
            }
            mark_mvcc_txn_aborted(txn->get_transaction_id());
            throw;
        }
    }
}

void TransactionManager::remove_dependencies(txn_id_t txn_id) {
    auto state_it = mvcc_txns_.find(txn_id);
    if (state_it == mvcc_txns_.end()) {
        return;
    }
    for (txn_id_t incoming : state_it->second.incoming_rw) {
        auto it = mvcc_txns_.find(incoming);
        if (it != mvcc_txns_.end()) {
            it->second.outgoing_rw.erase(txn_id);
        }
    }
    for (txn_id_t outgoing : state_it->second.outgoing_rw) {
        auto it = mvcc_txns_.find(outgoing);
        if (it != mvcc_txns_.end()) {
            it->second.incoming_rw.erase(txn_id);
        }
    }
    state_it->second.incoming_rw.clear();
    state_it->second.outgoing_rw.clear();
}

void TransactionManager::abort_mvcc(Transaction *txn) {
    if (!uses_mvcc(txn)) {
        return;
    }
    auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
    auto state_it = mvcc_txns_.find(txn->get_transaction_id());
    if (state_it == mvcc_txns_.end()) {
        return;
    }
    timestamp_t partial_commit_ts = txn->get_commit_ts();
    for (const auto &key : state_it->second.write_records) {
        auto history_it = record_versions_.find(key);
        if (history_it == record_versions_.end()) {
            continue;
        }
        auto &history = history_it->second;
        history.erase(
            std::remove_if(history.begin(), history.end(), [&](const MvccVersion &version) {
                return version.owner == txn->get_transaction_id() &&
                       (version.commit_ts == INVALID_TS ||
                        (partial_commit_ts != INVALID_TS &&
                         version.commit_ts == partial_commit_ts));
            }),
            history.end());
        if (history.empty()) {
            auto candidate_it =
                mvcc_unique_conflict_keys_by_file_.find(key.file_id);
            if (candidate_it != mvcc_unique_conflict_keys_by_file_.end()) {
                candidate_it->second.erase(key);
                if (candidate_it->second.empty()) {
                    mvcc_unique_conflict_keys_by_file_.erase(candidate_it);
                }
            }
            record_versions_.erase(history_it);
        } else if (history.size() == 1 && history.front().commit_ts == 0 &&
                   !history.front().deleted) {
            auto candidate_it =
                mvcc_unique_conflict_keys_by_file_.find(key.file_id);
            if (candidate_it != mvcc_unique_conflict_keys_by_file_.end()) {
                candidate_it->second.erase(key);
                if (candidate_it->second.empty()) {
                    mvcc_unique_conflict_keys_by_file_.erase(candidate_it);
                }
            }
        }
    }
    remove_dependencies(txn->get_transaction_id());
    state_it->second.aborted = true;
    state_it->second.predicates.clear();
    state_it->second.read_records.clear();
    state_it->second.write_records.clear();
    // Pending versions of this transaction have now been removed and its
    // dependency edges cleared, so its bookkeeping entry can be reclaimed by GC.
    state_it->second.cleanup_done = true;
    mvcc_cv_.notify_all();
}

timestamp_t TransactionManager::GetWatermark() {
    auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
    timestamp_t watermark = last_commit_ts_.load();
    for (const auto &[txn_id, state] : mvcc_txns_) {
        (void)txn_id;
        if (state.aborted || state.commit_ts != INVALID_TS) {
            continue;  // only in-flight transactions hold back the watermark
        }
        watermark = std::min(watermark, state.start_ts);
    }
    return watermark;
}

void TransactionManager::GarbageCollection() {
    // Same lock ordering as commit_mvcc.  GC only compacts in-memory version
    // metadata; committed DELETEs have already been applied to the heap before
    // commit_mvcc() returns.
    auto apply_lock =
        rmdb_perf::lock_commit_apply_write(commit_apply_latch_);
    {
        auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);

        // The watermark is the smallest read timestamp of any in-flight MVCC
        // transaction. Any committed version older than the watermark can never
        // be observed again, so it is safe to reclaim.
        timestamp_t watermark = last_commit_ts_.load();
        for (const auto &[txn_id, state] : mvcc_txns_) {
            (void)txn_id;
            if (state.aborted || state.commit_ts != INVALID_TS) {
                continue;
            }
            watermark = std::min(watermark, state.start_ts);
        }

        for (auto dirty_it = gc_dirty_keys_.begin();
             dirty_it != gc_dirty_keys_.end();) {
            RecordKey candidate_key = *dirty_it;
            auto current_dirty_it = dirty_it++;
            auto it = record_versions_.find(candidate_key);
            if (it == record_versions_.end()) {
                gc_dirty_keys_.erase(current_dirty_it);
                continue;
            }
            auto &history = it->second;

            // Find the newest committed version visible at the watermark. It is
            // the baseline the oldest possible reader would observe; anything
            // strictly older than it is dead.
            timestamp_t baseline_ts = INVALID_TS;
            for (const auto &version : history) {
                if (version.commit_ts != INVALID_TS &&
                    version.commit_ts <= watermark &&
                    (baseline_ts == INVALID_TS ||
                     version.commit_ts > baseline_ts)) {
                    baseline_ts = version.commit_ts;
                }
            }
            if (baseline_ts != INVALID_TS) {
                history.erase(
                    std::remove_if(history.begin(), history.end(),
                                   [&](const MvccVersion &version) {
                                       return version.commit_ts != INVALID_TS &&
                                              version.commit_ts < baseline_ts;
                                   }),
                    history.end());
            }

            bool keep_dirty = false;
            if (history.size() == 1) {
                MvccVersion &only = history.front();
                if (only.commit_ts != INVALID_TS &&
                    only.commit_ts <= watermark) {
                    // Once a version is settled below the watermark it can no
                    // longer conflict with a future unique-key write.  In
                    // particular, committed tombstones were previously kept in
                    // this candidate set forever even though
                    // check_unique_key_conflict() explicitly ignores deleted
                    // versions.  Delivery-style delete/insert workloads then
                    // paid an ever-growing linear scan for every insert.
                    auto candidate_it =
                        mvcc_unique_conflict_keys_by_file_.find(
                            it->first.file_id);
                    if (candidate_it !=
                        mvcc_unique_conflict_keys_by_file_.end()) {
                        candidate_it->second.erase(it->first);
                        if (candidate_it->second.empty()) {
                            mvcc_unique_conflict_keys_by_file_.erase(
                                candidate_it);
                        }
                    }
                    // Keep the newest committed version so snapshot readers and
                    // deferred MVCC writers can still resolve visibility after
                    // physical apply; erasing the chain causes silent UPDATE
                    // skips once last_commit_ts advances.
                    if (only.deleted) {
                        // The heap row is already gone.  Retain the tombstone's
                        // timestamp while the chain exists so a copied/reused
                        // physical slot cannot resurrect for an older snapshot,
                        // but its payload and table metadata are no longer used.
                        only.table_name.clear();
                        only.before.clear();
                        only.data.clear();
                    }
                } else {
                    keep_dirty = true;
                }
            } else if (!history.empty()) {
                // Pending or multiple committed versions need another pass
                // after the watermark or writer state advances.
                keep_dirty = true;
            }
            if (!keep_dirty) {
                gc_dirty_keys_.erase(current_dirty_it);
            }
        }

        // Prune bookkeeping for transactions that can no longer participate in
        // any conflict check: committed ones below the watermark, and aborted
        // ones whose pending versions and dependency edges are already gone.
        for (auto it = mvcc_txns_.begin(); it != mvcc_txns_.end();) {
            const auto &state = it->second;
            bool committed_settled =
                state.commit_ts != INVALID_TS && state.commit_ts <= watermark;
            bool aborted_settled = state.aborted && state.cleanup_done;
            if (committed_settled || aborted_settled) {
                it = mvcc_txns_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void TransactionManager::apply_committed_deletes_for_checkpoint() {
    struct Reclaim {
        std::string table_name;
        Rid rid;
    };
    std::vector<Reclaim> reclaims;

    auto apply_lock =
        rmdb_perf::lock_commit_apply_write(commit_apply_latch_);
    {
        auto lock = rmdb_perf::lock_mvcc(mvcc_latch_);
        for (auto it = record_versions_.begin();
             it != record_versions_.end();) {
            const auto &history = it->second;
            const MvccVersion *latest = nullptr;
            bool has_pending = false;
            for (const auto &version : history) {
                if (version.commit_ts == INVALID_TS) {
                    has_pending = true;
                    break;
                }
                if (latest == nullptr ||
                    version.commit_ts > latest->commit_ts) {
                    latest = &version;
                }
            }
            // begin_static_checkpoint() has quiesced every transaction, so
            // pending versions should not exist; keep such chains untouched
            // out of caution.
            if (has_pending || latest == nullptr || !latest->deleted) {
                ++it;
                continue;
            }
            if (!latest->table_name.empty()) {
                reclaims.push_back(Reclaim{latest->table_name, it->first.rid});
            }
            // The physical state now becomes authoritative (row absent), so
            // the tombstone chain is no longer needed by any future snapshot.
            auto candidate_it =
                mvcc_unique_conflict_keys_by_file_.find(it->first.file_id);
            if (candidate_it != mvcc_unique_conflict_keys_by_file_.end()) {
                candidate_it->second.erase(it->first);
                if (candidate_it->second.empty()) {
                    mvcc_unique_conflict_keys_by_file_.erase(candidate_it);
                }
            }
            it = record_versions_.erase(it);
        }
    }

    for (const auto &reclaim : reclaims) {
        auto fh_it = sm_manager_->fhs_.find(reclaim.table_name);
        if (fh_it == sm_manager_->fhs_.end()) {
            continue;
        }
        RmFileHandle *file_handle = fh_it->second.get();
        if (file_handle->record_exists(reclaim.rid)) {
            file_handle->delete_record(reclaim.rid, nullptr);
        }
    }
}

void TransactionManager::finish_transaction(Transaction *txn) {
    {
        std::lock_guard<std::mutex> lock(checkpoint_latch_);
        active_txns_.erase(txn->get_transaction_id());
    }
    checkpoint_cv_.notify_all();
    release_snapshot_admission(txn->get_transaction_id());
}

void TransactionManager::release_transaction(Transaction *txn) {
    if (txn == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(latch_);
        auto it = txn_map.find(txn->get_transaction_id());
        if (it != txn_map.end() && it->second == txn) {
            txn_map.erase(it);
        }
    }
    delete txn;
}

void TransactionManager::enter_statement(txn_id_t txn_id) {
    std::unique_lock<std::mutex> lock(checkpoint_latch_);
    checkpoint_cv_.wait(lock, [this, txn_id] {
        return !checkpoint_in_progress_ ||
               (txn_id != INVALID_TXN_ID &&
                active_txns_.find(txn_id) != active_txns_.end());
    });
    active_statements_++;
}

void TransactionManager::leave_statement() {
    {
        std::lock_guard<std::mutex> lock(checkpoint_latch_);
        if (active_statements_ > 0) {
            active_statements_--;
        }
    }
    checkpoint_cv_.notify_all();
}

std::vector<txn_id_t> TransactionManager::begin_static_checkpoint() {
    checkpoint_serial_latch_.lock();
    std::unique_lock<std::mutex> lock(checkpoint_latch_);
    checkpoint_in_progress_ = true;
    checkpoint_cv_.wait(lock, [this] {
        return active_statements_ == 0 && active_txns_.empty();
    });

    try {
        return {};
    } catch (...) {
        checkpoint_in_progress_ = false;
        lock.unlock();
        checkpoint_cv_.notify_all();
        checkpoint_serial_latch_.unlock();
        throw;
    }
}

void TransactionManager::end_static_checkpoint() {
    {
        std::lock_guard<std::mutex> lock(checkpoint_latch_);
        checkpoint_in_progress_ = false;
    }
    checkpoint_cv_.notify_all();
    checkpoint_serial_latch_.unlock();
}
