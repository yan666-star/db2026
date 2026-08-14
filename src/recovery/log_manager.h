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

#include <condition_variable>
#include <mutex>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "log_defs.h"
#include "common/config.h"
#include "record/rm_defs.h"

/* 日志记录对应操作的类型 */
enum LogType: int {
    UPDATE = 0,
    INSERT,
    DELETE,
    begin,
    commit,
    ABORT,
    CHECKPOINT
};
static_assert(sizeof(LogType) == sizeof(int));
class LogRecord {
public:
    LogType log_type_;         /* 日志对应操作的类型 */
    lsn_t lsn_;                /* 当前日志的lsn */
    uint32_t log_tot_len_;     /* 整个日志记录的长度 */
    txn_id_t log_tid_;         /* 创建当前日志的事务ID */
    lsn_t prev_lsn_;           /* 事务创建的前一条日志记录的lsn，用于undo */

    virtual ~LogRecord() = default;

    // 把日志记录序列化到dest中
    virtual void serialize (char* dest) const {
        memcpy(dest + OFFSET_LOG_TYPE, &log_type_, sizeof(LogType));
        memcpy(dest + OFFSET_LSN, &lsn_, sizeof(lsn_t));
        memcpy(dest + OFFSET_LOG_TOT_LEN, &log_tot_len_, sizeof(uint32_t));
        memcpy(dest + OFFSET_LOG_TID, &log_tid_, sizeof(txn_id_t));
        memcpy(dest + OFFSET_PREV_LSN, &prev_lsn_, sizeof(lsn_t));
    }
    // 从src中反序列化出一条日志记录
    virtual void deserialize(const char* src) {
        memcpy(&log_type_, src + OFFSET_LOG_TYPE, sizeof(log_type_));
        memcpy(&lsn_, src + OFFSET_LSN, sizeof(lsn_));
        memcpy(&log_tot_len_, src + OFFSET_LOG_TOT_LEN, sizeof(log_tot_len_));
        memcpy(&log_tid_, src + OFFSET_LOG_TID, sizeof(log_tid_));
        memcpy(&prev_lsn_, src + OFFSET_PREV_LSN, sizeof(prev_lsn_));
    }
};

inline uint32_t serialized_record_size(const RmRecord &record) {
    return sizeof(uint32_t) + static_cast<uint32_t>(record.size);
}

inline uint32_t serialize_record(char *dest, const RmRecord &record) {
    uint32_t size = static_cast<uint32_t>(record.size);
    memcpy(dest, &size, sizeof(size));
    if (size > 0) {
        memcpy(dest + sizeof(size), record.data, size);
    }
    return sizeof(size) + size;
}

inline uint32_t deserialize_record(const char *src, RmRecord &record) {
    uint32_t size;
    memcpy(&size, src, sizeof(size));
    record = RmRecord(static_cast<int>(size));
    if (size > 0) {
        memcpy(record.data, src + sizeof(size), size);
    }
    return sizeof(size) + size;
}

inline uint32_t serialized_string_size(const std::string &value) {
    return sizeof(uint32_t) + static_cast<uint32_t>(value.size());
}

inline uint32_t serialize_string(char *dest, const std::string &value) {
    uint32_t size = static_cast<uint32_t>(value.size());
    memcpy(dest, &size, sizeof(size));
    if (size > 0) {
        memcpy(dest + sizeof(size), value.data(), size);
    }
    return sizeof(size) + size;
}

inline uint32_t deserialize_string(const char *src, std::string &value) {
    uint32_t size;
    memcpy(&size, src, sizeof(size));
    value.assign(src + sizeof(size), size);
    return sizeof(size) + size;
}

class BeginLogRecord: public LogRecord {
public:
    BeginLogRecord() {
        log_type_ = LogType::begin;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }
    BeginLogRecord(txn_id_t txn_id) : BeginLogRecord() {
        log_tid_ = txn_id;
    }
    // 序列化Begin日志记录到dest中
    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
    }
    // 从src中反序列化出一条Begin日志记录
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);   
    }
};

