/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "log_recovery.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "errors.h"

namespace {

bool get_action_target(const LogRecord &record, std::string *table_name,
                       Rid *rid) {
    switch (record.log_type_) {
        case LogType::INSERT: {
            const auto &insert_record =
                static_cast<const InsertLogRecord &>(record);
            *table_name = insert_record.table_name_;
            *rid = insert_record.rid_;
            return true;
        }
        case LogType::DELETE: {
            const auto &delete_record =
                static_cast<const DeleteLogRecord &>(record);
            *table_name = delete_record.table_name_;
            *rid = delete_record.rid_;
            return true;
        }
        case LogType::UPDATE: {
            const auto &update_record =
                static_cast<const UpdateLogRecord &>(record);
            *table_name = update_record.table_name_;
            *rid = update_record.rid_;
            return true;
        }
        default:
            return false;
    }
}

}  // namespace

std::unique_ptr<LogRecord> RecoveryManager::read_log_record(
    int64_t offset, int64_t log_end, int64_t *next_offset) const {
    if (offset < 0 || offset + LOG_HEADER_SIZE > log_end) {
        return nullptr;
    }

    char header[LOG_HEADER_SIZE];
    if (disk_manager_->read_log(
            header, LOG_HEADER_SIZE, static_cast<int>(offset)) !=
        LOG_HEADER_SIZE) {
        return nullptr;
    }

    uint32_t total_len;
    memcpy(&total_len, header + OFFSET_LOG_TOT_LEN, sizeof(total_len));
    if (total_len < LOG_HEADER_SIZE || offset + total_len > log_end) {
        return nullptr;
    }

    std::vector<char> data(total_len);
    if (disk_manager_->read_log(
            data.data(), total_len, static_cast<int>(offset)) !=
        static_cast<int>(total_len)) {
        return nullptr;
    }

    auto record = deserialize_log_record(data.data(), total_len);
    if (record != nullptr && next_offset != nullptr) {
        *next_offset = offset + total_len;
    }
    return record;
}

void RecoveryManager::analyze() {
    active_txns_.clear();
    aborted_txns_.clear();
    active_last_lsns_.clear();
    loser_action_offsets_.clear();
    touched_tables_.clear();
    index_rebuild_tables_.clear();
    next_txn_id_ = 0;
    valid_log_end_ = 0;
    has_valid_checkpoint_ = false;
    indexes_from_checkpoint_ = false;

    const int log_size = disk_manager_->get_file_size(LOG_FILE_NAME);
    if (log_size <= 0) {
        restart_offset_ = 0;
        return;
    }

    restart_offset_ = 0;
    int scan_start = 0;
    if (disk_manager_->is_file(RESTART_FILE_NAME)) {
        int64_t candidate = disk_manager_->read_restart_offset();
        if (candidate >= 0 && candidate + LOG_HEADER_SIZE <= log_size) {
            char header[LOG_HEADER_SIZE];
            if (disk_manager_->read_log(
                    header, LOG_HEADER_SIZE, static_cast<int>(candidate)) ==
                LOG_HEADER_SIZE) {
                uint32_t total_len;
                memcpy(&total_len, header + OFFSET_LOG_TOT_LEN, sizeof(total_len));
                if (total_len >= LOG_HEADER_SIZE &&
                    candidate + total_len <= log_size) {
                    std::vector<char> data(total_len);
                    if (disk_manager_->read_log(
                            data.data(), total_len, static_cast<int>(candidate)) ==
                        static_cast<int>(total_len)) {
                        auto checkpoint =
                            deserialize_log_record(data.data(), total_len);
                        if (checkpoint != nullptr &&
                            checkpoint->log_type_ == LogType::CHECKPOINT) {
                            auto *checkpoint_record =
                                static_cast<CheckpointLogRecord *>(checkpoint.get());
                            if (checkpoint_record->active_txns_.empty()) {
                                has_valid_checkpoint_ = true;
                                restart_offset_ = candidate;
                                scan_start = static_cast<int>(candidate);
                                next_txn_id_ = std::max(
                                    next_txn_id_,
                                    static_cast<txn_id_t>(
                                        checkpoint_record->lsn_) + 1);
                            }
                        }
                    }
                }
            }
        }
    }

    int offset = scan_start;
    txn_id_t max_txn_id = INVALID_TXN_ID;
    while (offset < log_size) {
        int64_t next_offset = offset;
        auto record = read_log_record(offset, log_size, &next_offset);
        if (record == nullptr) {
            break;
        }

        txn_id_t txn_id = record->log_tid_;
        if (record->log_type_ != LogType::CHECKPOINT &&
            txn_id != INVALID_TXN_ID) {
            max_txn_id = std::max(max_txn_id, txn_id);
        }
        switch (record->log_type_) {
            case LogType::begin:
                aborted_txns_.erase(txn_id);
                active_txns_.insert(txn_id);
                active_last_lsns_[txn_id] = record->lsn_;
                break;
            case LogType::commit:
                active_txns_.erase(txn_id);
                aborted_txns_.erase(txn_id);
                active_last_lsns_.erase(txn_id);
                break;
            case LogType::ABORT:
                active_txns_.erase(txn_id);
                active_last_lsns_.erase(txn_id);
                aborted_txns_.insert(txn_id);
                break;
            case LogType::INSERT:
            case LogType::DELETE:
            case LogType::UPDATE:
                if (aborted_txns_.find(txn_id) == aborted_txns_.end()) {
                    active_txns_.insert(txn_id);
                    active_last_lsns_[txn_id] = record->lsn_;
                }
                break;
            case LogType::CHECKPOINT:
                break;
        }

        offset = static_cast<int>(next_offset);
    }
    valid_log_end_ = offset;

    if (max_txn_id != INVALID_TXN_ID) {
        next_txn_id_ = std::max(next_txn_id_, max_txn_id + 1);
    }
    indexes_from_checkpoint_ =
        has_valid_checkpoint_ &&
        sm_manager_->restore_index_snapshots(restart_offset_);
    if (!indexes_from_checkpoint_) {
        for (const auto &entry : sm_manager_->fhs_) {
            if (!sm_manager_->db_.get_table(entry.first).indexes.empty()) {
                index_rebuild_tables_.insert(entry.first);
            }
        }
    }
}

