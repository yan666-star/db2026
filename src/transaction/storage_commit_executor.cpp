#include "transaction/storage_commit_executor.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <utility>

#include "common/context.h"
#include "errors.h"
#include "index/ix.h"
#include "record/rm_file_handle.h"
#include "recovery/log_manager.h"
#include "system/sm_manager.h"
#include "transaction/transaction.h"
#include "transaction/transaction_manager.h"

namespace {

std::vector<char> index_key(const RmRecord &record,
                            const IndexMeta &index) {
    std::vector<char> key(index.col_tot_len);
    int offset = 0;
    for (const ColMeta &column : index.cols) {
        std::memcpy(key.data() + offset, record.data + column.offset,
                    column.len);
        offset += column.len;
    }
    return key;
}

void insert_indexes(SmManager *sm_manager, const std::string &table_name,
                    const RmRecord &record, const Rid &rid,
                    Transaction *txn) {
    const TabMeta &table = sm_manager->db_.get_table(table_name);
    for (const IndexMeta &index : table.indexes) {
        const std::string name =
            sm_manager->get_ix_manager()->get_index_name(table_name,
                                                         index.cols);
        std::vector<char> key = index_key(record, index);
        sm_manager->ihs_.at(name)->insert_entry(key.data(), rid, txn);
    }
}

void update_indexes(SmManager *sm_manager, TransactionManager *txn_manager,
                    IndexVersionStore *index_versions,
                    const std::string &table_name,
                    const RmRecord &before, const RmRecord &after,
                    const Rid &rid, Transaction *txn) {
    const TabMeta &table = sm_manager->db_.get_table(table_name);
    for (const IndexMeta &index : table.indexes) {
        std::vector<char> old_key = index_key(before, index);
        std::vector<char> new_key = index_key(after, index);
        if (old_key == new_key) {
            continue;
        }
        const std::string name =
            sm_manager->get_ix_manager()->get_index_name(table_name,
                                                         index.cols);
        IxIndexHandle *handle = sm_manager->ihs_.at(name).get();
        txn_manager->acquire_unique_key_intent(txn, handle->GetFd(), old_key);
        index_versions->retain(handle->GetFd(), old_key, rid,
                               txn->get_transaction_id());
        handle->delete_entry(old_key.data(), txn);
        handle->insert_entry(new_key.data(), rid, txn);
    }
}

void delete_indexes(SmManager *sm_manager, TransactionManager *txn_manager,
                    IndexVersionStore *index_versions,
                    const std::string &table_name,
                    const RmRecord &record, const Rid &rid,
                    Transaction *txn) {
    const TabMeta &table = sm_manager->db_.get_table(table_name);
    for (const IndexMeta &index : table.indexes) {
        const std::string name =
            sm_manager->get_ix_manager()->get_index_name(table_name,
                                                         index.cols);
        IxIndexHandle *handle = sm_manager->ihs_.at(name).get();
        std::vector<char> key = index_key(record, index);
        std::vector<Rid> indexed;
        if (!handle->get_value(key.data(), &indexed, txn) ||
            std::find(indexed.begin(), indexed.end(), rid) == indexed.end()) {
            continue;
        }
        txn_manager->acquire_unique_key_intent(txn, handle->GetFd(), key);
        index_versions->retain(handle->GetFd(), key, rid,
                               txn->get_transaction_id());
        handle->delete_entry(key.data(), txn);
    }
}

Rid resolve_rid(const PreparedStorageCommit &commit,
                const StagedWrite &write) {
    if (write.rid.has_value()) {
        return *write.rid;
    }
    auto resolved = commit.resolved_insert_rids.find(write.temp_id);
    if (resolved == commit.resolved_insert_rids.end()) {
        throw InternalError("Staged insert RID was not resolved");
    }
    return resolved->second;
}

RmRecord record_view(const std::vector<char> &bytes) {
    return RmRecord(static_cast<int>(bytes.size()),
                    const_cast<char *>(bytes.data()));
}

}  // namespace

HeapSlotReservation::~HeapSlotReservation() { release(); }

std::vector<ReservedInsert> HeapSlotReservation::reserve(
    const std::vector<StagedWrite> &inserts) {
    if (file_ == nullptr) {
        throw InternalError("Missing Heap file for insert reservation");
    }
    for (const StagedWrite &write : inserts) {
        if (write.kind != LogicalWriteKind::INSERT || write.rid.has_value()) {
            throw InternalError("Heap reservation received a non-insert");
        }
    }

    reserved_rids_ = file_->reserve_insert_slots(inserts.size());
    std::vector<ReservedInsert> result;
    result.reserve(inserts.size());
    for (size_t index = 0; index < inserts.size(); ++index) {
        result.push_back(
            ReservedInsert{inserts[index].temp_id, reserved_rids_[index],
                           inserts[index].after});
    }
    return result;
}

