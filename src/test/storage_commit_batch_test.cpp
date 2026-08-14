#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <vector>

#include <unistd.h>

#include "common/perf_counters.h"
#include "execution/executor_insert.h"
#include "record/rm_manager.h"
#include "system/sm.h"
#include "transaction/transaction_manager.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void require_equal(uint64_t actual, uint64_t expected,
                   const char *message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << " (expected " << expected
                  << ", got " << actual << ")\n";
        std::exit(1);
    }
}

std::vector<Value> row_values(int id) {
    Value id_value;
    id_value.set_int(id);
    Value payload;
    payload.set_str(std::string(300, static_cast<char>('a' + id % 26)));
    return {id_value, payload};
}

std::vector<char> raw_row(int id) {
    std::vector<char> row(sizeof(int) + 300, 0);
    std::memcpy(row.data(), &id, sizeof(id));
    return row;
}

void require_one_commit_batches_each_storage_unit() {
    char directory_template[] = "/tmp/rmdb-storage-commit-batch-XXXXXX";
    char *directory = mkdtemp(directory_template);
    require(directory != nullptr, "mkdtemp failed");
    const std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::current_path(directory);

    DiskManager disk;
    BufferPoolManager buffer_pool(64, &disk);
    RmManager record_manager(&disk, &buffer_pool);
    IxManager index_manager(&disk, &buffer_pool);
    SmManager system_manager(&disk, &buffer_pool, &record_manager,
                             &index_manager);
    LockManager lock_manager;
    TransactionManager transaction_manager(&lock_manager, &system_manager);
    LogManager log_manager(&disk);

    system_manager.create_db("commit_batch_db");
    system_manager.open_db("commit_batch_db");
    system_manager.create_table(
        "rows", {ColDef{"id", TYPE_INT, static_cast<int>(sizeof(int))},
                 ColDef{"payload", TYPE_STRING, 300}}, nullptr);
    system_manager.create_index("rows", {"id"}, nullptr);
    RmFileHandle *file = system_manager.fhs_.at("rows").get();

    std::vector<Rid> seeded;
    std::set<page_id_t> seeded_pages;
    for (int row = 0; row < 15; ++row) {
        std::vector<char> record = raw_row(row);
        Rid rid = file->insert_record(record.data(), nullptr);
        seeded.push_back(rid);
        seeded_pages.insert(rid.page_no);
    }
    require(seeded_pages.size() == 2,
            "15-row fixture must occupy exactly two Heap pages");
    for (const Rid &rid : seeded) {
        file->delete_record(rid, nullptr);
    }

    Transaction *txn = transaction_manager.begin(
        nullptr, &log_manager, IsolationLevel::SNAPSHOT_ISOLATION);
    Context context(&lock_manager, &log_manager, txn, &transaction_manager);
    for (int row = 0; row < 15; ++row) {
        InsertExecutor insert(&system_manager, "rows", row_values(row),
                              &context);
        insert.Next();
    }

    auto &counters = rmdb_perf::shared_counters();
    counters.heap_page_write_guards.store(0, std::memory_order_relaxed);
    counters.ix_leaf_write_guards.store(0, std::memory_order_relaxed);
    counters.wal_row_append_latch_acquires.store(0,
                                                 std::memory_order_relaxed);
    counters.transaction_row_wal_sets.store(0, std::memory_order_relaxed);

    transaction_manager.commit(txn, &log_manager);

    require_equal(counters.heap_page_write_guards.load(
                      std::memory_order_relaxed),
                  2, "commit reacquired a Heap page write guard");
    require_equal(counters.ix_leaf_write_guards.load(
                      std::memory_order_relaxed),
                  1, "commit reacquired the safe Index leaf write guard");
    require_equal(counters.wal_row_append_latch_acquires.load(
                      std::memory_order_relaxed),
                  1, "commit reacquired the row-WAL append latch");
    require_equal(counters.transaction_row_wal_sets.load(
                      std::memory_order_relaxed),
                  1, "commit appended more than one transaction row-WAL set");

    transaction_manager.release_transaction(txn);

    // Reservation must use the maintained free-page candidates.  Scanning
    // from page 1 makes every append O(table size) on the official dataset.
    Rid tail{};
    int row = 1000;
    while (tail.page_no < 40) {
        std::vector<char> record = raw_row(row++);
        tail = file->insert_record(record.data(), nullptr);
    }
    const uint64_t fetches_before_reserve =
        counters.buffer_fetches.load(std::memory_order_relaxed);
    std::vector<Rid> reservation = file->reserve_insert_slots(1);
    const uint64_t reservation_fetches =
        counters.buffer_fetches.load(std::memory_order_relaxed) -
        fetches_before_reserve;
    require(reservation.size() == 1,
            "Heap reservation did not return one slot");
    require(reservation_fetches <= 2,
            "Heap reservation scanned historical table pages");
    file->release_reserved_slots(reservation);

    system_manager.close_db();
    const int log_fd = disk.GetLogFd();
    if (log_fd >= 0) {
        disk.close_file(log_fd);
    }
    std::filesystem::current_path(previous);
    std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
    setenv("RMDB_PERF_DIAG", "1", 1);
    require_one_commit_batches_each_storage_unit();
    std::cout << "storage commit batch tests passed\n";
    return 0;
}