void RecoveryManager::redo() {
    loser_action_offsets_.clear();
    int64_t offset = restart_offset_;
    while (offset < valid_log_end_) {
        int64_t next_offset = offset;
        auto record = read_log_record(offset, valid_log_end_, &next_offset);
        if (record == nullptr) {
            break;
        }

        const LogRecord &base = *record;
        bool is_action = base.log_type_ == LogType::INSERT ||
                         base.log_type_ == LogType::DELETE ||
                         base.log_type_ == LogType::UPDATE;
        if (is_action &&
            active_txns_.find(base.log_tid_) != active_txns_.end()) {
            loser_action_offsets_.push_back(offset);
        }
        if (aborted_txns_.find(base.log_tid_) != aborted_txns_.end()) {
            // An ABORT record is written only after runtime rollback has
            // restored the database. Replaying its original actions would
            // resurrect changes that were already undone.
            offset = next_offset;
            continue;
        }
        if (is_action && !indexes_from_checkpoint_) {
            std::string table_name;
            Rid rid;
            if (get_action_target(base, &table_name, &rid) &&
                sm_manager_->db_.is_table(table_name)) {
                auto file_handle = sm_manager_->fhs_.at(table_name).get();
                lsn_t page_lsn = file_handle->get_page_lsn(rid.page_no);
                if (base.lsn_ > 0 && page_lsn >= base.lsn_) {
                    touched_tables_.insert(table_name);
                    offset = next_offset;
                    continue;
                }
            }
        }
        switch (base.log_type_) {
            case LogType::INSERT:
                redo_insert(static_cast<const InsertLogRecord &>(base));
                break;
            case LogType::DELETE:
                redo_delete(static_cast<const DeleteLogRecord &>(base));
                break;
            case LogType::UPDATE:
                redo_update(static_cast<const UpdateLogRecord &>(base));
                break;
            default:
                break;
        }
        offset = next_offset;
    }
}

void RecoveryManager::undo() {
    std::vector<txn_id_t> recovered_losers(
        active_txns_.begin(), active_txns_.end());

    for (auto it = loser_action_offsets_.rbegin();
         it != loser_action_offsets_.rend(); ++it) {
        auto record = read_log_record(*it, valid_log_end_, nullptr);
        if (record == nullptr) {
            continue;
        }

        const LogRecord &base = *record;
        switch (base.log_type_) {
            case LogType::INSERT:
                undo_insert(static_cast<const InsertLogRecord &>(base));
                break;
            case LogType::DELETE:
                undo_delete(static_cast<const DeleteLogRecord &>(base));
                break;
            case LogType::UPDATE:
                undo_update(static_cast<const UpdateLogRecord &>(base));
                break;
            default:
                break;
        }
    }
    finish_recovery();

    // The framework has no compensation log records. Persist the completed
    // undo before marking each loser aborted, so a crash can safely retry
    // undo until the ABORT record becomes durable.
    if (log_manager_ != nullptr) {
        for (txn_id_t txn_id : recovered_losers) {
            AbortLogRecord abort_record(txn_id);
            auto last_lsn = active_last_lsns_.find(txn_id);
            if (last_lsn != active_last_lsns_.end()) {
                abort_record.prev_lsn_ = last_lsn->second;
            }
            log_manager_->add_log_to_buffer(&abort_record);
        }
        if (!recovered_losers.empty()) {
            log_manager_->flush_log_to_disk(true);
        }
    }
}

void RecoveryManager::redo_insert(const InsertLogRecord &record) {
    install_record(record.table_name_, record.rid_, record.insert_value_,
                   nullptr, record.lsn_);
}