class CommitLogRecord: public LogRecord {
public:
    CommitLogRecord() {
        log_type_ = LogType::commit;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }

    explicit CommitLogRecord(txn_id_t txn_id) : CommitLogRecord() {
        log_tid_ = txn_id;
    }
};

class AbortLogRecord: public LogRecord {
public:
    AbortLogRecord() {
        log_type_ = LogType::ABORT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }

    explicit AbortLogRecord(txn_id_t txn_id) : AbortLogRecord() {
        log_tid_ = txn_id;
    }
};

class CheckpointLogRecord : public LogRecord {
public:
    CheckpointLogRecord() {
        log_type_ = LogType::CHECKPOINT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE + sizeof(uint32_t);
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }

    explicit CheckpointLogRecord(std::vector<txn_id_t> active_txns)
        : CheckpointLogRecord() {
        active_txns_ = std::move(active_txns);
        log_tot_len_ += static_cast<uint32_t>(active_txns_.size() * sizeof(txn_id_t));
    }

    void serialize(char *dest) const override {
        LogRecord::serialize(dest);
        uint32_t count = static_cast<uint32_t>(active_txns_.size());
        memcpy(dest + OFFSET_LOG_DATA, &count, sizeof(count));
        if (!active_txns_.empty()) {
            memcpy(dest + OFFSET_LOG_DATA + sizeof(count),
                   active_txns_.data(),
                   active_txns_.size() * sizeof(txn_id_t));
        }
    }

    void deserialize(const char *src) override {
        LogRecord::deserialize(src);
        uint32_t count;
        memcpy(&count, src + OFFSET_LOG_DATA, sizeof(count));
        active_txns_.resize(count);
        if (count > 0) {
            memcpy(active_txns_.data(),
                   src + OFFSET_LOG_DATA + sizeof(count),
                   count * sizeof(txn_id_t));
        }
    }

    std::vector<txn_id_t> active_txns_;
};

class InsertLogRecord: public LogRecord {
public:
    InsertLogRecord() {
        log_type_ = LogType::INSERT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }
    InsertLogRecord(txn_id_t txn_id, const RmRecord &insert_value, const Rid &rid,
                    std::string table_name)
        : InsertLogRecord() {
        log_tid_ = txn_id;
        insert_value_ = insert_value;
        rid_ = rid;
        table_name_ = std::move(table_name);
        log_tot_len_ += serialized_record_size(insert_value_);
        log_tot_len_ += sizeof(Rid);
        log_tot_len_ += serialized_string_size(table_name_);
    }

    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
        uint32_t offset = OFFSET_LOG_DATA;
        offset += serialize_record(dest + offset, insert_value_);
        memcpy(dest + offset, &rid_, sizeof(Rid));
        offset += sizeof(Rid);
        offset += serialize_string(dest + offset, table_name_);
    }

    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
        uint32_t offset = OFFSET_LOG_DATA;
        offset += deserialize_record(src + offset, insert_value_);
        memcpy(&rid_, src + offset, sizeof(Rid));
        offset += sizeof(Rid);
        deserialize_string(src + offset, table_name_);
    }

    RmRecord insert_value_;     // 插入的记录
    Rid rid_;                   // 记录插入的位置
    std::string table_name_;    // 插入记录的表名称
};

class DeleteLogRecord: public LogRecord {
public:
    DeleteLogRecord() {
        log_type_ = LogType::DELETE;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }

    DeleteLogRecord(txn_id_t txn_id, const RmRecord &delete_value, const Rid &rid,
                    std::string table_name)
        : DeleteLogRecord() {
        log_tid_ = txn_id;
        delete_value_ = delete_value;
        rid_ = rid;
        table_name_ = std::move(table_name);
        log_tot_len_ += serialized_record_size(delete_value_);
        log_tot_len_ += sizeof(Rid);
        log_tot_len_ += serialized_string_size(table_name_);
    }

