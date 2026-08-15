#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
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
    auto log = std::make_unique<LogManager>(&disk);
    constexpr int kTransactions = 16;
    std::vector<std::thread> workers;
    workers.reserve(kTransactions);
    for (int index = 0; index < kTransactions; ++index) {
        workers.emplace_back([&, index] {
            CommitLogRecord commit(index + 1);
            const lsn_t lsn = log->add_log_to_buffer(&commit);
            log->force_flush_up_to(lsn);
            require(log->durable_lsn() >= lsn,
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
    auto recovered_log = std::make_unique<LogManager>(&large_disk);
    recovered_log->initialize_from_disk();
    require(recovered_log->durable_lsn() == tail_commit.lsn_,
            "WAL tail beyond INT32_MAX was not recovered");
    require(large_disk.get_file_size(LOG_FILE_NAME) == expected_size,
            "large WAL was truncated at a narrowed checkpoint offset");

    close_wal(&large_disk);

    // RecoveryManager already validates the WAL and discovers both its last
    // complete byte and maximum LSN.  Adopting that scan must initialize the
    // durability clocks and discard only an incomplete tail without scanning
    // every record again.
    std::ofstream(LOG_FILE_NAME,
                  std::ios::binary | std::ios::trunc).close();
    DiskManager scanned_disk;
    BeginLogRecord scanned_begin(23);
    scanned_begin.lsn_ = 73;
    append_record(&scanned_disk, scanned_begin);
    CommitLogRecord scanned_commit(23);
    scanned_commit.lsn_ = 74;
    scanned_commit.prev_lsn_ = scanned_begin.lsn_;
    append_record(&scanned_disk, scanned_commit);
    const int64_t scanned_valid_end =
        scanned_disk.get_file_size(LOG_FILE_NAME);
    char incomplete_tail[LOG_HEADER_SIZE - 1]{};
    scanned_disk.write_log(incomplete_tail, sizeof(incomplete_tail));

    LogManager scanned_log(&scanned_disk);
    scanned_log.initialize_from_recovery_scan(
        scanned_valid_end, scanned_commit.lsn_);
    require(scanned_log.durable_lsn() == scanned_commit.lsn_,
            "recovery scan result did not initialize the durable LSN");
    require(scanned_disk.get_file_size(LOG_FILE_NAME) == scanned_valid_end,
            "recovery scan result did not truncate the incomplete WAL tail");

    close_wal(&scanned_disk);
    std::filesystem::current_path(previous);
    std::filesystem::remove_all(directory);
    std::cout << "log manager durability tests passed\n";
    return 0;
}
