#include "transaction/storage_commit_executor.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <unordered_map>
#include <utility>

#include "common/perf_counters.h"
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

    commit->row_lsns.assign(commit->writes.size(), INVALID_LSN);
    if (context->log_mgr_ != nullptr && !commit->writes.empty()) {
        std::vector<std::unique_ptr<LogRecord>> owned_logs;
        std::vector<LogRecord *> logs;
        owned_logs.reserve(commit->writes.size());
        logs.reserve(commit->writes.size());
        for (const StagedWrite &write : commit->writes) {
            const Rid rid = resolve_rid(*commit, write);
            std::unique_ptr<LogRecord> log;
            if (write.kind == LogicalWriteKind::INSERT) {
                RmRecord after = record_view(write.after);
                log = std::make_unique<InsertLogRecord>(
                    txn->get_transaction_id(), after, rid, write.table_name);
            } else if (write.kind == LogicalWriteKind::UPDATE) {
                RmRecord before = record_view(write.before);
                RmRecord after = record_view(write.after);
                log = std::make_unique<UpdateLogRecord>(
                    txn->get_transaction_id(), before, after, rid,
                    write.table_name);
            } else {
                RmRecord before = record_view(write.before);
                log = std::make_unique<DeleteLogRecord>(
                    txn->get_transaction_id(), before, rid,
                    write.table_name);
            }
            log->prev_lsn_ = logs.empty() ? txn->get_prev_lsn() : INVALID_LSN;
            logs.push_back(log.get());
            owned_logs.push_back(std::move(log));
        }
        if (rmdb_perf::enabled()) {
            rmdb_perf::shared_counters().transaction_row_wal_sets.fetch_add(
                1, std::memory_order_relaxed);
        }
        commit->row_lsns = context->log_mgr_->add_logs_to_buffer(logs);
        if (commit->row_lsns.size() != commit->writes.size()) {
            throw InternalError("Row WAL batch returned the wrong LSN count");
        }
        commit->greatest_row_lsn = commit->row_lsns.back();
        txn->set_prev_lsn(commit->greatest_row_lsn);
    }

    const bool has_physical_writes = !commit->writes.empty();
    transaction_manager_->begin_storage_apply(txn, has_physical_writes);
    if (!has_physical_writes) {
        return;
    }
    commit->physical_apply_started = true;

    struct HeapPageKey {
        int fd;
        page_id_t page_no;
        bool operator<(const HeapPageKey &other) const {
            return fd < other.fd ||
                   (fd == other.fd && page_no < other.page_no);
        }
    };
    struct HeapPageWork {
        RmFileHandle *file = nullptr;
        HeapPageMutationBatch batch;
        lsn_t page_lsn = INVALID_LSN;
        std::vector<size_t> write_indexes;
    };
    struct IndexWork {
        IxIndexHandle *handle = nullptr;
        std::vector<IndexMutation> mutations;
    };

    std::map<HeapPageKey, HeapPageWork> heap_pages;
    std::map<int, IndexWork> indexes;
    for (size_t index = 0; index < commit->writes.size(); ++index) {
        const StagedWrite &write = commit->writes[index];
        const Rid rid = resolve_rid(*commit, write);
        RmFileHandle *file = sm_manager_->fhs_.at(write.table_name).get();
        HeapPageKey page_key{file->GetFd(), rid.page_no};
        HeapPageWork &page = heap_pages[page_key];
        page.file = file;
        page.batch.fd = file->GetFd();
        page.batch.page_no = rid.page_no;
        HeapMutationKind heap_kind = HeapMutationKind::DELETE;
        if (write.kind == LogicalWriteKind::INSERT) {
            heap_kind = HeapMutationKind::INSERT;
        } else if (write.kind == LogicalWriteKind::UPDATE) {
            heap_kind = HeapMutationKind::UPDATE;
        }
        page.batch.mutations.push_back(
            HeapMutation{heap_kind, rid, write.before, write.after});
        page.write_indexes.push_back(index);
        page.page_lsn = std::max(page.page_lsn, commit->row_lsns[index]);

        const TabMeta &table = sm_manager_->db_.get_table(write.table_name);
        for (const IndexMeta &meta : table.indexes) {
            const std::string name =
                sm_manager_->get_ix_manager()->get_index_name(
                    write.table_name, meta.cols);
            IxIndexHandle *handle = sm_manager_->ihs_.at(name).get();
            IndexWork &work = indexes[handle->GetFd()];
            work.handle = handle;
            if (write.kind == LogicalWriteKind::INSERT) {
                RmRecord after = record_view(write.after);
                work.mutations.push_back(IndexMutation{
                    IndexMutationKind::INSERT, index_key(after, meta), rid});
                continue;
            }

            RmRecord before = record_view(write.before);
            std::vector<char> old_key = index_key(before, meta);
            if (write.kind == LogicalWriteKind::DELETE) {
                std::vector<Rid> indexed;
                if (!handle->get_value(old_key.data(), &indexed, txn) ||
                    std::find(indexed.begin(), indexed.end(), rid) ==
                        indexed.end()) {
                    continue;
                }
                transaction_manager_->acquire_unique_key_intent(
                    txn, handle->GetFd(), old_key);
                transaction_manager_->index_versions_.retain(
                    handle->GetFd(), old_key, rid, txn->get_control());
                work.mutations.push_back(IndexMutation{
                    IndexMutationKind::DELETE, std::move(old_key), rid});
                continue;
            }

            RmRecord after = record_view(write.after);
            std::vector<char> new_key = index_key(after, meta);
            if (old_key != new_key) {
                transaction_manager_->acquire_unique_key_intent(
                    txn, handle->GetFd(), old_key);
                transaction_manager_->index_versions_.retain(
                    handle->GetFd(), old_key, rid, txn->get_control());
                work.mutations.push_back(IndexMutation{
                    IndexMutationKind::DELETE, std::move(old_key), rid});
                work.mutations.push_back(IndexMutation{
                    IndexMutationKind::INSERT, std::move(new_key), rid});
            }
        }
    }
    for (auto &[key, page] : heap_pages) {
        (void)key;
        std::sort(page.batch.mutations.begin(), page.batch.mutations.end(),
                  [](const HeapMutation &left, const HeapMutation &right) {
                      return left.rid.slot_no < right.rid.slot_no;
                  });
    }

    std::vector<size_t> applied;
    Context rollback_context(context->lock_mgr_, nullptr, txn,
                             transaction_manager_);
    try {
        for (auto &[key, page] : heap_pages) {
            (void)key;
            page.file->apply_page_batch(page.batch, page.page_lsn);
            applied.insert(applied.end(), page.write_indexes.begin(),
                           page.write_indexes.end());
        }
        for (auto &[index_fd, work] : indexes) {
            (void)index_fd;
            if (!work.mutations.empty()) {
                work.handle->apply_sorted_batch(std::move(work.mutations),
                                                txn);
            }
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
