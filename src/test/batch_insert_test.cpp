#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

#include <unistd.h>

#include "record/rm_manager.h"
#include "record/rm_scan.h"
#include "system/sm.h"
#include "transaction/transaction_manager.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_heap_batch_registers_undo_before_physical_write() {
    char directory_template[] = "/tmp/rmdb-batch-insert-XXXXXX";
    char *directory = mkdtemp(directory_template);
    require(directory != nullptr, "mkdtemp failed");
    const std::filesystem::path previous =
        std::filesystem::current_path();
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

    system_manager.create_db("batch_db");
    system_manager.open_db("batch_db");
    system_manager.create_table(
        "generic_rows", {ColDef{"payload", TYPE_STRING, 256}}, nullptr);
    RmFileHandle *file = system_manager.fhs_.at("generic_rows").get();

    Transaction *txn = transaction_manager.begin(
        nullptr, &log_manager, IsolationLevel::SNAPSHOT_ISOLATION);
    Context context(&lock_manager, &log_manager, txn,
                    &transaction_manager);

    constexpr int kRows = 80;
    std::vector<std::vector<char>> records(
        kRows, std::vector<char>(256, 0));
    std::vector<PendingInsert> pending;
    pending.reserve(kRows);
    for (int row = 0; row < kRows; ++row) {
        std::memcpy(records[row].data(), &row, sizeof(row));
        pending.push_back(PendingInsert{records[row].data(),
                                        records[row].size()});
    }
    const std::vector<Rid> rids =
        file->insert_records(pending, &context, "generic_rows");
    require(rids.size() == kRows,
            "heap batch did not insert every prepared record");
    require(txn->get_write_set()->size() == kRows,
            "heap batch exposed rows before registering MVCC undo");

    transaction_manager.abort(txn, &log_manager);
    RmScan scan(file);
    require(scan.is_end(),
            "aborting a multi-page heap batch left physical rows behind");

    transaction_manager.release_transaction(txn);
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
    test_heap_batch_registers_undo_before_physical_write();
    std::cout << "batch insert tests passed\n";
    return 0;
}
