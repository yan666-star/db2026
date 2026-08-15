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
#include <chrono>
#include <cstring>
#include <cstdlib>
#include "common/perf_counters.h"
#include "log_manager.h"
#include "log_record_scanner.h"

namespace {

std::chrono::microseconds group_commit_delay() {
    static const auto delay = [] {
        // Give concurrent committers a short opportunity to append their
        // commit records before the leader performs the audited write+fsync.
        // Every waiter still blocks until durable_lsn_ covers its own LSN.
        constexpr unsigned long long kDefaultMicros = 1000;
        constexpr unsigned long long kMaxMicros = 5000;
        const char *raw = std::getenv("RMDB_GROUP_COMMIT_US");
        if (raw == nullptr || raw[0] == '\0') {
            return std::chrono::microseconds(kDefaultMicros);
        }
        char *end = nullptr;
        unsigned long long value = std::strtoull(raw, &end, 10);
        if (end == raw || *end != '\0' || value > kMaxMicros) {
            return std::chrono::microseconds(kDefaultMicros);
        }
        return std::chrono::microseconds(value);
    }();
    return delay;
}

}  // namespace

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

    if (rmdb_perf::enabled() &&
        (log_record->log_type_ == LogType::INSERT ||
         log_record->log_type_ == LogType::UPDATE ||
         log_record->log_type_ == LogType::DELETE)) {
        rmdb_perf::shared_counters().wal_row_append_latch_acquires.fetch_add(
            1, std::memory_order_relaxed);
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

std::vector<lsn_t> LogManager::add_logs_to_buffer(
    const std::vector<LogRecord *> &log_records) {
    for (const LogRecord *record : log_records) {
        if (record == nullptr) {
            throw InternalError("Cannot append a null log record");
        }
        if (record->log_tot_len_ > LOG_BUFFER_SIZE) {
            throw InternalError("Log record is larger than the log buffer");
        }
    }

    if (rmdb_perf::enabled() && !log_records.empty()) {
        rmdb_perf::shared_counters().wal_row_append_latch_acquires.fetch_add(
            1, std::memory_order_relaxed);
    }
    std::vector<lsn_t> lsns;
    lsns.reserve(log_records.size());
    std::lock_guard<std::mutex> lock(latch_);
    LogRecord *previous = nullptr;
    for (LogRecord *record : log_records) {
        if (log_buffer_.is_full(record->log_tot_len_)) {
            flush_log_to_disk_locked();
        }
        record->lsn_ = global_lsn_.fetch_add(1);
        if (previous != nullptr && previous->log_tid_ == record->log_tid_) {
            record->prev_lsn_ = previous->lsn_;
        }
        record->serialize(log_buffer_.buffer_ + log_buffer_.offset_);
        log_buffer_.offset_ += record->log_tot_len_;
        lsns.push_back(record->lsn_);
        previous = record;
    }
    return lsns;
}

/**
 * @description: 把日志缓冲区的内容刷到磁盘中，由于目前只设置了一个缓冲区，因此需要阻塞其他日志操作
 */
void LogManager::flush_log_to_disk(bool force_sync) {
    std::unique_lock<std::mutex> lock(latch_);
    flush_log_to_disk_locked(force_sync);
    if (force_sync) {
        durable_cv_.notify_all();
    }
}

void LogManager::flush_log_to_disk_locked(bool force_sync) {
    if (log_buffer_.offset_ != 0) {
        disk_manager_->write_log(log_buffer_.buffer_, log_buffer_.offset_);
        written_lsn_ = global_lsn_.load() - 1;
        log_buffer_.offset_ = 0;
    }

    // A dirty-page eviction may ask for WAL durability many times after the
    // same WAL prefix has already been synced.  Repeating fsync without a new
    // WAL write cannot advance the durable boundary and is especially costly
    // during indexed LOAD.  Only sync when there is a written-but-not-yet-
    // durable LSN; COMMIT still waits for durable_lsn_ to cover its own LSN.
    if (force_sync && durable_lsn_ < written_lsn_) {
        if (rmdb_perf::enabled()) {
            auto start = std::chrono::steady_clock::now();
            disk_manager_->sync_log();
            auto elapsed =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count();
            rmdb_perf::record(rmdb_perf::Metric::WAL_FSYNC,
                              static_cast<uint64_t>(elapsed));
        } else {
            disk_manager_->sync_log();
        }
        durable_lsn_ = written_lsn_;
    }
}

void LogManager::force_flush_up_to(lsn_t target_lsn) {
    if (target_lsn == INVALID_LSN) {
        throw InternalError("Cannot force an invalid WAL LSN");
    }

    const bool diagnose = rmdb_perf::enabled();
    const auto wait_start = diagnose ? std::chrono::steady_clock::now()
                                     : std::chrono::steady_clock::time_point{};
    std::unique_lock<std::mutex> lock(latch_);
    ++force_flush_waiters_;
    try {
        while (durable_lsn_ < target_lsn) {
            if (group_flush_in_progress_) {
                durable_cv_.wait(lock, [&] {
                    return durable_lsn_ >= target_lsn ||
                           !group_flush_in_progress_;
                });
                continue;
            }

            // 首个到达者成为本轮 group commit leader。短暂释放 latch，让其他
            // 已完成逻辑提交的线程把 commit record 追加到同一 WAL buffer。
            group_flush_in_progress_ = true;
            try {
                durable_cv_.wait_for(lock, group_commit_delay(), [&] {
                    return durable_lsn_ >= target_lsn;
                });
                if (durable_lsn_ < target_lsn) {
                    if (diagnose) {
                        rmdb_perf::record(
                            rmdb_perf::Metric::WAL_GROUP_SIZE,
                            static_cast<uint64_t>(force_flush_waiters_));
                    }
                    // 同一 ACK 窗口内先产生 WAL 正字节写入，再 fsync 同一 fd；
                    // 返回后 durable_lsn_ 覆盖本事务的 commit record。
                    flush_log_to_disk_locked(true);
                }
                group_flush_in_progress_ = false;
                durable_cv_.notify_all();
            } catch (...) {
                group_flush_in_progress_ = false;
                durable_cv_.notify_all();
                throw;
            }
        }
        --force_flush_waiters_;
    } catch (...) {
        --force_flush_waiters_;
        throw;
    }
    lock.unlock();
    if (diagnose) {
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now() - wait_start)
                           .count();
        rmdb_perf::record(rmdb_perf::Metric::WAL_DURABLE_WAIT,
                          static_cast<uint64_t>(elapsed));
    }
}

