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
#include "transaction/storage_commit_executor.h"

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
    rmdb_perf::write_report(std::cerr);
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

}  // namespace

static void clear_write_set(Transaction *txn) {
    auto write_set = txn->get_write_set();
    for (auto it = write_set->begin(); it != write_set->end();) {
        delete *it;
        it = write_set->erase(it);
    }
}

Transaction *TransactionManager::begin(Transaction *txn, LogManager *log_manager,
                                       IsolationLevel isolation_level) {
    ensure_perf_diag_registered();
    if (txn == nullptr) {
        txn_id_t txn_id = next_txn_id_.fetch_add(1);
        txn = new Transaction(txn_id, isolation_level);
        {
            std::lock_guard<std::mutex> lock(latch_);
            txn_map[txn_id] = txn;
        }
        txn_registry_.attach(txn->get_control());
        if (txn->uses_mvcc()) {
            const timestamp_t start_ts = txn_registry_.current_commit_ts();
            txn->set_start_ts(start_ts);
            txn->set_read_ts(start_ts);
            if (isolation_level == IsolationLevel::SERIALIZABLE) {
                std::lock_guard<std::mutex> lock(serializable_state_latch_);
                auto &state = mvcc_txns_[txn_id];
                state.isolation_level = isolation_level;
                state.start_ts = start_ts;
            }
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
    const timestamp_t start_ts = txn_registry_.current_commit_ts();
    txn->set_start_ts(start_ts);
    txn->set_read_ts(start_ts);
    if (txn->get_isolation_level() == IsolationLevel::SERIALIZABLE) {
        std::lock_guard<std::mutex> lock(serializable_state_latch_);
        auto state_it = mvcc_txns_.find(txn->get_transaction_id());
        if (state_it != mvcc_txns_.end()) {
            state_it->second.start_ts = start_ts;
        }
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

void TransactionManager::commit(Transaction *txn, LogManager *log_manager) {
    if (txn == nullptr || txn->get_state() == TransactionState::COMMITTED ||
        txn->get_state() == TransactionState::ABORTED) {
        return;
    }

    if (txn->uses_mvcc()) {
        try {
            commit_mvcc(txn, log_manager);
        } catch (const TransactionAbortException &) {
            abort(txn, log_manager);
            throw;
        }
        release_unique_key_intents(txn);
        const uint64_t commit_count =
            mvcc_commit_count_.fetch_add(1, std::memory_order_relaxed) + 1;
        if ((commit_count & 0x00FFu) == 0) {
            garbage_collect_incremental();
        }
        maybe_fail_commit(CommitFailurePoint::BEFORE_ACK);
    }

    if (!txn->uses_mvcc() && log_manager != nullptr) {
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
    bool entered_mvcc_commit = mvcc_txn_entered_apply(txn);
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
        txn->write_batch().discard();
        abort_mvcc(txn);
        release_unique_key_intents(txn);
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
    std::unique_ptr<RmRecord> physical_record) {
    if (!uses_mvcc(txn)) {
        return physical_record;
    }

    std::vector<char> overlay;
    const OverlayKind overlay_kind =
        txn->write_batch().lookup(file_id, rid, &overlay);
    if (overlay_kind == OverlayKind::DELETED) {
        return nullptr;
    }
    if (overlay_kind == OverlayKind::VALUE) {
        return make_record(overlay);
    }

    RecordKey key{file_id, rid};
    auto &shard = mvcc_store_.shards_[get_shard_idx(key)];
    auto shard_lock = rmdb_perf::lock_mvcc(shard.latch);
    return resolve_snapshot_record_under_latch(
        txn, file_id, rid, std::move(physical_record));
}

std::unique_ptr<RmRecord>
TransactionManager::resolve_snapshot_record_under_latch(
    Transaction *txn, uint64_t file_id, const Rid &rid,
    std::unique_ptr<RmRecord> physical_record) {
    RecordKey key{file_id, rid};
    auto &shard = mvcc_store_.shards_[get_shard_idx(key)];
    auto history_it = shard.record_versions.find(key);
    if (history_it == shard.record_versions.end()) {
        // Loaded / never-versioned rows are implicitly committed at ts=0.
        return physical_record;
    }

    const auto &history = history_it->second;
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->owner_control == txn->get_control() &&
            it->commit_ts == INVALID_TS) {
            return it->deleted ? nullptr : make_record(it->data);
        }
    }

    const MvccVersion *visible = nullptr;
    for (const auto &version : history) {
        timestamp_t commit_ts = version.commit_ts;
        if (version.owner_control != nullptr) {
            if (version.owner_control->state.load(std::memory_order_acquire) !=
                TxnVisibilityState::VISIBLE) {
                continue;
            }
            commit_ts = version.owner_control->commit_ts.load(
                std::memory_order_relaxed);
        }
        if (commit_ts != INVALID_TS && commit_ts <= txn->get_start_ts() &&
            (visible == nullptr || commit_ts > visible->commit_ts)) {
            visible = &version;
        }
    }
    if (visible == nullptr || visible->deleted) {
        return nullptr;
    }
    return make_record(visible->data);
}

std::unique_ptr<RmRecord> TransactionManager::get_latest_committed_record(
    uint64_t file_id, const Rid &rid,
    std::unique_ptr<RmRecord> physical_record) {
    RecordKey key{file_id, rid};
    auto &shard = mvcc_store_.shards_[get_shard_idx(key)];
    auto shard_lock = rmdb_perf::lock_mvcc(shard.latch);
    return resolve_latest_record_under_latch(
        file_id, rid, std::move(physical_record));
}

std::unique_ptr<RmRecord>
TransactionManager::resolve_latest_record_under_latch(
    uint64_t file_id, const Rid &rid,
    std::unique_ptr<RmRecord> physical_record) {
    RecordKey key{file_id, rid};
    auto &shard = mvcc_store_.shards_[get_shard_idx(key)];
    auto history_it = shard.record_versions.find(key);
    if (history_it == shard.record_versions.end()) {
        return physical_record;
    }

    const MvccVersion *latest = nullptr;
    bool has_pending_insert = false;
    const MvccVersion *pending = nullptr;
    for (const auto &version : history_it->second) {
        const TxnVisibilityState owner_state =
            version.owner_control == nullptr
                ? TxnVisibilityState::VISIBLE
                : version.owner_control->state.load(std::memory_order_acquire);
        if (version.commit_ts == INVALID_TS ||
            owner_state != TxnVisibilityState::VISIBLE) {
            if (owner_state != TxnVisibilityState::ABORTED) {
                pending = &version;
            }
            if (version.before_deleted && !version.deleted &&
                owner_state != TxnVisibilityState::ABORTED) {
                has_pending_insert =
                    owner_state != TxnVisibilityState::ABORTED;
            }
            continue;
        }
        if (latest == nullptr || version.commit_ts > latest->commit_ts) {
            latest = &version;
        }
    }

    if (pending != nullptr) {
        return pending->before_deleted ? nullptr : make_record(pending->before);
    }
    if (latest != nullptr && latest->deleted) {
        return nullptr;
    }
    if (latest == nullptr && has_pending_insert) {
        return nullptr;
    }

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

    // Fast path: if no version was ever installed the physical records are all
    // visible.  any_versions_ever_ is monotonic (set once, never reset), so a
    // false here safely proves the store is empty without touching all 64 shard
    // headers on every batch read.
    const bool has_own_overlay =
        uses_mvcc(txn) && !txn->write_batch().writes().empty();
    if (!has_own_overlay &&
        !mvcc_store_.any_versions()) {
        return;
    }

    // Group record indices by shard so each shard is locked once.
    std::unordered_map<size_t, std::vector<size_t>> shard_groups;
    for (size_t i = 0; i < rids.size(); ++i) {
        RecordKey key{file_id, rids[i]};
        shard_groups[get_shard_idx(key)].push_back(i);
    }

    const bool snapshot = uses_mvcc(txn);
    std::vector<Rid> out_rids;
    std::vector<std::unique_ptr<RmRecord>> out_records;
    out_rids.reserve(rids.size());
    out_records.reserve(records.size());

    for (auto &[shard_idx, group] : shard_groups) {
        auto &shard = mvcc_store_.shards_[shard_idx];
        auto shard_lock = rmdb_perf::lock_mvcc(shard.latch);
        for (size_t i : group) {
            if (snapshot) {
                std::vector<char> overlay;
                const OverlayKind overlay_kind =
                    txn->write_batch().lookup(file_id, rids[i], &overlay);
                if (overlay_kind == OverlayKind::DELETED) {
                    continue;
                }
                if (overlay_kind == OverlayKind::VALUE) {
                    out_rids.push_back(rids[i]);
                    out_records.push_back(make_record(overlay));
                    continue;
                }
            }
            std::unique_ptr<RmRecord> visible =
                snapshot
                    ? resolve_snapshot_record_under_latch(
                          txn, file_id, rids[i], std::move(records[i]))
                    : resolve_latest_record_under_latch(
                          file_id, rids[i], std::move(records[i]));
            if (visible != nullptr) {
                out_rids.push_back(rids[i]);
                out_records.push_back(std::move(visible));
            }
        }
    }
    rids = std::move(out_rids);
    records = std::move(out_records);
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
    // COMMIT。调用方持有 SERIALIZABLE 元数据锁，先标记再抛出；网络服务层捕获后会
    // 同步完成完整回滚，随后才能发送 TRANSACTION_ABORT。
    txn_id_t victim = current_txn->get_transaction_id();
    auto victim_state = mvcc_txns_.find(victim);
    if (victim_state != mvcc_txns_.end()) {
        victim_state->second.aborted = true;
        victim_state->second.commit_state = MvccCommitState::ABORTED;
    }
    mark_mvcc_txn_aborted_under_latch(victim);
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
    const std::shared_ptr<TxnControl> control = txn_registry_.find(txn_id);
    return control != nullptr &&
           control->state.load(std::memory_order_acquire) ==
               TxnVisibilityState::ABORTED;
}

CommitFailurePoint TransactionManager::configured_commit_failure_point() {
    const char *value = std::getenv("RMDB_COMMIT_FAILURE_POINT");
    if (value == nullptr) {
        return CommitFailurePoint::NONE;
    }
    static const std::pair<const char *, CommitFailurePoint> points[] = {
        {"BEFORE_HEAP", CommitFailurePoint::BEFORE_HEAP},
        {"AFTER_HEAP", CommitFailurePoint::AFTER_HEAP},
        {"AFTER_INDEX", CommitFailurePoint::AFTER_INDEX},
        {"AFTER_COMMIT_WRITE", CommitFailurePoint::AFTER_COMMIT_WRITE},
        {"AFTER_COMMIT_SYNC", CommitFailurePoint::AFTER_COMMIT_SYNC},
        {"AFTER_PUBLISH", CommitFailurePoint::AFTER_PUBLISH},
        {"BEFORE_ACK", CommitFailurePoint::BEFORE_ACK},
    };
    for (const auto &[name, point] : points) {
        if (std::strcmp(value, name) == 0) {
            return point;
        }
    }
    return CommitFailurePoint::NONE;
}

void TransactionManager::maybe_fail_commit(CommitFailurePoint point) const {
    if (commit_failure_point_ == point) {
        std::_Exit(86);
    }
}

void TransactionManager::mark_mvcc_txn_aborted_under_latch(txn_id_t txn_id) {
    const std::shared_ptr<TxnControl> control = txn_registry_.find(txn_id);
    if (control != nullptr) {
        control->state.store(TxnVisibilityState::ABORTED,
                             std::memory_order_release);
    }
}

int phase2_max_active() {
    static int limit = [] {
        constexpr int kDefaultLimit = 16;
        constexpr int kMaximumLimit = 64;
        const char *value = std::getenv("RMDB_PHASE2_MAX_ACTIVE");
        if (value == nullptr || value[0] == '\0') {
            return kDefaultLimit;
        }
        char *end = nullptr;
        const long parsed = std::strtol(value, &end, 10);
        if (end == value || *end != '\0' || parsed < 1 ||
            parsed > kMaximumLimit) {
            return kDefaultLimit;
        }
        return static_cast<int>(parsed);
    }();
    return limit;
}

bool TransactionManager::mvcc_txn_entered_apply(Transaction *txn) {
    if (!uses_mvcc(txn)) {
        return false;
    }
    return txn->get_control()->entered_apply.load(std::memory_order_acquire);
}

void TransactionManager::begin_storage_apply(Transaction *txn,
                                             bool physical_apply) {
    const std::shared_ptr<TxnControl> &control = txn->get_control();
    TxnVisibilityState expected = TxnVisibilityState::VALIDATING;
    if (!control->state.compare_exchange_strong(
            expected, TxnVisibilityState::APPLYING,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        throw InternalError("Invalid MVCC storage-apply transition");
    }
    control->entered_apply.store(physical_apply, std::memory_order_release);
}

void TransactionManager::mark_mvcc_txn_aborted(txn_id_t txn_id) {
    const std::shared_ptr<TxnControl> control = txn_registry_.find(txn_id);
    if (control != nullptr) {
        control->state.store(TxnVisibilityState::ABORTED,
                             std::memory_order_release);
    }
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
            throw TransactionAbortException(txn->get_transaction_id(),
                                            AbortReason::WRITE_CONFLICT);
        }
        return;
    }
    if (before_record == nullptr) {
        throw TransactionAbortException(txn->get_transaction_id(),
                                        AbortReason::WRITE_CONFLICT);
    }
    std::unique_ptr<RmRecord> physical;
    try {
        physical = file_handle->get_record(rid, nullptr);
    } catch (const RecordNotFoundError &) {
        throw TransactionAbortException(txn->get_transaction_id(),
                                        AbortReason::WRITE_CONFLICT);
    }
    int record_size = file_handle->get_file_hdr().record_size;
    if (before_record->size != record_size ||
        memcmp(physical->data, before_record->data, record_size) != 0) {
        throw TransactionAbortException(txn->get_transaction_id(),
                                        AbortReason::WRITE_CONFLICT);
    }
}

RecordKey TransactionManager::make_record_key(
    const std::string &table_name, const Rid &rid) const {
    if (sm_manager_ == nullptr) {
        return RecordKey{0, rid};
    }
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
    for (const auto &key : txn->get_control()->record_intents_snapshot()) {
        auto &pending_shard = mvcc_store_.shards_[get_shard_idx(key)];
        auto pending_history = pending_shard.record_versions.find(key);
        if (pending_history != pending_shard.record_versions.end()) {
            MvccVersion *pending = find_own_pending_version(
                pending_history->second, txn->get_transaction_id());
            if (pending != nullptr && pending->before_deleted) {
                continue;
            }
        }
        if (write_record_is_insert_only(txn, key)) {
            continue;
        }

        WriteRecord *mutating = first_mutating_write_record(txn, key);
        if (has_multiple_mutating_writes(txn, key)) {
            if (mutating == nullptr) {
                mark_mvcc_txn_aborted_under_latch(txn->get_transaction_id());
                throw TransactionAbortException(txn->get_transaction_id(),
                                                AbortReason::WRITE_CONFLICT);
            }
            check_physical_before(txn, mutating->GetTableName(), key.rid,
                                  &mutating->GetRecord());
            continue;
        }

        auto &shard = mvcc_store_.shards_[get_shard_idx(key)];
        auto history_it = shard.record_versions.find(key);
        MvccVersion *own_pending = nullptr;
        if (history_it != shard.record_versions.end()) {
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

        mark_mvcc_txn_aborted_under_latch(txn->get_transaction_id());
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

    std::lock_guard<std::mutex> txn_lock(serializable_state_latch_);
    auto state_it = mvcc_txns_.find(txn->get_transaction_id());
    if (state_it == mvcc_txns_.end()) {
        return;
    }

    ReadPredicate predicate{file_id, conditions, columns};
    state_it->second.predicates.push_back(predicate);

    // Scan all shards for versions matching this file_id.
    // SSI predicate registration is only active for SERIALIZABLE, off the
    // TPC-C ranking hot path. SNAPSHOT ISOLATION never enters this metadata.
    for (size_t si = 0; si < kMvccShardCount; ++si) {
        auto &shard = mvcc_store_.shards_[si];
        std::lock_guard<std::mutex> shard_lock(shard.latch);
        for (const auto &[key, history] : shard.record_versions) {
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
}

std::vector<Rid> TransactionManager::get_historical_index_rids(
    Transaction *txn, int index_id) {
    if (!uses_mvcc(txn)) {
        return {};
    }
    const std::vector<IndexVersionStore::Entry> entries =
        index_versions_.snapshot(index_id);
    if (entries.empty()) {
        return {};
    }

    std::vector<Rid> result;
    result.reserve(entries.size());
    std::unordered_set<uint64_t> seen;
    for (const auto &entry : entries) {
        bool visible = false;
        if (entry.valid_until != INVALID_TS) {
            visible = txn->get_read_ts() < entry.valid_until;
        } else {
            const auto &owner = entry.owner_control;
            if (owner != nullptr &&
                owner->state.load(std::memory_order_acquire) !=
                    TxnVisibilityState::ABORTED) {
                if (owner->state.load(std::memory_order_acquire) ==
                        TxnVisibilityState::VISIBLE &&
                    owner->commit_ts.load(std::memory_order_relaxed) !=
                        INVALID_TS) {
                    visible =
                        txn->get_read_ts() < owner->commit_ts.load(
                            std::memory_order_relaxed);
                } else {
                    // Physical index application precedes publication. Until
                    // the owner resolves, every snapshot must retain a path
                    // to the pre-transaction record version.
                    visible = true;
                }
            }
        }
        if (!visible || entry.rid.page_no < 0 || entry.rid.slot_no < 0) {
            continue;
        }
        const uint64_t encoded =
            (static_cast<uint64_t>(
                 static_cast<uint32_t>(entry.rid.page_no))
             << 32) |
            static_cast<uint32_t>(entry.rid.slot_no);
        if (seen.insert(encoded).second) {
            result.push_back(entry.rid);
        }
    }
    return result;
}

void TransactionManager::register_record_read(
    Transaction *txn, uint64_t file_id, const Rid &rid) {
    if (txn == nullptr ||
        txn->get_isolation_level() != IsolationLevel::SERIALIZABLE) {
        return;
    }
    txn_id_t reader = txn->get_transaction_id();
    RecordKey key{file_id, rid};

    std::lock_guard<std::mutex> txn_lock(serializable_state_latch_);
    auto state_it = mvcc_txns_.find(reader);
    if (state_it == mvcc_txns_.end()) {
        return;
    }
    state_it->second.read_records.insert(key);

    auto &shard = mvcc_store_.shards_[get_shard_idx(key)];
    std::lock_guard<std::mutex> shard_lock(shard.latch);
    auto history_it = shard.record_versions.find(key);
    if (history_it == shard.record_versions.end()) {
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

void TransactionManager::prepare_inserts(
    Transaction *txn, uint64_t file_id, const std::vector<Rid> &rids,
    const std::vector<RmRecord> &new_records) {
    if (!uses_mvcc(txn)) {
        return;
    }
    if (rids.size() != new_records.size()) {
        throw InternalError("MVCC batch insert size mismatch");
    }

    for (size_t row = 0; row < rids.size(); ++row) {
        prepare_write_internal(txn, file_id, rids[row], nullptr,
                               &new_records[row], false, "");
    }
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

    RecordKey key{file_id, rid};
    if (!mvcc_store_.try_acquire_record_intent(txn->get_control(), key)) {
        if (perf_diag_enabled()) {
            perf_diag_stats().write_conflict_pending.fetch_add(
                1, std::memory_order_relaxed);
        }
        mark_mvcc_txn_aborted(txn->get_transaction_id());
        throw TransactionAbortException(
            txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
    }
    auto &shard = mvcc_store_.shards_[get_shard_idx(key)];
    {
        std::lock_guard<std::mutex> shard_lock(shard.latch);
        auto history_it = shard.record_versions.find(key);
        if (history_it == shard.record_versions.end()) {
            return;
        }

        timestamp_t latest_commit = 0;
        for (const auto &version : history_it->second) {
            if (version.owner_control != nullptr &&
                version.owner_control->state.load(std::memory_order_acquire) !=
                    TxnVisibilityState::VISIBLE) {
                continue;
            }
            latest_commit = std::max(latest_commit, version.commit_ts);
        }

        if (latest_commit > txn->get_start_ts()) {
            if (perf_diag_enabled()) {
                perf_diag_stats().write_conflict_committed_after_start.fetch_add(
                    1, std::memory_order_relaxed);
            }
            mark_mvcc_txn_aborted_under_latch(txn->get_transaction_id());
            throw TransactionAbortException(
                txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
        }
    }
}

bool TransactionManager::has_stale_write_target(
    Transaction *txn, uint64_t file_id,
    const std::function<bool(const RmRecord &)> &matches) {
    if (!uses_mvcc(txn)) {
        return false;
    }

    for (size_t si = 0; si < kMvccShardCount; ++si) {
        auto &shard = mvcc_store_.shards_[si];
        std::lock_guard<std::mutex> shard_lock(shard.latch);
        for (const auto &[key, history] : shard.record_versions) {
            if (key.file_id != file_id) {
                continue;
            }

            timestamp_t latest_commit = 0;
            const MvccVersion *snapshot_visible = nullptr;
            for (const auto &version : history) {
                if (version.owner_control != nullptr &&
                    version.owner_control->state.load(
                        std::memory_order_acquire) !=
                        TxnVisibilityState::VISIBLE) {
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
    }
    return false;
}

void TransactionManager::check_unique_key_conflict(
    Transaction *txn, int index_id, const Rid &target_rid,
    const RmRecord &new_record, const std::vector<ColMeta> &index_cols) {
    if (!uses_mvcc(txn)) {
        return;
    }

    size_t key_size = 0;
    for (const ColMeta &column : index_cols) {
        key_size += static_cast<size_t>(column.len);
    }
    std::vector<char> key(key_size);
    size_t offset = 0;
    for (const ColMeta &column : index_cols) {
        std::memcpy(key.data() + offset, new_record.data + column.offset,
                    column.len);
        offset += static_cast<size_t>(column.len);
    }

    acquire_unique_key_intent(txn, index_id, key);
    if (index_versions_.conflicts_with_snapshot(
            index_id, key, target_rid, txn->get_transaction_id(),
            txn->get_start_ts())) {
        if (perf_diag_enabled()) {
            perf_diag_stats().unique_conflict_committed_after_start.fetch_add(
                1, std::memory_order_relaxed);
        }
        mark_mvcc_txn_aborted(txn->get_transaction_id());
        throw TransactionAbortException(txn->get_transaction_id(),
                                        AbortReason::WRITE_CONFLICT);
    }
}

void TransactionManager::acquire_unique_key_intent(
    Transaction *txn, int index_id, const std::vector<char> &key) {
    if (!uses_mvcc(txn)) {
        return;
    }
    if (!mvcc_store_.try_acquire_unique_intent(
            txn->get_control(), index_id,
            std::string_view(key.data(), key.size()))) {
        if (perf_diag_enabled()) {
            perf_diag_stats().unique_conflict_pending.fetch_add(
                1, std::memory_order_relaxed);
        }
        mark_mvcc_txn_aborted(txn->get_transaction_id());
        throw TransactionAbortException(txn->get_transaction_id(),
                                        AbortReason::WRITE_CONFLICT);
    }
}

void TransactionManager::release_unique_key_intents(Transaction *txn) {
    // MvccStore releases both record and unique intents atomically with
    // publication or abort. Keep this compatibility seam for existing callers.
    txn->clear_unique_intents();
}

void TransactionManager::prepare_write(
    Transaction *txn, uint64_t file_id, const Rid &rid,
    const RmRecord *old_record, const RmRecord *new_record, bool deleted,
    const std::string &table_name) {
    if (!uses_mvcc(txn)) {
        return;
    }

    prepare_write_internal(txn, file_id, rid, old_record, new_record,
                           deleted, table_name);
}

void TransactionManager::prepare_write_internal(
    Transaction *txn, uint64_t file_id, const Rid &rid,
    const RmRecord *old_record, const RmRecord *new_record, bool deleted,
    const std::string &table_name) {

    RecordKey key{file_id, rid};
    if (!mvcc_store_.try_acquire_record_intent(txn->get_control(), key)) {
        if (perf_diag_enabled()) {
            perf_diag_stats().prepare_conflict_pending.fetch_add(
                1, std::memory_order_relaxed);
        }
        mark_mvcc_txn_aborted(txn->get_transaction_id());
        throw TransactionAbortException(
            txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
    }
    auto &shard = mvcc_store_.shards_[get_shard_idx(key)];

    MvccVersion *own_pending = nullptr;
    {
        std::lock_guard<std::mutex> shard_lock(shard.latch);
        auto &history = shard.record_versions[key];
        shard.gc_dirty_keys.insert(key);

        if (history.empty() && old_record != nullptr) {
            MvccVersion baseline;
            baseline.commit_ts = 0;
            baseline.deleted = false;
            baseline.data = copy_record(old_record);
            history.push_back(std::move(baseline));
        }

        own_pending = nullptr;
        timestamp_t latest_commit = 0;
        for (auto &version : history) {
            if (version.owner_control == txn->get_control() &&
                version.commit_ts == INVALID_TS) {
                own_pending = &version;
                continue;
            }
            if (version.owner_control != nullptr &&
                version.owner_control->state.load(std::memory_order_acquire) !=
                    TxnVisibilityState::VISIBLE) {
                continue;
            }
            latest_commit = std::max(latest_commit, version.commit_ts);
        }

        if (own_pending == nullptr && latest_commit > txn->get_start_ts()) {
            if (perf_diag_enabled()) {
                perf_diag_stats()
                    .prepare_conflict_committed_after_start.fetch_add(
                        1, std::memory_order_relaxed);
            }
            mark_mvcc_txn_aborted_under_latch(txn->get_transaction_id());
            throw TransactionAbortException(
                txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
        }
    }

    if (own_pending == nullptr && old_record != nullptr &&
        !table_name.empty()) {
        check_physical_before(txn, table_name, rid, old_record);
    }

    // Build the prospective version (re-acquire shard to read/modify history).
    MvccVersion prospective;
    {
        std::lock_guard<std::mutex> shard_lock(shard.latch);
        auto &history = shard.record_versions[key];
        // Re-find own_pending — the vector address may have changed across
        // unlock/re-lock if another thread inserted or erased an entry.
        own_pending = find_own_pending_version(history, txn->get_transaction_id());
        if (own_pending == nullptr) {
            prospective.owner = txn->get_transaction_id();
            prospective.owner_control = txn->get_control();
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
    }

    // SSI metadata is isolated from the SNAPSHOT ISOLATION path.
    if (txn->get_isolation_level() == IsolationLevel::SERIALIZABLE) {
        std::lock_guard<std::mutex> serial_lock(serializable_state_latch_);
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

    // Install the pending version under the shard latch.
    {
        std::lock_guard<std::mutex> shard_lock(shard.latch);
        auto &history = shard.record_versions[key];
        own_pending = find_own_pending_version(history, txn->get_transaction_id());
        if (own_pending == nullptr) {
            history.push_back(std::move(prospective));
        } else {
            *own_pending = std::move(prospective);
        }
    }

    // Latch-free fast path for filter_visible_records: set once, never reset.
    // The check-then-set avoids bouncing the cache line once the bit is set.
    if (!mvcc_store_.any_versions_ever_.load(std::memory_order_relaxed)) {
        mvcc_store_.any_versions_ever_.store(true, std::memory_order_release);
    }

    if (txn->get_isolation_level() == IsolationLevel::SERIALIZABLE) {
        std::lock_guard<std::mutex> serial_lock(serializable_state_latch_);
        auto state_it = mvcc_txns_.find(txn->get_transaction_id());
        if (state_it != mvcc_txns_.end()) {
            state_it->second.write_records.insert(key);
        }
    }
}

void TransactionManager::check_commit_conflict_under_latch(Transaction *txn) {
    // Precondition: all record shards touched by this transaction are locked.
    if (!uses_mvcc(txn)) {
        return;
    }

    for (const auto &key : txn->get_control()->record_intents_snapshot()) {
        auto &shard = mvcc_store_.shards_[get_shard_idx(key)];
        auto history_it = shard.record_versions.find(key);
        if (history_it == shard.record_versions.end()) {
            if (write_record_is_insert_only(txn, key)) {
                continue;
            }
            WriteRecord *mutating = first_mutating_write_record(txn, key);
            if (mutating != nullptr) {
                check_physical_before(txn, mutating->GetTableName(), key.rid,
                                      &mutating->GetRecord());
            } else {
                mark_mvcc_txn_aborted_under_latch(txn->get_transaction_id());
                throw TransactionAbortException(txn->get_transaction_id(),
                                                AbortReason::WRITE_CONFLICT);
            }
            continue;
        }
        for (const auto &version : history_it->second) {
            if (version.owner_control != nullptr &&
                version.owner_control != txn->get_control() &&
                version.owner_control->state.load(std::memory_order_acquire) !=
                    TxnVisibilityState::VISIBLE &&
                version.owner_control->state.load(std::memory_order_acquire) !=
                    TxnVisibilityState::ABORTED) {
                    if (perf_diag_enabled()) {
                        perf_diag_stats().commit_conflict_pending.fetch_add(
                            1, std::memory_order_relaxed);
                    }
                    mark_mvcc_txn_aborted_under_latch(txn->get_transaction_id());
                    throw TransactionAbortException(
                        txn->get_transaction_id(),
                        AbortReason::WRITE_CONFLICT);
            } else if (version.commit_ts > txn->get_start_ts() &&
                       version.owner_control != txn->get_control()) {
                if (perf_diag_enabled()) {
                    perf_diag_stats()
                        .commit_conflict_committed_after_start.fetch_add(
                            1, std::memory_order_relaxed);
                }
                mark_mvcc_txn_aborted_under_latch(txn->get_transaction_id());
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

    // Collect and lock all shards touched by this transaction.
    std::set<size_t> sorted_shards;
    for (const auto &key : txn->get_control()->record_intents_snapshot()) {
        sorted_shards.insert(get_shard_idx(key));
    }
    std::vector<std::unique_lock<std::mutex>> shard_locks;
    for (size_t idx : sorted_shards) {
        shard_locks.emplace_back(mvcc_store_.shards_[idx].latch);
    }

    check_commit_conflict_under_latch(txn);
}

void TransactionManager::commit_mvcc(Transaction *txn,
                                     LogManager *log_manager) {
    if (!uses_mvcc(txn)) {
        return;
    }

    StorageCommitExecutor storage_commit(sm_manager_, this);
    PreparedStorageCommit prepared = storage_commit.prepare(txn);

    // Phase 1 validates the pending versions installed from the frozen batch.
    // No physical Heap/Index state changes before validation completes.
    TxnVisibilityState expected = TxnVisibilityState::ACTIVE;
    if (!txn->get_control()->state.compare_exchange_strong(
            expected, TxnVisibilityState::VALIDATING,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        throw TransactionAbortException(txn->get_transaction_id(),
                                        AbortReason::WRITE_CONFLICT);
    }
    check_commit_conflict(txn);

    // Phase 2 applies heap/index changes with bounded concurrency. Versions
    // remain pending, so readers cannot observe the not-yet-durable result.
    {
        std::unique_lock<std::mutex> phase2_lock(phase2_latch_);
        phase2_cv_.wait(phase2_lock, [&] {
            return phase2_active_count_ < phase2_max_active();
        });
        ++phase2_active_count_;
        phase2_lock.unlock();
    }

    try {
        Context context(lock_manager_, log_manager, txn, this);
        storage_commit.apply(&prepared, &context);
    } catch (...) {
        // Release Phase-2 slot before propagating the abort.
        {
            std::lock_guard<std::mutex> lk(phase2_latch_);
            --phase2_active_count_;
        }
        phase2_cv_.notify_one();
        throw;
    }

    // Release Phase-2 slot.
    {
        std::lock_guard<std::mutex> lk(phase2_latch_);
        --phase2_active_count_;
    }
    phase2_cv_.notify_one();

    try {
        // The positive COMMIT record and the transaction's entire preceding
        // WAL chain must be durable before any version is published.
        if (log_manager != nullptr) {
            CommitLogRecord commit_log(txn->get_transaction_id());
            commit_log.prev_lsn_ = txn->get_prev_lsn();
            lsn_t lsn = log_manager->add_log_to_buffer(&commit_log);
            if (lsn == INVALID_LSN ||
                (!prepared.writes.empty() &&
                 prepared.greatest_row_lsn == INVALID_LSN)) {
                throw InternalError("Commit is missing positive row/COMMIT WAL");
            }
            txn->set_prev_lsn(lsn);
            maybe_fail_commit(CommitFailurePoint::AFTER_COMMIT_WRITE);
            log_manager->force_flush_up_to(lsn);
            if (log_manager->durable_lsn() < lsn) {
                throw InternalError("Commit WAL is not durably covered");
            }
            maybe_fail_commit(CommitFailurePoint::AFTER_COMMIT_SYNC);
        }

        if (txn->get_control()->state.load(std::memory_order_acquire) !=
            TxnVisibilityState::APPLYING) {
            throw InternalError("Invalid MVCC durable publication state");
        }
        const timestamp_t commit_ts = txn_registry_.next_commit_ts();
        index_versions_.finalize(txn->get_control(), commit_ts);
        mvcc_store_.publish(txn->get_control(), commit_ts);
        maybe_fail_commit(CommitFailurePoint::AFTER_PUBLISH);
        if (txn->get_isolation_level() == IsolationLevel::SERIALIZABLE) {
            std::lock_guard<std::mutex> lock(serializable_state_latch_);
            auto state_it = mvcc_txns_.find(txn->get_transaction_id());
            if (state_it != mvcc_txns_.end()) {
                state_it->second.commit_ts = commit_ts;
                state_it->second.commit_state = MvccCommitState::VISIBLE;
            }
        }
    } catch (...) {
        Context rollback_context(lock_manager_, nullptr, txn, this);
        storage_commit.rollback(&prepared, &rollback_context);
        throw;
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

    index_versions_.discard(txn->get_control());
    mvcc_store_.abort(txn->get_control());
    if (txn->get_isolation_level() == IsolationLevel::SERIALIZABLE) {
        std::lock_guard<std::mutex> lock(serializable_state_latch_);
        auto state_it = mvcc_txns_.find(txn->get_transaction_id());
        if (state_it != mvcc_txns_.end()) {
            remove_dependencies(txn->get_transaction_id());
            state_it->second.aborted = true;
            state_it->second.commit_state = MvccCommitState::ABORTED;
            state_it->second.predicates.clear();
            state_it->second.read_records.clear();
            state_it->second.write_records.clear();
            state_it->second.cleanup_done = true;
        }
    }
}

timestamp_t TransactionManager::GetWatermark() {
    timestamp_t watermark = txn_registry_.current_commit_ts();
    for (const auto &control : txn_registry_.snapshot()) {
        const TxnVisibilityState state =
            control->state.load(std::memory_order_acquire);
        if (state == TxnVisibilityState::VISIBLE ||
            state == TxnVisibilityState::ABORTED) {
            continue;
        }
        watermark = std::min(watermark, control->start_ts);
    }
    return watermark;
}

void TransactionManager::GarbageCollection() {
    garbage_collect_shards(0, kMvccShardCount);
}

void TransactionManager::garbage_collect_incremental() {
    const size_t shard_idx =
        mvcc_gc_shard_cursor_.fetch_add(1, std::memory_order_relaxed) &
        (kMvccShardCount - 1);
    garbage_collect_shards(shard_idx, 1);
}

void TransactionManager::garbage_collect_shards(size_t first_shard,
                                                 size_t shard_count) {
    // GC only compacts in-memory version metadata; committed DELETEs have
    // already been applied to the heap before commit_mvcc() returns.

    // The watermark is the smallest read timestamp of any in-flight MVCC
    // transaction. Any committed version older than the watermark can never
    // be observed again, so it is safe to reclaim.
    timestamp_t watermark = txn_registry_.current_commit_ts();
    for (const auto &control : txn_registry_.snapshot()) {
        const TxnVisibilityState state =
            control->state.load(std::memory_order_acquire);
        if (state == TxnVisibilityState::VISIBLE ||
            state == TxnVisibilityState::ABORTED) {
            continue;
        }
        watermark = std::min(watermark, control->start_ts);
    }
    index_versions_.garbage_collect(watermark);

    // Process only the requested bounded shard range. The commit hot path
    // advances one shard every 16 commits, avoiding the long stop-the-world
    // pause caused by scanning all 64 shards in one client COMMIT. The public
    // GarbageCollection entry point still performs a complete pass.
    for (size_t offset = 0; offset < shard_count; ++offset) {
        const size_t shard_idx =
            (first_shard + offset) & (kMvccShardCount - 1);
        std::lock_guard<std::mutex> shard_lock(
            mvcc_store_.shards_[shard_idx].latch);
        auto &shard = mvcc_store_.shards_[shard_idx];

        for (auto dirty_it = shard.gc_dirty_keys.begin();
             dirty_it != shard.gc_dirty_keys.end();) {
            RecordKey candidate_key = *dirty_it;
            auto current_dirty_it = dirty_it++;
            auto it = shard.record_versions.find(candidate_key);
            if (it == shard.record_versions.end()) {
                shard.gc_dirty_keys.erase(current_dirty_it);
                continue;
            }
            auto &history = it->second;

            // Find the newest committed version visible at the watermark.
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
                    if (only.deleted) {
                        // The heap row is already gone.  Retain the
                        // tombstone's timestamp while the chain exists so a
                        // copied/reused physical slot cannot resurrect for an
                        // older snapshot.
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
                shard.gc_dirty_keys.erase(current_dirty_it);
            }
        }
    }

    // Prune SERIALIZABLE-only metadata independently of SI version shards.
    {
        std::lock_guard<std::mutex> lock(serializable_state_latch_);
        for (auto it = mvcc_txns_.begin(); it != mvcc_txns_.end();) {
            const auto &state = it->second;
            const bool committed_settled =
                state.commit_ts != INVALID_TS && state.commit_ts <= watermark;
            const bool aborted_settled = state.aborted && state.cleanup_done;
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

    {

        for (size_t shard_idx = 0; shard_idx < kMvccShardCount; ++shard_idx) {
            std::lock_guard<std::mutex> shard_lock(
                mvcc_store_.shards_[shard_idx].latch);
            auto &shard = mvcc_store_.shards_[shard_idx];

            for (auto it = shard.record_versions.begin();
                 it != shard.record_versions.end();) {
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
                // begin_static_checkpoint() has quiesced every transaction,
                // so pending versions should not exist; keep such chains
                // untouched out of caution.
                if (has_pending || latest == nullptr || !latest->deleted) {
                    ++it;
                    continue;
                }
                if (!latest->table_name.empty()) {
                    reclaims.push_back(
                        Reclaim{latest->table_name, it->first.rid});
                }
                // The physical state now becomes authoritative (row absent),
                // so the tombstone chain is no longer needed.
                it = shard.record_versions.erase(it);
            }
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
    txn_registry_.release(txn->get_transaction_id());
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
