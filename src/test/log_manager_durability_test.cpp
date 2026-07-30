#include <filesystem>
#include <fstream>
#include <iostream>
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

    const int log_fd = disk.GetLogFd();
    if (log_fd >= 0) {
        disk.close_file(log_fd);
    }
    std::filesystem::current_path(previous);
    std::filesystem::remove_all(directory);
    std::cout << "log manager durability tests passed\n";
    return 0;
}
