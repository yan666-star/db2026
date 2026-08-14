#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "record/rm.h"
#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void require_parallel_inserts_have_unique_slots() {
    constexpr int kThreads = 12;
    constexpr int kRowsPerThread = 200;
    const std::string file_name = "heap_page_concurrency_test.db";

    DiskManager disk_manager;
    BufferPoolManager buffer_pool_manager(64, &disk_manager);
    RmManager rm_manager(&disk_manager, &buffer_pool_manager);
    if (disk_manager.is_file(file_name)) {
        disk_manager.destroy_file(file_name);
    }
    rm_manager.create_file(file_name, sizeof(int));
    auto file = rm_manager.open_file(file_name);

    std::mutex result_latch;
    std::vector<std::pair<Rid, int>> inserted;
    inserted.reserve(kThreads * kRowsPerThread);
    std::vector<std::thread> workers;
    for (int thread_no = 0; thread_no < kThreads; ++thread_no) {
        workers.emplace_back([&, thread_no] {
            std::vector<std::pair<Rid, int>> local;
            local.reserve(kRowsPerThread);
            for (int row = 0; row < kRowsPerThread; ++row) {
                int value = thread_no * kRowsPerThread + row;
                Rid rid = file->insert_record(
                    reinterpret_cast<char *>(&value), nullptr);
                local.emplace_back(rid, value);
            }
            std::lock_guard<std::mutex> lock(result_latch);
            inserted.insert(inserted.end(), local.begin(), local.end());
        });
    }
    for (auto &worker : workers) {
        worker.join();
    }

    std::set<std::pair<int, int>> unique_rids;
    for (const auto &[rid, expected] : inserted) {
        require(unique_rids.emplace(rid.page_no, rid.slot_no).second,
                "concurrent inserts allocated the same slot twice");
        auto record = file->get_record(rid, nullptr);
        int actual = 0;
        std::memcpy(&actual, record->data, sizeof(actual));
        require(actual == expected,
                "concurrent insert contents were overwritten");
    }
    require(inserted.size() == kThreads * kRowsPerThread,
            "parallel insert lost rows");

    rm_manager.close_file(file.get());
    rm_manager.destroy_file(file_name);
}

void require_batch_insert_preserves_order_and_contents() {
    const std::string file_name = "heap_batch_insert_test.db";
    DiskManager disk_manager;
    BufferPoolManager buffer_pool_manager(16, &disk_manager);
    RmManager rm_manager(&disk_manager, &buffer_pool_manager);
    if (disk_manager.is_file(file_name)) {
        disk_manager.destroy_file(file_name);
    }
    rm_manager.create_file(file_name, sizeof(int));
    auto file = rm_manager.open_file(file_name);

    std::vector<int> values(300);
    std::vector<PendingInsert> pending;
    pending.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<int>(1000 + i);
        pending.push_back(PendingInsert{
            reinterpret_cast<const char *>(&values[i]), sizeof(int)});
    }

    std::vector<Rid> rids = file->insert_records(pending, nullptr, "");
    require(rids.size() == values.size(),
            "batch insert must return one RID per input row");
    for (size_t i = 0; i < rids.size(); ++i) {
        auto record = file->get_record(rids[i], nullptr);
        int actual = 0;
        std::memcpy(&actual, record->data, sizeof(actual));
        require(actual == values[i],
                "batch insert changed input order or row contents");
    }

    rm_manager.close_file(file.get());
    rm_manager.destroy_file(file_name);
}

}  // namespace

int main() {
    require_parallel_inserts_have_unique_slots();
    require_batch_insert_preserves_order_and_contents();
    std::cout << "heap page concurrency tests passed\n";
    return 0;
}
