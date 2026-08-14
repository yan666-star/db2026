#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include <unistd.h>

#include "recovery/log_manager.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class CountingSyncDiskManager : public DiskManager {
   public:
    void sync_log() override {
        sync_calls.fetch_add(1, std::memory_order_relaxed);
        DiskManager::sync_log();
    }

    std::atomic<int> sync_calls{0};
};

void test_concurrent_commits_share_fsync() {
    char directory_template[] = "/tmp/rmdb-group-commit-XXXXXX";
    char *directory = mkdtemp(directory_template);
    require(directory != nullptr, "mkdtemp failed");
    const std::filesystem::path previous =
        std::filesystem::current_path();
    std::filesystem::current_path(directory);
    std::ofstream(LOG_FILE_NAME, std::ios::binary).close();

    CountingSyncDiskManager disk;
    LogManager log(&disk);
    constexpr int kTransactions = 16;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::vector<lsn_t> commit_lsns(kTransactions, INVALID_LSN);
    std::vector<std::thread> workers;
    workers.reserve(kTransactions);
    for (int index = 0; index < kTransactions; ++index) {
        workers.emplace_back([&, index] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            CommitLogRecord commit(index + 1);
            const lsn_t lsn = log.add_log_to_buffer(&commit);
            commit_lsns[index] = lsn;
            log.force_flush_up_to(lsn);
        });
    }
    while (ready.load(std::memory_order_acquire) != kTransactions) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    for (auto &worker : workers) {
        worker.join();
    }

    for (lsn_t lsn : commit_lsns) {
        require(lsn != INVALID_LSN && log.durable_lsn() >= lsn,
                "group commit returned before an own commit LSN was durable");
    }
    require(disk.sync_calls.load(std::memory_order_relaxed) < kTransactions,
            "concurrent commits performed one fsync per transaction");

    const int log_fd = disk.GetLogFd();
    if (log_fd >= 0) {
        disk.close_file(log_fd);
    }
    std::filesystem::current_path(previous);
    std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
    test_concurrent_commits_share_fsync();
    std::cout << "group commit tests passed\n";
    return 0;
}
