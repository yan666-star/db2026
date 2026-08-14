#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "common/perf_counters.h"
#include "index/ix.h"
#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::vector<char> make_key(int value) {
    std::vector<char> key(64, 0);
    std::snprintf(key.data(), key.size(), "%010d", value);
    return key;
}

void require_same_leaf_batch_reuses_path() {
    const std::string table_name = "ix_batch_path";
    const ColMeta column{table_name, "generic_key", TYPE_STRING, 64, 0,
                         true};
    DiskManager disk_manager;
    BufferPoolManager buffer_pool_manager(32, &disk_manager);
    IxManager ix_manager(&disk_manager, &buffer_pool_manager);
    const std::string index_name =
        ix_manager.get_index_name(table_name, std::vector<ColMeta>{column});
    if (disk_manager.is_file(index_name)) {
        disk_manager.destroy_file(index_name);
    }
    ix_manager.create_index(table_name, std::vector<ColMeta>{column});
    auto index =
        ix_manager.open_index(table_name, std::vector<ColMeta>{column});

    constexpr int kBatchRows = 15;
    std::vector<std::pair<std::vector<char>, Rid>> batch;
    for (int row = 0; row < kBatchRows; ++row) {
        batch.emplace_back(make_key(row), Rid{row + 10, row + 20});
    }
    auto &perf = rmdb_perf::shared_counters();
    const uint64_t fetches_before =
        perf.buffer_fetches.load(std::memory_order_relaxed);
    index->insert_entries_batch(std::move(batch), nullptr);
    const uint64_t fetches_after =
        perf.buffer_fetches.load(std::memory_order_relaxed);
    require(fetches_after - fetches_before <= 6,
            "same-leaf batch repeated the full B+Tree traversal");

    std::vector<std::vector<char>> lookup_keys;
    for (int row = 0; row < kBatchRows; ++row) {
        lookup_keys.push_back(make_key(row));
    }
    const uint64_t lookup_fetches_before =
        perf.buffer_fetches.load(std::memory_order_relaxed);
    require(index->contains_any_entries_batch(lookup_keys),
            "batch unique precheck missed an existing key");
    const uint64_t lookup_fetches_after =
        perf.buffer_fetches.load(std::memory_order_relaxed);
    require(lookup_fetches_after - lookup_fetches_before <= 3,
            "batch unique precheck repeated point traversals");

    std::vector<std::pair<std::vector<char>, Rid>> duplicates;
    duplicates.emplace_back(make_key(0), Rid{999, 999});
    bool duplicate_rejected = false;
    try {
        index->insert_entries_batch(std::move(duplicates), nullptr);
    } catch (const std::exception &) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected,
            "checked B+Tree batch accepted an existing unique key");

    for (int row = 0; row < kBatchRows; ++row) {
        auto key = make_key(row);
        std::vector<Rid> result;
        require(index->get_value(key.data(), &result, nullptr) &&
                    result.size() == 1 &&
                    result[0] == Rid{row + 10, row + 20},
                "same-leaf batch inserted the wrong key/RID");
    }
    ix_manager.close_index(index.get());
    disk_manager.destroy_file(index_name);
}

void require_concurrent_insert_lookup_and_lazy_delete() {
    static_assert(std::is_move_constructible<IxReadNode>::value,
                  "index read nodes must own a move-only page guard");
    static_assert(std::is_move_constructible<IxWriteNode>::value,
                  "index write nodes must own a move-only page guard");

    constexpr int kThreads = 8;
    constexpr int kRowsPerThread = 500;
    const std::string table_name = "ix_concurrency";
    const ColMeta column{table_name, "generic_key", TYPE_STRING, 64, 0,
                         true};

    DiskManager disk_manager;
    BufferPoolManager buffer_pool_manager(128, &disk_manager);
    IxManager ix_manager(&disk_manager, &buffer_pool_manager);
    const std::string index_name =
        ix_manager.get_index_name(table_name, std::vector<ColMeta>{column});
    if (disk_manager.is_file(index_name)) {
        disk_manager.destroy_file(index_name);
    }
    ix_manager.create_index(table_name, std::vector<ColMeta>{column});
    auto index =
        ix_manager.open_index(table_name, std::vector<ColMeta>{column});

    std::atomic<bool> start{false};
    std::atomic<bool> stop_readers{false};
    std::vector<std::thread> workers;
    for (int thread_no = 0; thread_no < kThreads; ++thread_no) {
        workers.emplace_back([&, thread_no] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int row = 0; row < kRowsPerThread; ++row) {
                int value = row * kThreads + thread_no;
                auto key = make_key(value);
                index->insert_entry(key.data(), Rid{value + 10, value + 20},
                                    nullptr);
            }
        });
    }
    std::thread reader([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        int probe = 0;
        while (!stop_readers.load(std::memory_order_acquire)) {
            auto key = make_key(probe % (kThreads * kRowsPerThread));
            std::vector<Rid> result;
            index->get_value(key.data(), &result, nullptr);
            probe++;
        }
    });

    start.store(true, std::memory_order_release);
    for (auto &worker : workers) {
        worker.join();
    }
    stop_readers.store(true, std::memory_order_release);
    reader.join();

    for (int value = 0; value < kThreads * kRowsPerThread; ++value) {
        auto key = make_key(value);
        std::vector<Rid> result;
        require(index->get_value(key.data(), &result, nullptr),
                "key disappeared after concurrent split");
        require(result.size() == 1 && result[0] == Rid{value + 10, value + 20},
                "index returned the wrong RID");
    }

    std::vector<std::thread> deleters;
    for (int thread_no = 0; thread_no < kThreads; ++thread_no) {
        deleters.emplace_back([&, thread_no] {
            for (int value = thread_no * 2;
                 value < kThreads * kRowsPerThread;
                 value += kThreads * 2) {
                auto key = make_key(value);
                require(index->delete_entry(key.data(), nullptr),
                        "existing key could not be deleted");
            }
        });
    }
    for (auto &deleter : deleters) {
        deleter.join();
    }

    for (int value = 0; value < kThreads * kRowsPerThread; ++value) {
        auto key = make_key(value);
        std::vector<Rid> result;
        bool found = index->get_value(key.data(), &result, nullptr);
        require(found == (value % 2 == 1),
                "lazy delete changed an unrelated key");
    }

    int expected = 1;
    Iid begin = index->leaf_begin();
    Iid end = index->leaf_end();
    for (IxScan scan(index.get(), begin, end, &buffer_pool_manager);
         !scan.is_end(); scan.next()) {
        Rid rid = scan.rid();
        require(rid.page_no == expected + 10,
                "leaf chain range scan is missing, duplicated, or unordered");
        expected += 2;
    }
    require(expected == kThreads * kRowsPerThread + 1,
            "leaf chain range scan returned the wrong row count");

    ix_manager.close_index(index.get());
    disk_manager.destroy_file(index_name);
}

}  // namespace

int main() {
    setenv("RMDB_PERF_DIAG", "1", 1);
    require_same_leaf_batch_reuses_path();
    require_concurrent_insert_lookup_and_lazy_delete();
    std::cout << "index concurrency tests passed\n";
    return 0;
}