    void serialize(char *dest) const override {
        LogRecord::serialize(dest);
        uint32_t offset = OFFSET_LOG_DATA;
        offset += serialize_record(dest + offset, delete_value_);
        memcpy(dest + offset, &rid_, sizeof(Rid));
        offset += sizeof(Rid);
        serialize_string(dest + offset, table_name_);
    }

    void deserialize(const char *src) override {
        LogRecord::deserialize(src);
        uint32_t offset = OFFSET_LOG_DATA;
        offset += deserialize_record(src + offset, delete_value_);
        memcpy(&rid_, src + offset, sizeof(Rid));
        offset += sizeof(Rid);
        deserialize_string(src + offset, table_name_);
    }

    RmRecord delete_value_;
    Rid rid_;
    std::string table_name_;
};

class UpdateLogRecord: public LogRecord {
public:
    UpdateLogRecord() {
        log_type_ = LogType::UPDATE;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }

    UpdateLogRecord(txn_id_t txn_id, const RmRecord &old_value,
                    const RmRecord &new_value, const Rid &rid,
                    std::string table_name)
        : UpdateLogRecord() {
        log_tid_ = txn_id;
        old_value_ = old_value;
        new_value_ = new_value;
        rid_ = rid;
        table_name_ = std::move(table_name);
        log_tot_len_ += serialized_record_size(old_value_);
        log_tot_len_ += serialized_record_size(new_value_);
        log_tot_len_ += sizeof(Rid);
        log_tot_len_ += serialized_string_size(table_name_);
    }

    void serialize(char *dest) const override {
        LogRecord::serialize(dest);
        uint32_t offset = OFFSET_LOG_DATA;
        offset += serialize_record(dest + offset, old_value_);
        offset += serialize_record(dest + offset, new_value_);
        memcpy(dest + offset, &rid_, sizeof(Rid));
        offset += sizeof(Rid);
        serialize_string(dest + offset, table_name_);
    }

    void deserialize(const char *src) override {
        LogRecord::deserialize(src);
        uint32_t offset = OFFSET_LOG_DATA;
        offset += deserialize_record(src + offset, old_value_);
        offset += deserialize_record(src + offset, new_value_);
        memcpy(&rid_, src + offset, sizeof(Rid));
        offset += sizeof(Rid);
        deserialize_string(src + offset, table_name_);
    }

    RmRecord old_value_;
    RmRecord new_value_;
    Rid rid_;
    std::string table_name_;
};

/* 日志缓冲区，只有一个buffer，因此需要阻塞地去把日志写入缓冲区中 */

inline bool validate_serialized_log_record(const char *src, uint32_t available) {
    if (src == nullptr || available < LOG_HEADER_SIZE) {
        return false;
    }

    int raw_type;
    uint32_t total_len;
    memcpy(&raw_type, src + OFFSET_LOG_TYPE, sizeof(raw_type));
    memcpy(&total_len, src + OFFSET_LOG_TOT_LEN, sizeof(total_len));
    if (raw_type < static_cast<int>(LogType::UPDATE) ||
        raw_type > static_cast<int>(LogType::CHECKPOINT) ||
        total_len < LOG_HEADER_SIZE || total_len > available) {
        return false;
    }
    LogType type = static_cast<LogType>(raw_type);

    uint32_t offset = OFFSET_LOG_DATA;
    auto consume = [&](uint32_t size) {
        if (offset > total_len || size > total_len - offset) {
            return false;
        }
        offset += size;
        return true;
    };
    auto consume_record = [&]() {
        if (!consume(sizeof(uint32_t))) {
            return false;
        }
        uint32_t size;
        memcpy(&size, src + offset - sizeof(uint32_t), sizeof(size));
        return consume(size);
    };
    auto consume_string = [&]() {
        if (!consume(sizeof(uint32_t))) {
            return false;
        }
        uint32_t size;
        memcpy(&size, src + offset - sizeof(uint32_t), sizeof(size));
        return consume(size);
    };

    switch (type) {
        case LogType::UPDATE:
            if (!consume_record() || !consume_record() || !consume(sizeof(Rid)) ||
                !consume_string()) {
                return false;
            }
            break;
        case LogType::INSERT:
        case LogType::DELETE:
            if (!consume_record() || !consume(sizeof(Rid)) || !consume_string()) {
                return false;
            }
            break;
        case LogType::CHECKPOINT: {
            if (!consume(sizeof(uint32_t))) {
                return false;
            }
            uint32_t count;
            memcpy(&count, src + OFFSET_LOG_DATA, sizeof(count));
            if (count > (total_len - offset) / sizeof(txn_id_t) ||
                !consume(count * sizeof(txn_id_t))) {
                return false;
            }
            break;
        }
        case LogType::begin:
        case LogType::commit:
        case LogType::ABORT:
            break;
    }
    return offset == total_len;
}