void HeapSlotReservation::release() noexcept {
    if (file_ != nullptr && !reserved_rids_.empty()) {
        file_->release_reserved_slots(reserved_rids_);
        reserved_rids_.clear();
    }
}

PreparedStorageCommit StorageCommitExecutor::prepare(Transaction *txn) {
    if (txn == nullptr) {
        throw InternalError("Missing transaction for storage commit");
    }

    PreparedStorageCommit commit;
    commit.writes = txn->write_batch().freeze();
    if (!commit.writes.empty() && sm_manager_ == nullptr) {
        throw InternalError("Missing system manager for staged commit");
    }

    std::unordered_map<uint64_t, std::vector<StagedWrite>> inserts_by_file;
    std::unordered_map<uint64_t, std::string> table_by_file;
    for (const StagedWrite &write : commit.writes) {
        if (write.kind != LogicalWriteKind::INSERT) {
            continue;
        }
        auto [table_it, inserted] =
            table_by_file.emplace(write.file_id, write.table_name);
        if (!inserted && table_it->second != write.table_name) {
            throw InternalError("One Heap file mapped to multiple tables");
        }
        inserts_by_file[write.file_id].push_back(write);
    }

    for (auto &[file_id, inserts] : inserts_by_file) {
        const std::string &table_name = table_by_file.at(file_id);
        RmFileHandle *file = sm_manager_->fhs_.at(table_name).get();
        if (file->GetMvccFileId() != file_id) {
            throw InternalError("Staged insert file identity changed");
        }
        auto reservation = std::make_unique<HeapSlotReservation>(file);
        std::vector<ReservedInsert> reserved = reservation->reserve(inserts);
        for (const ReservedInsert &insert : reserved) {
            commit.resolved_insert_rids.emplace(insert.temp_id, insert.rid);
            commit.inserts.push_back(insert);
        }
        reservations_.push_back(std::move(reservation));
    }

    for (const StagedWrite &write : commit.writes) {
        if (write.kind == LogicalWriteKind::INSERT) {
            RmRecord after = record_view(write.after);
            transaction_manager_->prepare_insert(
                txn, write.file_id, resolve_rid(commit, write), after);
        } else if (write.kind == LogicalWriteKind::UPDATE) {
            RmRecord before = record_view(write.before);
            RmRecord after = record_view(write.after);
            transaction_manager_->prepare_update(
                txn, write.file_id, *write.rid, before, after,
                write.table_name);
        } else {
            RmRecord before = record_view(write.before);
            transaction_manager_->prepare_delete(
                txn, write.file_id, *write.rid, before, write.table_name);
        }
    }
    return commit;
}

