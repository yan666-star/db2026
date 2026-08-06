#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#include <unistd.h>

#include "recovery/log_manager.h"
#include "storage/disk_manager.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void close_wal(DiskManager *disk) {
    const int log_fd = disk->GetLogFd();
    if (log_fd >= 0) {
        disk->close_file(log_fd);
    }
}

void append_record(DiskManager *disk, const LogRecord &record) {
    std::vector<char> bytes(record.log_tot_len_);
    record.serialize(bytes.data());
    disk->write_log(bytes.data(), static_cast<int>(bytes.size()));
}

}  // namespace

int main() {
    char directory_template[] = "/tmp/rmdb-wal-test-XXXXXX";
    char *directory = mkdtemp(directory_template);
    require(directory != nullptr, "mkdtemp failed");

    const std::filesystem::path previous =
        std::filesystem::current_path();
    std::filesystem::current_path(directory);
    std::ofstream(LOG_FILE_NAME, std::ios::binary).close();

    DiskManager disk;
    LogManager log(&disk);
    constexpr int kTransactions = 16;
    std::vector<std::thread> workers;
    workers.reserve(kTransactions);
    for (int index = 0; index < kTransactions; ++index) {
        workers.emplace_back([&, index] {
            CommitLogRecord commit(index + 1);
            const lsn_t lsn = log.add_log_to_buffer(&commit);
            log.force_flush_up_to(lsn);
            require(log.durable_lsn() >= lsn,
                    "COMMIT returned before its LSN was durable");
        });
    }
    for (auto &worker : workers) {
        worker.join();
    }

    require(std::filesystem::file_size(LOG_FILE_NAME) >=
                static_cast<uintmax_t>(kTransactions * LOG_HEADER_SIZE),
            "WAL does not contain every commit record");

    close_wal(&disk);

    // A 50-warehouse LOAD produces a WAL larger than INT32_MAX.  The
    // restart file already stores an int64_t offset, so the reader must not
    // narrow that position while validating the checkpoint and its tail.
    std::ofstream(LOG_FILE_NAME,
                  std::ios::binary | std::ios::trunc).close();
    constexpr int64_t kLargeCheckpointOffset =
        static_cast<int64_t>(std::numeric_limits<int32_t>::max()) + 4096;
    std::filesystem::resize_file(LOG_FILE_NAME, kLargeCheckpointOffset);

    DiskManager large_disk;
    CheckpointLogRecord checkpoint(std::vector<txn_id_t>{});
    checkpoint.lsn_ = 41;
    append_record(&large_disk, checkpoint);

    CommitLogRecord tail_commit(7);
    tail_commit.lsn_ = 42;
    tail_commit.prev_lsn_ = checkpoint.lsn_;
    append_record(&large_disk, tail_commit);
    large_disk.sync_log();
    large_disk.write_restart_offset(kLargeCheckpointOffset);

    const int64_t expected_size =
        kLargeCheckpointOffset + checkpoint.log_tot_len_ +
        tail_commit.log_tot_len_;
    LogManager recovered_log(&large_disk);
    recovered_log.initialize_from_disk();
    require(recovered_log.durable_lsn() == tail_commit.lsn_,
            "WAL tail beyond INT32_MAX was not recovered");
    require(large_disk.get_file_size(LOG_FILE_NAME) == expected_size,
            "large WAL was truncated at a narrowed checkpoint offset");

    close_wal(&large_disk);
    std::filesystem::current_path(previous);
    std::filesystem::remove_all(directory);
    std::cout << "log manager durability tests passed\n";
    return 0;
}
