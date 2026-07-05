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
#include <cstring>
#include "common/context.h"
#include "execution/execution_eval.h"
#include "record/rm_file_handle.h"
#include "system/sm_manager.h"

std::unordered_map<txn_id_t, Transaction *> TransactionManager::txn_map = {};

static void clear_write_set(Transaction *txn) {
    auto write_set = txn->get_write_set();
    for (auto it = write_set->begin(); it != write_set->end();) {
        delete *it;
        it = write_set->erase(it);
    }
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
            std::lock_guard<std::mutex> lock(mvcc_latch_);
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

static void update_indexes(SmManager *sm_manager,
                           const std::string &table_name,
                           const RmRecord &old_record,
                           const RmRecord &new_record,
                           const Rid &rid, Transaction *txn) {
    auto &tab = sm_manager->db_.get_table(table_name);
    for (auto &index_meta : tab.indexes) {
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
        if (memcmp(old_key.data(), new_key.data(),
                   index_meta.col_tot_len) == 0) {
            continue;
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

    if (log_manager != nullptr) {
        CommitLogRecord commit_log(txn->get_transaction_id());
        commit_log.prev_lsn_ = txn->get_prev_lsn();
        lsn_t lsn = log_manager->add_log_to_buffer(&commit_log);
        txn->set_prev_lsn(lsn);
        log_manager->flush_log_to_disk();
    }

    if (txn->uses_mvcc()) {
        commit_mvcc(txn);
        // Amortized reclamation of obsolete MVCC versions and transaction
        // bookkeeping. Without this both structures grow without bound under a
        // sustained workload (e.g. the TPCC performance run) and eventually
        // exhaust memory, which surfaces as a post-run consistency failure.
        if ((mvcc_commit_count_.fetch_add(1) & 0xFFu) == 0) {
            GarbageCollection();
        }
    }

    for (const auto &lock_id : *txn->get_lock_set()) {
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

    Context context(lock_manager_, log_manager, txn);
    auto write_set = txn->get_write_set();
    while (!write_set->empty()) {
        WriteRecord *write_record = write_set->back();
        if (!txn->uses_mvcc() ||
            (write_record->GetWriteType() != WType::UPDATE_TUPLE &&
             write_record->GetWriteType() != WType::DELETE_TUPLE)) {
            sm_manager_->rollback(write_record, &context);
        }
        write_set->pop_back();
        delete write_record;
    }

    if (log_manager != nullptr) {
        // Rollback operations are not represented by compensation log
        // records in this framework. Make the restored table/index state
        // durable before the ABORT record says recovery may skip this txn.
        sm_manager_->flush_for_checkpoint();
        AbortLogRecord abort_log(txn->get_transaction_id());
        abort_log.prev_lsn_ = txn->get_prev_lsn();
        lsn_t lsn = log_manager->add_log_to_buffer(&abort_log);
        txn->set_prev_lsn(lsn);
        log_manager->flush_log_to_disk(true);
    }

    for (const auto &lock_id : *txn->get_lock_set()) {
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

    std::lock_guard<std::mutex> lock(mvcc_latch_);
    RecordKey key{file_id, rid};
    auto history_it = record_versions_.find(key);
    if (history_it == record_versions_.end()) {
        if (physical_record == nullptr) {
            return nullptr;
        }
        MvccVersion baseline;
        baseline.commit_ts = 0;
        baseline.deleted = false;
        baseline.data = copy_record(physical_record);
        history_it =
            record_versions_.emplace(key, std::vector<MvccVersion>{std::move(baseline)}).first;
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
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    RecordKey key{file_id, rid};
    auto history_it = record_versions_.find(key);
    if (history_it == record_versions_.end()) {
        return physical_record == nullptr
                   ? nullptr
                   : std::make_unique<RmRecord>(*physical_record);
    }

    const MvccVersion *latest = nullptr;
    for (const auto &version : history_it->second) {
        if (version.commit_ts == INVALID_TS) {
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

    // The physical record is the authoritative latest-committed state: MVCC
    // commits and non-MVCC (READ COMMITTED) writes both land there, while the
    // version history is only updated by MVCC. Returning the version data here
    // would mask non-MVCC updates and cause lost updates once a row has an MVCC
    // history entry, so prefer the physical record.
    if (physical_record != nullptr) {
        return std::make_unique<RmRecord>(*physical_record);
    }
    if (latest == nullptr) {
        return nullptr;
    }
    return make_record(latest->data);
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
}

void TransactionManager::register_table_read(
    Transaction *txn, uint64_t file_id,
    const std::vector<Condition> &conditions,
    const std::vector<ColMeta> &columns) {
    if (txn == nullptr ||
        txn->get_isolation_level() != IsolationLevel::SERIALIZABLE) {
        return;
    }

    std::lock_guard<std::mutex> lock(mvcc_latch_);
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
                if (add_rw_dependency(reader, writer) &&
                    dependency_forms_dangerous_structure(reader, writer)) {
                    mark_mvcc_txn_aborted(txn->get_transaction_id());
                    throw TransactionAbortException(
                        txn->get_transaction_id(),
                        AbortReason::SERIALIZATION_FAILURE);
                }
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
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    auto it = mvcc_txns_.find(txn->get_transaction_id());
    if (it != mvcc_txns_.end()) {
        it->second.read_records.insert(RecordKey{file_id, rid});
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

    std::lock_guard<std::mutex> lock(mvcc_latch_);
    RecordKey key{file_id, rid};
    auto history_it = record_versions_.find(key);
    if (history_it == record_versions_.end()) {
        return;
    }

    timestamp_t latest_commit = 0;
    for (const auto &version : history_it->second) {
        if (version.commit_ts == INVALID_TS) {
            if (version.owner != txn->get_transaction_id()) {
                if (mvcc_txn_aborted(version.owner)) {
                    continue;
                }
                mark_mvcc_txn_aborted(txn->get_transaction_id());
                throw TransactionAbortException(
                    txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
            }
        } else {
            latest_commit = std::max(latest_commit, version.commit_ts);
        }
    }
    if (latest_commit > txn->get_start_ts()) {
        mark_mvcc_txn_aborted(txn->get_transaction_id());
        throw TransactionAbortException(
            txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
    }
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

    std::lock_guard<std::mutex> lock(mvcc_latch_);
    for (const auto &[key, history] : record_versions_) {
        if (key.file_id != file_id || key.rid == target_rid) {
            continue;
        }

        const MvccVersion *latest_committed = nullptr;
        for (const auto &version : history) {
            if (version.commit_ts == INVALID_TS) {
                if (version.owner != txn->get_transaction_id() &&
                    !mvcc_txn_aborted(version.owner) &&
                    !version.deleted && same_key(version.data)) {
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

    std::lock_guard<std::mutex> lock(mvcc_latch_);
    RecordKey key{file_id, rid};
    auto &history = record_versions_[key];
    if (history.empty() && old_record != nullptr) {
        MvccVersion baseline;
        baseline.commit_ts = 0;
        baseline.deleted = false;
        baseline.data = copy_record(old_record);
        history.push_back(std::move(baseline));
    }

    MvccVersion *own_pending = nullptr;
    timestamp_t latest_commit = 0;
    for (auto &version : history) {
        if (version.commit_ts == INVALID_TS) {
            if (version.owner != txn->get_transaction_id()) {
                if (mvcc_txn_aborted(version.owner)) {
                    continue;
                }
                mark_mvcc_txn_aborted(txn->get_transaction_id());
                throw TransactionAbortException(
                    txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
            }
            own_pending = &version;
        } else {
            latest_commit = std::max(latest_commit, version.commit_ts);
        }
    }
    if (own_pending == nullptr && latest_commit > txn->get_start_ts()) {
        mark_mvcc_txn_aborted(txn->get_transaction_id());
        throw TransactionAbortException(
            txn->get_transaction_id(), AbortReason::WRITE_CONFLICT);
    }

    if (own_pending == nullptr) {
        MvccVersion pending;
        pending.owner = txn->get_transaction_id();
        pending.before_deleted = old_record == nullptr;
        pending.before = copy_record(old_record);
        pending.deleted = deleted;
        pending.data = copy_record(new_record);
        pending.table_name = table_name;
        history.push_back(std::move(pending));
        own_pending = &history.back();
    } else {
        if (own_pending->before_deleted && own_pending->before.empty() &&
            !own_pending->deleted && old_record != nullptr) {
            own_pending->before = own_pending->data;
        }
        own_pending->deleted = deleted;
        own_pending->data = copy_record(new_record);
        if (!table_name.empty()) {
            own_pending->table_name = table_name;
        }
    }

    auto state_it = mvcc_txns_.find(txn->get_transaction_id());
    if (state_it != mvcc_txns_.end()) {
        state_it->second.write_records.insert(key);
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
                        predicate_affected(predicate, *own_pending)) {
                        affected = true;
                        break;
                    }
                }
            }
            if (affected) {
                txn_id_t writer_id = txn->get_transaction_id();
                if (add_rw_dependency(reader_id, writer_id) &&
                    dependency_forms_dangerous_structure(
                        reader_id, writer_id)) {
                    mark_mvcc_txn_aborted(txn->get_transaction_id());
                    throw TransactionAbortException(
                        txn->get_transaction_id(),
                        AbortReason::SERIALIZATION_FAILURE);
                }
            }
        }
    }
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
    std::lock_guard<std::mutex> apply_lock(commit_apply_latch_);
    {
        std::lock_guard<std::mutex> lock(mvcc_latch_);
        timestamp_t commit_ts = last_commit_ts_.fetch_add(1) + 1;
        txn->set_commit_ts(commit_ts);
        auto state_it = mvcc_txns_.find(txn->get_transaction_id());
        if (state_it != mvcc_txns_.end()) {
            state_it->second.commit_ts = commit_ts;
            for (const auto &key : state_it->second.write_records) {
                auto history_it = record_versions_.find(key);
                if (history_it == record_versions_.end()) {
                    continue;
                }
                for (auto &version : history_it->second) {
                    if (version.owner == txn->get_transaction_id() &&
                        version.commit_ts == INVALID_TS) {
                        if (!version.table_name.empty() &&
                            !version.before.empty()) {
                            ops.push_back(PhysicalOp{version.deleted,
                                                     version.table_name,
                                                     key.rid, version.before,
                                                     version.data});
                        }
                        version.commit_ts = commit_ts;
                    }
                }
            }
        }
    }

    for (auto &op : ops) {
        RmRecord before(static_cast<int>(op.before.size()),
                        const_cast<char *>(op.before.data()));
        if (op.is_delete) {
            delete_indexes(sm_manager_, op.table_name, before, op.rid, txn);
        } else {
            auto file_handle = sm_manager_->fhs_.at(op.table_name).get();
            RmRecord after(static_cast<int>(op.after.size()),
                           const_cast<char *>(op.after.data()));
            file_handle->update_record(op.rid, after.data, nullptr);
            update_indexes(sm_manager_, op.table_name, before, after, op.rid,
                           txn);
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
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    auto state_it = mvcc_txns_.find(txn->get_transaction_id());
    if (state_it == mvcc_txns_.end()) {
        return;
    }
    for (const auto &key : state_it->second.write_records) {
        auto history_it = record_versions_.find(key);
        if (history_it == record_versions_.end()) {
            continue;
        }
        auto &history = history_it->second;
        history.erase(
            std::remove_if(history.begin(), history.end(), [&](const MvccVersion &version) {
                return version.owner == txn->get_transaction_id() &&
                       version.commit_ts == INVALID_TS;
            }),
            history.end());
        if (history.empty()) {
            record_versions_.erase(history_it);
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
}

timestamp_t TransactionManager::GetWatermark() {
    std::lock_guard<std::mutex> lock(mvcc_latch_);
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
    struct Reclaim {
        std::string table_name;
        RecordKey key;
        timestamp_t commit_ts;
    };
    std::vector<Reclaim> reclaims;

    // Same lock ordering as commit_mvcc: mvcc_latch_ is a leaf lock, so all
    // physical slot reclamation happens after releasing it, serialized against
    // concurrent commit application by commit_apply_latch_.
    std::lock_guard<std::mutex> apply_lock(commit_apply_latch_);
    {
        std::lock_guard<std::mutex> lock(mvcc_latch_);

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

        for (auto it = record_versions_.begin();
             it != record_versions_.end();) {
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

            if (history.size() == 1) {
                MvccVersion &only = history.front();
                if (only.commit_ts != INVALID_TS &&
                    only.commit_ts <= watermark) {
                    if (!only.deleted) {
                        // The physical record already carries the authoritative
                        // committed state for a live row, so the chain can go.
                        it = record_versions_.erase(it);
                        continue;
                    }
                    if (!only.table_name.empty()) {
                        // Deleted rows keep a tombstone in the chain forever:
                        // a concurrent reader may have copied the physical
                        // bytes right before we reclaim the slot, and only the
                        // tombstone stops that row from resurrecting. The
                        // physical delete happens outside mvcc_latch_.
                        reclaims.push_back(
                            Reclaim{only.table_name, it->first,
                                    only.commit_ts});
                    }
                }
            }
            ++it;
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

    if (reclaims.empty()) {
        return;
    }

    for (const auto &reclaim : reclaims) {
        auto fh_it = sm_manager_->fhs_.find(reclaim.table_name);
        if (fh_it == sm_manager_->fhs_.end()) {
            continue;
        }
        RmFileHandle *file_handle = fh_it->second.get();
        if (file_handle->record_exists(reclaim.key.rid)) {
            file_handle->delete_record(reclaim.key.rid, nullptr);
        }
    }

    // Shrink the reclaimed tombstones: keep the (deleted, commit_ts) marker but
    // drop the payload copies, and clear table_name so the physical delete is
    // not retried on every GC cycle.
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    for (const auto &reclaim : reclaims) {
        auto history_it = record_versions_.find(reclaim.key);
        if (history_it == record_versions_.end()) {
            continue;
        }
        for (auto &version : history_it->second) {
            if (version.deleted && version.commit_ts == reclaim.commit_ts) {
                version.table_name.clear();
                version.table_name.shrink_to_fit();
                version.before.clear();
                version.before.shrink_to_fit();
                version.data.clear();
                version.data.shrink_to_fit();
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

    std::lock_guard<std::mutex> apply_lock(commit_apply_latch_);
    {
        std::lock_guard<std::mutex> lock(mvcc_latch_);
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
