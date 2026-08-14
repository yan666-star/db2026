#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <unistd.h>

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

class CountingDiskManager : public DiskManager {
   public:
    void sync_all_open_files() override {
        sync_all_calls_.fetch_add(1, std::memory_order_relaxed);
        DiskManager::sync_all_open_files();
    }

    void sync_file(const std::string &path) override {
        database_fsync_calls_.fetch_add(1, std::memory_order_relaxed);
        DiskManager::sync_file(path);
    }

    void reset_counts() {
        sync_all_calls_.store(0, std::memory_order_relaxed);
        database_fsync_calls_.store(0, std::memory_order_relaxed);
    }

    int sync_all_calls() const {
        return sync_all_calls_.load(std::memory_order_relaxed);
    }

    int database_fsync_calls() const {
        return database_fsync_calls_.load(std::memory_order_relaxed);
    }

   private:
    std::atomic<int> sync_all_calls_{0};
    std::atomic<int> database_fsync_calls_{0};
};

std::vector<Value> row_values(int id, int payload) {
    Value id_value;
    id_value.set_int(id);
    Value payload_value;
    payload_value.set_int(payload);
    return {id_value, payload_value};
}

void test_pre_apply_abort_does_not_checkpoint_or_fsync_database_files() {
    char directory_template[] = "/tmp/rmdb-abort-no-sync-XXXXXX";
    char *directory = mkdtemp(directory_template);
    require(directory != nullptr, "mkdtemp failed");
    const std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::current_path(directory);

    CountingDiskManager disk;
    BufferPoolManager buffer_pool(64, &disk);
    RmManager record_manager(&disk, &buffer_pool);
    IxManager index_manager(&disk, &buffer_pool);
    SmManager system_manager(&disk, &buffer_pool, &record_manager,
                             &index_manager);
    LockManager lock_manager;
    TransactionManager transaction_manager(&lock_manager, &system_manager);
    LogManager log_manager(&disk);

    system_manager.create_db("abort_db");
    system_manager.open_db("abort_db");
    system_manager.create_table(
        "items", {ColDef{"id", TYPE_INT, static_cast<int>(sizeof(int))},
                  ColDef{"payload", TYPE_INT,
                         static_cast<int>(sizeof(int))}},
        nullptr);
    system_manager.create_index("items", {"id"}, nullptr);

    Transaction *writer = transaction_manager.begin(
        nullptr, &log_manager, IsolationLevel::SNAPSHOT_ISOLATION);
    Context writer_context(&lock_manager, &log_manager, writer,
                           &transaction_manager);
    InsertExecutor insert(&system_manager, "items", row_values(7, 70),
                          &writer_context);
    insert.Next();

    disk.reset_counts();
    transaction_manager.abort(writer, &log_manager);
    require(disk.sync_all_calls() == 0,
            "pre-apply abort synchronized all database files");
    require(disk.database_fsync_calls() == 0,
            "pre-apply abort fsynced a database file");

    transaction_manager.release_transaction(writer);
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
    test_pre_apply_abort_does_not_checkpoint_or_fsync_database_files();
    std::cout << "abort without checkpoint tests passed\n";
    return 0;
}
