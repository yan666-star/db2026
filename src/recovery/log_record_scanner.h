#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "log_manager.h"
#include "storage/disk_manager.h"

struct ScannedLogRecord {
    int64_t offset;
    int64_t next_offset;
    std::unique_ptr<LogRecord> record;
};

class LogRecordScanner {
   public:
    static constexpr size_t kDefaultBufferSize = 1U << 20;

    LogRecordScanner(DiskManager *disk_manager, int64_t scan_start,
                     int64_t log_end,
                     size_t buffer_size = kDefaultBufferSize);

    std::optional<ScannedLogRecord> next();

    int64_t position() const noexcept { return position_; }
    int64_t valid_end() const noexcept { return valid_end_; }

   private:
    bool ensure_available(size_t bytes);

    DiskManager *disk_manager_;
    int64_t log_end_;
    int64_t position_;
    int64_t valid_end_;
    int64_t buffer_offset_;
    size_t buffer_begin_ = 0;
    size_t buffer_end_ = 0;
    bool stopped_ = false;
    std::vector<char> buffer_;
};
