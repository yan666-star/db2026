#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <unistd.h>

#include "recovery/log_manager.h"
#include "storage/buffer_pool_manager.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class OrderedDiskManager : public DiskManager {
   public:
    void sync_log() override {
        std::lock_guard<std::mutex> lock(latch_);
        events_.push_back("wal_sync");
        DiskManager::sync_log();
    }

    void write_page(int fd, page_id_t page_no, const char *data,
                    int size) override {
        {
            std::lock_guard<std::mutex> lock(latch_);
            if (track_page_writes_) {
                events_.push_back("page_write");
            }
        }
        DiskManager::write_page(fd, page_no, data, size);
    }

    void begin_tracking() {
        std::lock_guard<std::mutex> lock(latch_);
        events_.clear();
        track_page_writes_ = true;
    }

    std::vector<std::string> events() const {
        std::lock_guard<std::mutex> lock(latch_);
        return events_;
    }

   private:
    mutable std::mutex latch_;
    bool track_page_writes_ = false;
    std::vector<std::string> events_;
};

void require_batch_append_and_wal_before_page() {
    char directory_template[] = "/tmp/rmdb-page-lsn-test-XXXXXX";
    char *directory = mkdtemp(directory_template);
    require(directory != nullptr, "mkdtemp failed");
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(directory);
    std::ofstream(LOG_FILE_NAME, std::ios::binary).close();

    OrderedDiskManager disk;
    disk.create_file("data.db");
    int fd = disk.open_file("data.db");
    LogManager log(&disk);
    BufferPoolManager buffer_pool(4, &disk);
    buffer_pool.set_log_manager(&log);

    BeginLogRecord begin(7);
    InsertLogRecord insert;
    insert.log_tid_ = 7;
    CommitLogRecord commit(7);
    std::vector<LogRecord *> records{&begin, &insert, &commit};
    std::vector<lsn_t> lsns = log.add_logs_to_buffer(records);
    require(lsns.size() == records.size(),
            "batch WAL append did not return every LSN");
    require(lsns[1] == lsns[0] + 1 && lsns[2] == lsns[1] + 1,
            "batch WAL append did not allocate consecutive LSNs");

    PageId page_id{fd, INVALID_PAGE_ID};
    {
        WritePageGuard page = buffer_pool.new_page_guarded(&page_id);
        require(page.is_valid(), "failed to allocate test page");
        page.data()[64] = 42;
        page.set_page_lsn(lsns.back(), true);
        page.mark_dirty();
    }

    disk.begin_tracking();
    require(buffer_pool.flush_page(page_id), "dirty page flush failed");
    std::vector<std::string> events = disk.events();
    require(events.size() >= 2 && events[0] == "wal_sync" &&
                events[1] == "page_write",
            "data page reached disk before its WAL LSN was durable");
    require(log.durable_lsn() >= lsns.back(),
            "page flush did not advance durable_lsn to PageLSN");

    disk.close_file(fd);
    const int log_fd = disk.GetLogFd();
    if (log_fd >= 0) {
        disk.close_file(log_fd);
    }
    std::filesystem::current_path(previous);
    std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
    require_batch_append_and_wal_before_page();
    std::cout << "WAL PageLSN tests passed\n";
    return 0;
}
