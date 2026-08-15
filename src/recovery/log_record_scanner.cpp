#include "log_record_scanner.h"

#include <algorithm>
#include <cstring>

#include "errors.h"

LogRecordScanner::LogRecordScanner(DiskManager *disk_manager,
                                   int64_t scan_start, int64_t log_end,
                                   size_t buffer_size)
    : disk_manager_(disk_manager),
      log_end_(log_end),
      position_(scan_start),
      valid_end_(scan_start),
      buffer_offset_(scan_start),
      buffer_(std::max(buffer_size,
                       static_cast<size_t>(LOG_HEADER_SIZE))) {
    if (disk_manager_ == nullptr || scan_start < 0 || log_end < scan_start) {
        throw InternalError("Invalid WAL scanner bounds");
    }
}

bool LogRecordScanner::ensure_available(size_t bytes) {
    while (buffer_end_ - buffer_begin_ < bytes) {
        if (buffer_begin_ != 0) {
            const size_t remaining = buffer_end_ - buffer_begin_;
            if (remaining != 0) {
                std::memmove(buffer_.data(),
                             buffer_.data() + buffer_begin_, remaining);
            }
            buffer_offset_ += static_cast<int64_t>(buffer_begin_);
            buffer_begin_ = 0;
            buffer_end_ = remaining;
        }

        if (buffer_.size() < bytes) {
            buffer_.resize(std::max(bytes, buffer_.size() * 2));
        }

        const int64_t read_offset =
            buffer_offset_ + static_cast<int64_t>(buffer_end_);
        if (read_offset >= log_end_) {
            return false;
        }
        const int64_t remaining_file = log_end_ - read_offset;
        const size_t free_space = buffer_.size() - buffer_end_;
        const int request = static_cast<int>(std::min<int64_t>(
            static_cast<int64_t>(free_space), remaining_file));
        const int read_bytes = disk_manager_->read_log(
            buffer_.data() + buffer_end_, request, read_offset);
        if (read_bytes <= 0) {
            return false;
        }
        buffer_end_ += static_cast<size_t>(read_bytes);
    }
    return true;
}

std::optional<ScannedLogRecord> LogRecordScanner::next() {
    if (stopped_ || position_ >= log_end_) {
        return std::nullopt;
    }
    if (!ensure_available(LOG_HEADER_SIZE)) {
        stopped_ = true;
        return std::nullopt;
    }

    uint32_t total_len = 0;
    std::memcpy(&total_len,
                buffer_.data() + buffer_begin_ + OFFSET_LOG_TOT_LEN,
                sizeof(total_len));
    if (total_len < LOG_HEADER_SIZE ||
        static_cast<int64_t>(total_len) > log_end_ - position_ ||
        !ensure_available(total_len)) {
        stopped_ = true;
        return std::nullopt;
    }

    auto record = deserialize_log_record(
        buffer_.data() + buffer_begin_, total_len);
    if (record == nullptr) {
        stopped_ = true;
        return std::nullopt;
    }

    const int64_t offset = position_;
    buffer_begin_ += total_len;
    position_ += total_len;
    valid_end_ = position_;
    return ScannedLogRecord{offset, position_, std::move(record)};
}
