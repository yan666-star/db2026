#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "storage/buffer_pool_manager.h"

namespace {

using namespace std::chrono_literals;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class ControlledDiskManager : public DiskManager {
   public:
    void read_page(int fd, page_id_t page_no, char *offset,
                   int num_bytes) override {
        read_count_.fetch_add(1, std::memory_order_relaxed);
        if (page_no == delayed_read_page_.load(std::memory_order_relaxed)) {
            std::unique_lock<std::mutex> lock(control_latch_);
            read_started_ = true;
            control_cv_.notify_all();
            control_cv_.wait(lock, [&] { return allow_read_; });
        }
        DiskManager::read_page(fd, page_no, offset, num_bytes);
    }

    void write_page(int fd, page_id_t page_no, const char *offset,
                    int num_bytes) override {
        write_count_.fetch_add(1, std::memory_order_relaxed);
        if (page_no == delayed_write_page_.load(std::memory_order_relaxed)) {
            std::unique_lock<std::mutex> lock(control_latch_);
            write_started_ = true;
            control_cv_.notify_all();
            control_cv_.wait(lock, [&] { return allow_write_; });
        }
        DiskManager::write_page(fd, page_no, offset, num_bytes);
    }

    void delay_read(page_id_t page_no) {
        std::lock_guard<std::mutex> lock(control_latch_);
        delayed_read_page_.store(page_no, std::memory_order_relaxed);
        read_started_ = false;
        allow_read_ = false;
    }

    void wait_for_read_start() {
        std::unique_lock<std::mutex> lock(control_latch_);
        require(control_cv_.wait_for(lock, 2s, [&] { return read_started_; }),
                "timed out waiting for delayed read");
    }

    void release_read() {
        std::lock_guard<std::mutex> lock(control_latch_);
        allow_read_ = true;
        delayed_read_page_.store(INVALID_PAGE_ID, std::memory_order_relaxed);
        control_cv_.notify_all();
    }

    void delay_write(page_id_t page_no) {
        std::lock_guard<std::mutex> lock(control_latch_);
        delayed_write_page_.store(page_no, std::memory_order_relaxed);
        write_started_ = false;
        allow_write_ = false;
    }

    void wait_for_write_start() {
        std::unique_lock<std::mutex> lock(control_latch_);
        require(control_cv_.wait_for(lock, 2s, [&] { return write_started_; }),
                "timed out waiting for delayed write");
    }

    void release_write() {
        std::lock_guard<std::mutex> lock(control_latch_);
        allow_write_ = true;
        delayed_write_page_.store(INVALID_PAGE_ID,
                                  std::memory_order_relaxed);
        control_cv_.notify_all();
    }

    int read_count() const {
        return read_count_.load(std::memory_order_relaxed);
    }

   private:
    std::atomic<int> read_count_{0};
    std::atomic<int> write_count_{0};
    std::atomic<page_id_t> delayed_read_page_{INVALID_PAGE_ID};
    std::atomic<page_id_t> delayed_write_page_{INVALID_PAGE_ID};
    std::mutex control_latch_;
    std::condition_variable control_cv_;
    bool read_started_ = false;
    bool write_started_ = false;
    bool allow_read_ = true;
    bool allow_write_ = true;
};

class TempPageFile {
   public:
    explicit TempPageFile(int page_count) {
        char directory_template[] = "/tmp/rmdb-buffer-concurrency-XXXXXX";
        char *directory = mkdtemp(directory_template);
        require(directory != nullptr, "mkdtemp failed");
        directory_ = directory;
        previous_ = std::filesystem::current_path();
        std::filesystem::current_path(directory_);

        disk_.create_file("pages.db");
        fd_ = disk_.open_file("pages.db");
        for (int page_no = 0; page_no < page_count; ++page_no) {
            std::array<char, PAGE_SIZE> bytes{};
            bytes[0] = static_cast<char>('A' + page_no);
            disk_.DiskManager::write_page(fd_, page_no, bytes.data(),
                                          PAGE_SIZE);
        }
        disk_.set_fd2pageno(fd_, page_count);
    }

    ~TempPageFile() {
        if (fd_ >= 0) {
            disk_.close_file(fd_);
        }
        std::filesystem::current_path(previous_);
        std::filesystem::remove_all(directory_);
    }

