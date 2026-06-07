/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include <algorithm>
#include <cstring>
#include "log_manager.h"

/**
 * @description: 添加日志记录到日志缓冲区中，并返回日志记录号
 * @param {LogRecord*} log_record 要写入缓冲区的日志记录
 * @return {lsn_t} 返回该日志的日志记录号
 */
lsn_t LogManager::add_log_to_buffer(LogRecord* log_record) {
    if (log_record == nullptr) {
        throw InternalError("Cannot append a null log record");
    }
    if (log_record->log_tot_len_ > LOG_BUFFER_SIZE) {
        throw InternalError("Log record is larger than the log buffer");
    }

    std::lock_guard<std::mutex> lock(latch_);
    if (log_buffer_.is_full(log_record->log_tot_len_)) {
        flush_log_to_disk_locked();
    }

    log_record->lsn_ = global_lsn_.fetch_add(1);
    log_record->serialize(log_buffer_.buffer_ + log_buffer_.offset_);
    log_buffer_.offset_ += log_record->log_tot_len_;
    return log_record->lsn_;
}

/**
 * @description: 把日志缓冲区的内容刷到磁盘中，由于目前只设置了一个缓冲区，因此需要阻塞其他日志操作
 */
void LogManager::flush_log_to_disk(bool force_sync) {
    std::lock_guard<std::mutex> lock(latch_);
    flush_log_to_disk_locked(force_sync);
}

void LogManager::flush_log_to_disk_locked(bool force_sync) {
    if (log_buffer_.offset_ == 0) {
        if (force_sync) {
            disk_manager_->sync_log();
        }
        return;
    }

    disk_manager_->write_log(log_buffer_.buffer_, log_buffer_.offset_);
    if (force_sync) {
        disk_manager_->sync_log();
    }
    persist_lsn_ = global_lsn_.load() - 1;
    memset(log_buffer_.buffer_, 0, sizeof(log_buffer_.buffer_));
    log_buffer_.offset_ = 0;
}

void LogManager::initialize_from_disk() {
    std::lock_guard<std::mutex> lock(latch_);

    int file_size = disk_manager_->get_file_size(LOG_FILE_NAME);
    if (file_size <= 0) {
        global_lsn_.store(0);
        persist_lsn_ = INVALID_LSN;
        return;
    }

    int offset = 0;
    if (disk_manager_->is_file(RESTART_FILE_NAME)) {
        int64_t candidate = disk_manager_->read_restart_offset();
        if (candidate >= 0 && candidate + LOG_HEADER_SIZE <= file_size) {
            char checkpoint_header[LOG_HEADER_SIZE];
            if (disk_manager_->read_log(
                    checkpoint_header, LOG_HEADER_SIZE,
                    static_cast<int>(candidate)) == LOG_HEADER_SIZE) {
                uint32_t checkpoint_len;
                memcpy(&checkpoint_len,
                       checkpoint_header + OFFSET_LOG_TOT_LEN,
                       sizeof(checkpoint_len));
                if (checkpoint_len >= LOG_HEADER_SIZE &&
                    candidate + checkpoint_len <= file_size) {
                    std::vector<char> checkpoint(checkpoint_len);
                    if (disk_manager_->read_log(
                            checkpoint.data(), checkpoint_len,
                            static_cast<int>(candidate)) ==
                            static_cast<int>(checkpoint_len) &&
                        validate_serialized_log_record(
                            checkpoint.data(), checkpoint_len)) {
                        LogType type;
                        memcpy(&type, checkpoint.data() + OFFSET_LOG_TYPE,
                               sizeof(type));
                        if (type == LogType::CHECKPOINT) {
                            offset = static_cast<int>(candidate);
                        }
                    }
                }
            }
        }
    }

    int valid_end = offset;
    lsn_t max_lsn = INVALID_LSN;
    char header[LOG_HEADER_SIZE];
    while (offset + LOG_HEADER_SIZE <= file_size) {
        int bytes = disk_manager_->read_log(header, LOG_HEADER_SIZE, offset);
        if (bytes != LOG_HEADER_SIZE) {
            break;
        }

        lsn_t lsn;
        uint32_t total_len;
        memcpy(&lsn, header + OFFSET_LSN, sizeof(lsn));
        memcpy(&total_len, header + OFFSET_LOG_TOT_LEN, sizeof(total_len));
        if (total_len < LOG_HEADER_SIZE ||
            static_cast<int64_t>(offset) + total_len > file_size) {
            break;
        }
        std::vector<char> record(total_len);
        if (disk_manager_->read_log(record.data(), total_len, offset) !=
                static_cast<int>(total_len) ||
            !validate_serialized_log_record(record.data(), total_len)) {
            break;
        }
        max_lsn = std::max(max_lsn, lsn);
        offset += static_cast<int>(total_len);
        valid_end = offset;
    }

    if (valid_end < file_size) {
        disk_manager_->truncate_log(valid_end);
    }

    persist_lsn_ = max_lsn;
    global_lsn_.store(max_lsn == INVALID_LSN ? 0 : max_lsn + 1);
}

int64_t LogManager::write_checkpoint_record(
    const std::vector<txn_id_t> &active_txns) {
    std::lock_guard<std::mutex> lock(latch_);

    // The checkpoint starts after every log record generated before it.
    flush_log_to_disk_locked();
    int64_t checkpoint_offset = disk_manager_->get_file_size(LOG_FILE_NAME);
    if (checkpoint_offset < 0) {
        throw InternalError("Cannot determine checkpoint log offset");
    }

    CheckpointLogRecord checkpoint(active_txns);
    if (checkpoint.log_tot_len_ > LOG_BUFFER_SIZE) {
        throw InternalError("Checkpoint record is larger than the log buffer");
    }
    checkpoint.lsn_ = global_lsn_.fetch_add(1);
    checkpoint.serialize(log_buffer_.buffer_);
    log_buffer_.offset_ = checkpoint.log_tot_len_;
    flush_log_to_disk_locked(true);
    return checkpoint_offset;
}

void LogManager::persist_restart_offset(int64_t offset) {
    disk_manager_->write_restart_offset(offset);
}