lsn_t LogManager::durable_lsn() {
    std::lock_guard<std::mutex> lock(latch_);
    return durable_lsn_;
}

void LogManager::initialize_from_disk() {
    std::lock_guard<std::mutex> lock(latch_);

    const int64_t file_size = disk_manager_->get_file_size(LOG_FILE_NAME);
    if (file_size <= 0) {
        global_lsn_.store(0);
        written_lsn_ = INVALID_LSN;
        durable_lsn_ = INVALID_LSN;
        return;
    }

    int64_t offset = 0;
    if (disk_manager_->is_file(RESTART_FILE_NAME)) {
        int64_t candidate = disk_manager_->read_restart_offset();
        if (candidate >= 0 &&
            candidate <= file_size - LOG_HEADER_SIZE) {
            char checkpoint_header[LOG_HEADER_SIZE];
            if (disk_manager_->read_log(
                    checkpoint_header, LOG_HEADER_SIZE,
                    candidate) == LOG_HEADER_SIZE) {
                uint32_t checkpoint_len;
                memcpy(&checkpoint_len,
                       checkpoint_header + OFFSET_LOG_TOT_LEN,
                       sizeof(checkpoint_len));
                if (checkpoint_len >= LOG_HEADER_SIZE &&
                    candidate <= file_size - checkpoint_len) {
                    std::vector<char> checkpoint(checkpoint_len);
                    if (disk_manager_->read_log(
                            checkpoint.data(), checkpoint_len,
                            candidate) ==
                            static_cast<int>(checkpoint_len) &&
                        validate_serialized_log_record(
                            checkpoint.data(), checkpoint_len)) {
                        LogType type;
                        memcpy(&type, checkpoint.data() + OFFSET_LOG_TYPE,
                               sizeof(type));
                        if (type == LogType::CHECKPOINT) {
                            offset = candidate;
                        }
                    }
                }
            }
        }
    }

    lsn_t max_lsn = INVALID_LSN;
    LogRecordScanner scanner(disk_manager_, offset, file_size);
    while (auto scanned = scanner.next()) {
        max_lsn = std::max(max_lsn, scanned->record->lsn_);
    }
    const int64_t valid_end = scanner.valid_end();

    if (valid_end < file_size) {
        disk_manager_->truncate_log(valid_end);
    }

    written_lsn_ = max_lsn;
    durable_lsn_ = max_lsn;
    global_lsn_.store(max_lsn == INVALID_LSN ? 0 : max_lsn + 1);
}

void LogManager::initialize_from_recovery_scan(int64_t valid_log_end,
                                               lsn_t max_lsn) {
    std::lock_guard<std::mutex> lock(latch_);
    const int64_t file_size = disk_manager_->get_file_size(LOG_FILE_NAME);
    if (file_size < 0 || valid_log_end < 0 || valid_log_end > file_size) {
        throw InternalError("Invalid WAL recovery scan result");
    }
    if (log_buffer_.offset_ != 0) {
        throw InternalError("Cannot adopt WAL scan after buffered appends");
    }
    if (valid_log_end < file_size) {
        disk_manager_->truncate_log(valid_log_end);
    }
    written_lsn_ = max_lsn;
    durable_lsn_ = max_lsn;
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