    ControlledDiskManager *disk() { return &disk_; }
    int fd() const { return fd_; }

   private:
    std::filesystem::path previous_;
    std::filesystem::path directory_;
    ControlledDiskManager disk_;
    int fd_ = -1;
};

void test_same_page_concurrent_miss_reads_once() {
    TempPageFile file(1);
    BufferPoolManager bpm(8, file.disk());
    file.disk()->delay_read(0);

    constexpr int kThreads = 12;
    std::atomic<int> ready{0};
    std::promise<void> start;
    std::shared_future<void> start_signal(start.get_future());
    std::vector<std::future<bool>> readers;
    readers.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        readers.emplace_back(std::async(std::launch::async, [&] {
            ready.fetch_add(1, std::memory_order_relaxed);
            start_signal.wait();
            auto guard = bpm.fetch_page_read(PageId{file.fd(), 0});
            return guard.get_page() != nullptr && guard.data()[0] == 'A';
        }));
    }
    while (ready.load(std::memory_order_relaxed) != kThreads) {
        std::this_thread::yield();
    }
    start.set_value();
    file.disk()->wait_for_read_start();
    std::this_thread::sleep_for(50ms);
    file.disk()->release_read();

    for (auto &reader : readers) {
        require(reader.wait_for(2s) == std::future_status::ready &&
                    reader.get(),
                "concurrent reader failed");
    }
    require(file.disk()->read_count() == 1,
            "same-page concurrent miss performed duplicate disk reads");
}

void test_slow_miss_does_not_block_other_shard_hit() {
    TempPageFile file(2);
    BufferPoolManager bpm(8, file.disk());
    {
        auto cached = bpm.fetch_page_read(PageId{file.fd(), 0});
        require(cached.get_page() != nullptr, "preload failed");
    }

    file.disk()->delay_read(1);
    auto slow_miss = std::async(std::launch::async, [&] {
        auto guard = bpm.fetch_page_read(PageId{file.fd(), 1});
        return guard.get_page() != nullptr && guard.data()[0] == 'B';
    });
    file.disk()->wait_for_read_start();

    auto cached_hit = std::async(std::launch::async, [&] {
        auto guard = bpm.fetch_page_read(PageId{file.fd(), 0});
        return guard.get_page() != nullptr && guard.data()[0] == 'A';
    });
    require(cached_hit.wait_for(100ms) == std::future_status::ready,
            "disk read in one shard blocked a cached hit in another shard");
    require(cached_hit.get(), "cached hit returned wrong data");

    file.disk()->release_read();
    require(slow_miss.wait_for(2s) == std::future_status::ready &&
                slow_miss.get(),
            "delayed miss did not complete");
}

void test_flush_does_not_lose_concurrent_dirty() {
    TempPageFile file(1);
    BufferPoolManager bpm(2, file.disk());
    {
        auto writer = bpm.fetch_page_write(PageId{file.fd(), 0});
        require(writer.get_page() != nullptr, "initial write fetch failed");
        writer.data()[0] = 'X';
        writer.mark_dirty();
    }

    file.disk()->delay_write(0);
    auto flush = std::async(std::launch::async, [&] {
        return bpm.flush_page(PageId{file.fd(), 0});
    });
    file.disk()->wait_for_write_start();

    {
        auto writer = bpm.fetch_page_write(PageId{file.fd(), 0});
        require(writer.get_page() != nullptr,
                "concurrent write fetch failed");
        writer.data()[0] = 'Y';
        writer.mark_dirty();
    }
    file.disk()->release_write();
    require(flush.wait_for(2s) == std::future_status::ready && flush.get(),
            "delayed flush did not complete");

    require(bpm.flush_page(PageId{file.fd(), 0}),
            "second flush failed");
    std::array<char, PAGE_SIZE> bytes{};
    file.disk()->DiskManager::read_page(file.fd(), 0, bytes.data(), PAGE_SIZE);
    require(bytes[0] == 'Y', "flush cleared a concurrent dirty update");
}

}  // namespace

int main() {
    test_same_page_concurrent_miss_reads_once();
    test_slow_miss_does_not_block_other_shard_hit();
    test_flush_does_not_lose_concurrent_dirty();
    std::cout << "buffer pool concurrency tests passed\n";
    return 0;
}
