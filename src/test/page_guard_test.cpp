#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <future>
#include <iostream>
#include <string>
#include <utility>

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

class TempPageFile {
public:
    explicit TempPageFile(int page_count) {
        char directory_template[] = "/tmp/rmdb-page-guard-test-XXXXXX";
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
            disk_.write_page(fd_, page_no, bytes.data(), PAGE_SIZE);
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

    DiskManager *disk() { return &disk_; }
    int fd() const { return fd_; }

private:
    std::filesystem::path previous_;
    std::filesystem::path directory_;
    DiskManager disk_;
    int fd_ = -1;
};

void test_guard_drop_restores_victim_eligibility() {
    TempPageFile file(2);
    BufferPoolManager bpm(1, file.disk());

    auto first = bpm.fetch_page_read(PageId{file.fd(), 0});
    require(first.get_page() != nullptr, "first guarded fetch failed");
    auto blocked = bpm.fetch_page_read(PageId{file.fd(), 1});
    require(blocked.get_page() == nullptr,
            "a pinned frame was selected as victim");

    first.drop();
    auto second = bpm.fetch_page_read(PageId{file.fd(), 1});
    require(second.get_page() != nullptr,
            "dropping a guard did not restore victim eligibility");
}

void test_move_does_not_double_unpin() {
    TempPageFile file(2);
    BufferPoolManager bpm(1, file.disk());

    auto original = bpm.fetch_page_read(PageId{file.fd(), 0});
    auto owner = std::move(original);
    original.drop();
    auto blocked = bpm.fetch_page_read(PageId{file.fd(), 1});
    require(blocked.get_page() == nullptr,
            "moved-from guard released the live pin");

    owner.drop();
    auto reused = bpm.fetch_page_write(PageId{file.fd(), 1});
    require(reused.get_page() != nullptr,
            "moved-to guard did not release its pin");
    reused.data()[0] = 'Z';
    reused.mark_dirty();
    original.drop();
    reused.drop();
    require(bpm.flush_page(PageId{file.fd(), 1}),
            "reused frame could not be flushed");

    std::array<char, PAGE_SIZE> bytes{};
    file.disk()->read_page(file.fd(), 1, bytes.data(), PAGE_SIZE);
    require(bytes[0] == 'Z',
            "moved-from guard polluted a reused frame");
}

void test_read_guards_are_shared() {
    TempPageFile file(1);
    BufferPoolManager bpm(2, file.disk());
    auto first = bpm.fetch_page_read(PageId{file.fd(), 0});
    require(first.get_page() != nullptr, "first read guard failed");

    auto second = std::async(std::launch::async, [&] {
        auto guard = bpm.fetch_page_read(PageId{file.fd(), 0});
        return guard.get_page() != nullptr && guard.data()[0] == 'A';
    });
    require(second.wait_for(500ms) == std::future_status::ready,
            "a read guard blocked another read guard");
    require(second.get(), "second read guard observed wrong data");
}

void test_write_guard_blocks_read_and_write() {
    TempPageFile file(1);
    BufferPoolManager bpm(2, file.disk());
    auto writer = bpm.fetch_page_write(PageId{file.fd(), 0});
    require(writer.get_page() != nullptr, "write guard fetch failed");

    std::promise<void> read_started;
    auto reader = std::async(std::launch::async, [&] {
        read_started.set_value();
        auto guard = bpm.fetch_page_read(PageId{file.fd(), 0});
        return guard.get_page() != nullptr;
    });
    read_started.get_future().wait();
    require(reader.wait_for(50ms) == std::future_status::timeout,
            "write guard did not block a reader");

    std::promise<void> write_started;
    auto second_writer = std::async(std::launch::async, [&] {
        write_started.set_value();
        auto guard = bpm.fetch_page_write(PageId{file.fd(), 0});
        return guard.get_page() != nullptr;
    });
    write_started.get_future().wait();
    require(second_writer.wait_for(50ms) == std::future_status::timeout,
            "write guard did not block another writer");

    writer.drop();
    require(reader.wait_for(1s) == std::future_status::ready && reader.get(),
            "reader did not resume after write guard drop");
    require(second_writer.wait_for(1s) == std::future_status::ready &&
                second_writer.get(),
            "writer did not resume after write guard drop");
}

void test_dirty_mark_reaches_disk() {
    TempPageFile file(1);
    BufferPoolManager bpm(1, file.disk());
    {
        auto guard = bpm.fetch_page_write(PageId{file.fd(), 0});
        require(guard.get_page() != nullptr, "write guard fetch failed");
        guard.data()[0] = 'D';
        guard.mark_dirty();
    }
    require(bpm.flush_page(PageId{file.fd(), 0}),
            "dirty guarded page could not be flushed");

    std::array<char, PAGE_SIZE> bytes{};
    file.disk()->read_page(file.fd(), 0, bytes.data(), PAGE_SIZE);
    require(bytes[0] == 'D', "guard dirty mark was not persisted");
}

}  // namespace

int main() {
    test_guard_drop_restores_victim_eligibility();
    test_move_does_not_double_unpin();
    test_read_guards_are_shared();
    test_write_guard_blocks_read_and_write();
    test_dirty_mark_reaches_disk();
    std::cout << "page guard tests passed\n";
    return 0;
}