inline std::unique_ptr<LogRecord> deserialize_log_record(const char *src,
                                                         uint32_t available) {
    if (!validate_serialized_log_record(src, available)) {
        return nullptr;
    }

    LogType type;
    memcpy(&type, src + OFFSET_LOG_TYPE, sizeof(type));
    std::unique_ptr<LogRecord> record;
    switch (type) {
        case LogType::UPDATE:
            record = std::make_unique<UpdateLogRecord>();
            break;
        case LogType::INSERT:
            record = std::make_unique<InsertLogRecord>();
            break;
        case LogType::DELETE:
            record = std::make_unique<DeleteLogRecord>();
            break;
        case LogType::begin:
            record = std::make_unique<BeginLogRecord>();
            break;
        case LogType::commit:
            record = std::make_unique<CommitLogRecord>();
            break;
        case LogType::ABORT:
            record = std::make_unique<AbortLogRecord>();
            break;
        case LogType::CHECKPOINT:
            record = std::make_unique<CheckpointLogRecord>();
            break;
    }
    record->deserialize(src);
    return record;
}

class LogBuffer {
public:
    LogBuffer() { 
        offset_ = 0; 
        memset(buffer_, 0, sizeof(buffer_));
    }

    bool is_full(int append_size) {
        if(offset_ + append_size > LOG_BUFFER_SIZE)
            return true;
        return false;
    }

    char buffer_[LOG_BUFFER_SIZE+1];
    int offset_;    // 写入log的offset
};

/* 日志管理器，负责把日志写入日志缓冲区，以及把日志缓冲区中的内容写入磁盘中 */
class LogManager {
public:
    LogManager(DiskManager* disk_manager)
        : written_lsn_(INVALID_LSN),
          durable_lsn_(INVALID_LSN),
          disk_manager_(disk_manager) {}

    lsn_t add_log_to_buffer(LogRecord* log_record);
    std::vector<lsn_t> add_logs_to_buffer(
        const std::vector<LogRecord *> &log_records);
    void flush_log_to_disk(bool force_sync = false);
    void force_flush_up_to(lsn_t target_lsn);
    lsn_t durable_lsn();
    void initialize_from_disk();
    int64_t write_checkpoint_record(const std::vector<txn_id_t> &active_txns);
    void persist_restart_offset(int64_t offset);

    LogBuffer* get_log_buffer() { return &log_buffer_; }

private:
    void flush_log_to_disk_locked(bool force_sync = false);

    std::atomic<lsn_t> global_lsn_{0};  // 全局lsn，递增，用于为每条记录分发lsn
    std::mutex latch_;                  // 用于对log_buffer_的互斥访问
    std::condition_variable durable_cv_;
    bool group_flush_in_progress_{false};
    size_t force_flush_waiters_{0};
    LogBuffer log_buffer_;              // 日志缓冲区
    lsn_t written_lsn_;                 // 已写入 WAL 文件（可能尚在页缓存）的最大 LSN
    lsn_t durable_lsn_;                 // 已被 fsync/fdatasync 覆盖的最大 LSN
    DiskManager* disk_manager_;
};
