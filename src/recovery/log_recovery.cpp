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
#include "transaction/transaction_manager.h"

void RecoveryManager::analyze() {
    logs_.clear();
    txn_states_.clear();
    txn_last_lsns_.clear();
    touched_tables_.clear();
    index_rebuild_tables_.clear();

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
                                restart_offset_ = candidate;
                                scan_start = static_cast<int>(candidate);
                                transaction_manager_->advance_next_txn_id(
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
    while (offset + LOG_HEADER_SIZE <= log_size) {
        char header[LOG_HEADER_SIZE];
        if (disk_manager_->read_log(header, LOG_HEADER_SIZE, offset) != LOG_HEADER_SIZE) {
            break;
        }

        uint32_t total_len;
        memcpy(&total_len, header + OFFSET_LOG_TOT_LEN, sizeof(total_len));
        if (total_len < LOG_HEADER_SIZE ||
            static_cast<int64_t>(offset) + total_len > log_size) {
            break;
        }

        std::vector<char> data(total_len);
        if (disk_manager_->read_log(data.data(), total_len, offset) !=
            static_cast<int>(total_len)) {
            break;
        }
        auto record = deserialize_log_record(data.data(), total_len);
        if (record == nullptr) {
            break;
        }

        txn_id_t txn_id = record->log_tid_;
        if (record->log_type_ != LogType::CHECKPOINT &&
            txn_id != INVALID_TXN_ID) {
            max_txn_id = std::max(max_txn_id, txn_id);
            txn_last_lsns_[txn_id] = record->lsn_;
        }
        switch (record->log_type_) {
            case LogType::begin:
                txn_states_[txn_id] = TxnState::ACTIVE;
                break;
            case LogType::commit:
                txn_states_[txn_id] = TxnState::COMMITTED;
                break;
            case LogType::ABORT:
                txn_states_[txn_id] = TxnState::ABORTED;
                break;
            case LogType::INSERT:
            case LogType::DELETE:
            case LogType::UPDATE:
                if (txn_states_.find(txn_id) == txn_states_.end()) {
                    txn_states_[txn_id] = TxnState::ACTIVE;
                }
                break;
            case LogType::CHECKPOINT:
                break;
        }

        logs_.push_back({offset, std::move(record)});
        offset += static_cast<int>(total_len);
    }

    if (max_txn_id != INVALID_TXN_ID) {
        transaction_manager_->advance_next_txn_id(max_txn_id + 1);
    }
    indexes_from_checkpoint_ =
        restart_offset_ > 0 &&
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
    for (const auto &entry : logs_) {
        if (entry.offset < restart_offset_) {
            continue;
        }

        const LogRecord &base = *entry.record;
        auto state_it = txn_states_.find(base.log_tid_);
        if (state_it != txn_states_.end() &&
            state_it->second == TxnState::ABORTED) {
            // An ABORT record is written only after runtime rollback has
            // restored the database. Replaying its original actions would
            // resurrect changes that were already undone.
            continue;
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
    }
}

void RecoveryManager::undo() {
    std::vector<txn_id_t> recovered_losers;
    recovered_losers.reserve(txn_states_.size());
    for (const auto &entry : txn_states_) {
        if (entry.second == TxnState::ACTIVE) {
            recovered_losers.push_back(entry.first);
        }
    }

    for (auto it = logs_.rbegin(); it != logs_.rend(); ++it) {
        const LogRecord &base = *it->record;
        auto state_it = txn_states_.find(base.log_tid_);
        if (state_it == txn_states_.end() ||
            state_it->second != TxnState::ACTIVE) {
            continue;
        }

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
    for (txn_id_t txn_id : recovered_losers) {
        AbortLogRecord abort_record(txn_id);
        auto last_lsn = txn_last_lsns_.find(txn_id);
        if (last_lsn != txn_last_lsns_.end()) {
            abort_record.prev_lsn_ = last_lsn->second;
        }
        log_manager_->add_log_to_buffer(&abort_record);
    }
    if (!recovered_losers.empty()) {
        log_manager_->flush_log_to_disk();
    }
}

void RecoveryManager::redo_insert(const InsertLogRecord &record) {
    install_record(record.table_name_, record.rid_, record.insert_value_);
}

void RecoveryManager::redo_delete(const DeleteLogRecord &record) {
    remove_record(record.table_name_, record.rid_, &record.delete_value_);
}

void RecoveryManager::redo_update(const UpdateLogRecord &record) {
    install_record(
        record.table_name_, record.rid_, record.new_value_, &record.old_value_);
}

void RecoveryManager::undo_insert(const InsertLogRecord &record) {
    remove_record(record.table_name_, record.rid_, &record.insert_value_);
}

void RecoveryManager::undo_delete(const DeleteLogRecord &record) {
    install_record(record.table_name_, record.rid_, record.delete_value_);
}

void RecoveryManager::undo_update(const UpdateLogRecord &record) {
    install_record(
        record.table_name_, record.rid_, record.old_value_, &record.new_value_);
}

void RecoveryManager::install_record(const std::string &table_name, const Rid &rid,
                                     const RmRecord &record,
                                     const RmRecord *known_old_record) {
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
    file_handle->upsert_record_for_recovery(rid, record.data);
    if (indexes_from_checkpoint_) {
        insert_index_entries(table_name, record, rid);
    }
    touched_tables_.insert(table_name);
}

void RecoveryManager::remove_record(const std::string &table_name, const Rid &rid,
                                    const RmRecord *known_record) {
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
        file_handle->delete_record_for_recovery(rid);
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