void StorageCommitExecutor::apply(PreparedStorageCommit *commit,
                                  Context *context) {
    if (commit == nullptr || context == nullptr || context->txn_ == nullptr) {
        throw InternalError("Missing staged storage commit context");
    }
    Transaction *txn = context->txn_;

    if (context->log_mgr_ != nullptr) {
        for (const StagedWrite &write : commit->writes) {
            const Rid rid = resolve_rid(*commit, write);
            lsn_t lsn = INVALID_LSN;
            if (write.kind == LogicalWriteKind::INSERT) {
                RmRecord after = record_view(write.after);
                InsertLogRecord log(txn->get_transaction_id(), after, rid,
                                    write.table_name);
                log.prev_lsn_ = txn->get_prev_lsn();
                lsn = context->log_mgr_->add_log_to_buffer(&log);
            } else if (write.kind == LogicalWriteKind::UPDATE) {
                RmRecord before = record_view(write.before);
                RmRecord after = record_view(write.after);
                UpdateLogRecord log(txn->get_transaction_id(), before, after,
                                    rid, write.table_name);
                log.prev_lsn_ = txn->get_prev_lsn();
                lsn = context->log_mgr_->add_log_to_buffer(&log);
            } else {
                RmRecord before = record_view(write.before);
                DeleteLogRecord log(txn->get_transaction_id(), before, rid,
                                    write.table_name);
                log.prev_lsn_ = txn->get_prev_lsn();
                lsn = context->log_mgr_->add_log_to_buffer(&log);
            }
            txn->set_prev_lsn(lsn);
            commit->greatest_row_lsn = lsn;
        }
    }

    const bool has_physical_writes = !commit->writes.empty();
    transaction_manager_->begin_storage_apply(txn, has_physical_writes);
    if (!has_physical_writes) {
        return;
    }
    commit->physical_apply_started = true;

    std::vector<size_t> applied;
    Context rollback_context(context->lock_mgr_, nullptr, txn,
                             transaction_manager_);
    try {
        for (size_t index = 0; index < commit->writes.size(); ++index) {
            const StagedWrite &write = commit->writes[index];
            if (write.kind == LogicalWriteKind::INSERT) {
                continue;
            }
            const Rid rid = *write.rid;
            RmFileHandle *file =
                sm_manager_->fhs_.at(write.table_name).get();
            RmRecord before = record_view(write.before);
            applied.push_back(index);
            if (write.kind == LogicalWriteKind::UPDATE) {
                RmRecord after = record_view(write.after);
                file->update_record(rid, after.data, nullptr,
                                    commit->greatest_row_lsn);
                update_indexes(sm_manager_, transaction_manager_,
                               &transaction_manager_->index_versions_,
                               write.table_name, before, after, rid, txn);
            } else {
                delete_indexes(sm_manager_, transaction_manager_,
                               &transaction_manager_->index_versions_,
                               write.table_name, before, rid, txn);
                file->delete_record(rid, nullptr,
                                    commit->greatest_row_lsn);
            }
        }

        std::unordered_map<std::string,
                           std::vector<std::pair<Rid, std::vector<char>>>>
            inserts_by_table;
        for (size_t index = 0; index < commit->writes.size(); ++index) {
            const StagedWrite &write = commit->writes[index];
            if (write.kind != LogicalWriteKind::INSERT) {
                continue;
            }
            applied.push_back(index);
            inserts_by_table[write.table_name].push_back(
                {resolve_rid(*commit, write), write.after});
        }
        for (auto &[table_name, inserts] : inserts_by_table) {
            sm_manager_->fhs_.at(table_name)->apply_reserved_inserts(
                inserts, commit->greatest_row_lsn);
        }
        for (const StagedWrite &write : commit->writes) {
            if (write.kind != LogicalWriteKind::INSERT) {
                continue;
            }
            RmRecord after = record_view(write.after);
            insert_indexes(sm_manager_, write.table_name, after,
                           resolve_rid(*commit, write), txn);
        }
        for (auto &reservation : reservations_) {
            reservation->consume();
        }
    } catch (...) {
        for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
            const StagedWrite &write = commit->writes[*it];
            Rid rid = resolve_rid(*commit, write);
            try {
                if (write.kind == LogicalWriteKind::INSERT) {
                    sm_manager_->rollback_insert(write.table_name, rid,
                                                 &rollback_context);
                } else {
                    RmRecord before = record_view(write.before);
                    if (write.kind == LogicalWriteKind::UPDATE) {
                        sm_manager_->rollback_update(write.table_name, rid,
                                                    before,
                                                    &rollback_context);
                    } else {
                        sm_manager_->rollback_delete(write.table_name, rid,
                                                    before,
                                                    &rollback_context);
                    }
                }
            } catch (...) {
                // The outer abort takes the exceptional recovery-flush lane.
            }
        }
        for (auto &reservation : reservations_) {
            reservation->release();
        }
        throw;
    }
}

void StorageCommitExecutor::rollback(PreparedStorageCommit *commit,
                                     Context *context) noexcept {
    if (commit == nullptr || context == nullptr || context->txn_ == nullptr ||
        !commit->physical_apply_started) {
        return;
    }
    Context rollback_context(context->lock_mgr_, nullptr, context->txn_,
                             transaction_manager_);
    for (auto it = commit->writes.rbegin(); it != commit->writes.rend(); ++it) {
        Rid rid{-1, -1};
        try {
            rid = resolve_rid(*commit, *it);
            if (it->kind == LogicalWriteKind::INSERT) {
                sm_manager_->rollback_insert(it->table_name, rid,
                                             &rollback_context);
            } else {
                RmRecord before = record_view(it->before);
                if (it->kind == LogicalWriteKind::UPDATE) {
                    sm_manager_->rollback_update(it->table_name, rid, before,
                                                &rollback_context);
                } else {
                    sm_manager_->rollback_delete(it->table_name, rid, before,
                                                &rollback_context);
                }
            }
        } catch (...) {
            // The caller follows with the exceptional recovery flush lane.
        }
    }
    for (auto &reservation : reservations_) {
        reservation->release();
    }
}