void RecoveryManager::redo_delete(const DeleteLogRecord &record) {
    remove_record(record.table_name_, record.rid_, &record.delete_value_,
                  record.lsn_);
}

void RecoveryManager::redo_update(const UpdateLogRecord &record) {
    install_record(
        record.table_name_, record.rid_, record.new_value_, &record.old_value_,
        record.lsn_);
}

void RecoveryManager::undo_insert(const InsertLogRecord &record) {
    remove_record(record.table_name_, record.rid_, &record.insert_value_,
                  record.lsn_);
}

void RecoveryManager::undo_delete(const DeleteLogRecord &record) {
    install_record(record.table_name_, record.rid_, record.delete_value_,
                   nullptr, record.lsn_);
}

void RecoveryManager::undo_update(const UpdateLogRecord &record) {
    install_record(
        record.table_name_, record.rid_, record.old_value_, &record.new_value_,
        record.lsn_);
}

void RecoveryManager::install_record(const std::string &table_name, const Rid &rid,
                                     const RmRecord &record,
                                     const RmRecord *known_old_record,
                                     lsn_t page_lsn) {
    if (!sm_manager_->db_.is_table(table_name)) {
        throw InternalError("Recovery log references missing table " + table_name);
    }
    RmFileHandle *file_handle = sm_manager_->fhs_.at(table_name).get();
    if (record.size != file_handle->get_file_hdr().record_size) {
        throw InternalError("Recovery record size does not match table schema");
    }

    if (indexes_from_checkpoint_) {
        if (known_old_record != nullptr) {
            delete_index_entries(table_name, *known_old_record, rid);
        }
        if (file_handle->record_exists(rid)) {
            auto current = file_handle->get_record(rid, nullptr);
            delete_index_entries(table_name, *current, rid);
        }
    }
    file_handle->upsert_record_for_recovery(rid, record.data, page_lsn);
    if (indexes_from_checkpoint_) {
        insert_index_entries(table_name, record, rid);
    }
    touched_tables_.insert(table_name);
}

void RecoveryManager::remove_record(const std::string &table_name, const Rid &rid,
                                    const RmRecord *known_record,
                                    lsn_t page_lsn) {
    if (!sm_manager_->db_.is_table(table_name)) {
        throw InternalError("Recovery log references missing table " + table_name);
    }
    RmFileHandle *file_handle = sm_manager_->fhs_.at(table_name).get();
    if (indexes_from_checkpoint_ && known_record != nullptr) {
        delete_index_entries(table_name, *known_record, rid);
    }
    if (file_handle->record_exists(rid)) {
        if (indexes_from_checkpoint_) {
            auto current = file_handle->get_record(rid, nullptr);
            delete_index_entries(table_name, *current, rid);
        }
        file_handle->delete_record_for_recovery(rid, page_lsn);
    }
    touched_tables_.insert(table_name);
}

void RecoveryManager::insert_index_entries(const std::string &table_name,
                                           const RmRecord &record,
                                           const Rid &rid) {
    const TabMeta &table = sm_manager_->db_.get_table(table_name);
    for (const auto &index : table.indexes) {
        std::vector<char> key(index.col_tot_len);
        int offset = 0;
        for (const auto &col : index.cols) {
            memcpy(key.data() + offset, record.data + col.offset, col.len);
            offset += col.len;
        }
        const std::string index_name =
            sm_manager_->get_ix_manager()->get_index_name(table_name, index.cols);
        IxIndexHandle *index_handle = sm_manager_->ihs_.at(index_name).get();
        std::vector<Rid> existing;
        if (!index_handle->get_value(key.data(), &existing, nullptr)) {
            index_handle->insert_entry(key.data(), rid, nullptr);
        } else if (existing.front() != rid) {
            throw InternalError("Unique index conflict during recovery");
        }
    }
}

void RecoveryManager::delete_index_entries(const std::string &table_name,
                                           const RmRecord &record,
                                           const Rid &rid) {
    const TabMeta &table = sm_manager_->db_.get_table(table_name);
    for (const auto &index : table.indexes) {
        std::vector<char> key(index.col_tot_len);
        int offset = 0;
        for (const auto &col : index.cols) {
            memcpy(key.data() + offset, record.data + col.offset, col.len);
            offset += col.len;
        }
        const std::string index_name =
            sm_manager_->get_ix_manager()->get_index_name(table_name, index.cols);
        IxIndexHandle *index_handle = sm_manager_->ihs_.at(index_name).get();
        std::vector<Rid> existing;
        if (index_handle->get_value(key.data(), &existing, nullptr) &&
            existing.front() == rid) {
            index_handle->delete_entry(key.data(), nullptr);
        }
    }
}

void RecoveryManager::finish_recovery() {
    for (const auto &table_name : touched_tables_) {
        sm_manager_->fhs_.at(table_name)->rebuild_free_page_list();
    }
    if (!indexes_from_checkpoint_) {
        sm_manager_->rebuild_indexes_for_recovery(index_rebuild_tables_);
    }
    sm_manager_->flush_for_checkpoint();
}
