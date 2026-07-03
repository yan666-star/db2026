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

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "log_manager.h"
#include "storage/disk_manager.h"
#include "system/sm_manager.h"

class RecoveryManager {
public:
    RecoveryManager(DiskManager *disk_manager,
                    BufferPoolManager *buffer_pool_manager,
                    SmManager *sm_manager)
        : disk_manager_(disk_manager),
          sm_manager_(sm_manager),
          log_manager_(nullptr) {
        (void)buffer_pool_manager;
    }

    RecoveryManager(DiskManager *disk_manager,
                    SmManager *sm_manager,
                    LogManager *log_manager)
        : disk_manager_(disk_manager),
          sm_manager_(sm_manager),
          log_manager_(log_manager) {}

    void analyze();
    void redo();
    void undo();
    void set_log_manager(LogManager *log_manager) {
        log_manager_ = log_manager;
    }
    int64_t get_restart_offset() const { return restart_offset_; }
    txn_id_t get_next_txn_id() const { return next_txn_id_; }

private:
    std::unique_ptr<LogRecord> read_log_record(int64_t offset,
                                               int64_t log_end,
                                               int64_t *next_offset) const;
    void redo_insert(const InsertLogRecord &record);
    void redo_delete(const DeleteLogRecord &record);
    void redo_update(const UpdateLogRecord &record);
    void undo_insert(const InsertLogRecord &record);
    void undo_delete(const DeleteLogRecord &record);
    void undo_update(const UpdateLogRecord &record);
    void install_record(const std::string &table_name, const Rid &rid,
                        const RmRecord &record,
                        const RmRecord *known_old_record = nullptr);
    void remove_record(const std::string &table_name, const Rid &rid,
                       const RmRecord *known_record = nullptr);
    void insert_index_entries(const std::string &table_name,
                              const RmRecord &record, const Rid &rid);
    void delete_index_entries(const std::string &table_name,
                              const RmRecord &record, const Rid &rid);
    void finish_recovery();

    DiskManager *disk_manager_;
    SmManager *sm_manager_;
    LogManager *log_manager_;
    int64_t restart_offset_ = 0;
    int64_t valid_log_end_ = 0;
    txn_id_t next_txn_id_ = 0;
    std::unordered_set<txn_id_t> active_txns_;
    std::unordered_set<txn_id_t> aborted_txns_;
    std::unordered_map<txn_id_t, lsn_t> active_last_lsns_;
    std::vector<int64_t> loser_action_offsets_;
    std::unordered_set<std::string> touched_tables_;
    std::unordered_set<std::string> index_rebuild_tables_;
    bool has_valid_checkpoint_ = false;
    bool indexes_from_checkpoint_ = false;
};
