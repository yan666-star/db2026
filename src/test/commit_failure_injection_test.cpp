#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "execution/executor_insert.h"
#include "record/rm_manager.h"
#include "recovery/log_recovery.h"
#include "system/sm.h"
#include "transaction/transaction_manager.h"

namespace {

void require(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::vector<Value> row(int id) {
    Value value;
    value.set_int(id);
    return {value};
}

void insert_and_commit(SmManager &sm, LockManager &locks,
                       TransactionManager &txns, LogManager &log, int id) {
    Transaction *txn = txns.begin(
        nullptr, &log, IsolationLevel::SNAPSHOT_ISOLATION);
    Context context(&locks, &log, txn, &txns);
    InsertExecutor insert(&sm, "items", row(id), &context);
    insert.Next();
    txns.commit(txn, &log);
    txns.release_transaction(txn);
}

[[noreturn]] void crash_at(const std::filesystem::path &root,
                           const char *point) {
    std::filesystem::current_path(root);
    DiskManager disk;
    BufferPoolManager pool(64, &disk);
    RmManager records(&disk, &pool);
    IxManager indexes(&disk, &pool);
    SmManager sm(&disk, &pool, &records, &indexes);
    LockManager locks;
    LogManager log(&disk);
    sm.create_db("db");
    sm.open_db("db");
    log.initialize_from_disk();
    pool.set_log_manager(&log);
    sm.create_table("items", {{"id", TYPE_INT, 4}}, nullptr);
    sm.create_index("items", {"id"}, nullptr);
    {
        TransactionManager txns(&locks, &sm);
        insert_and_commit(sm, locks, txns, log, 1);
    }

    setenv("RMDB_COMMIT_FAILURE_POINT", point, 1);
    TransactionManager injected(&locks, &sm);
    injected.advance_next_txn_id(1);
    insert_and_commit(sm, locks, injected, log, 2);
    std::_Exit(99);
}

bool indexed_row_exists(SmManager &sm, int id) {
    const IndexMeta &meta = sm.db_.get_table("items").indexes.front();
    const std::string name = sm.get_ix_manager()->get_index_name(
        "items", meta.cols);
    std::vector<Rid> rids;
    if (!sm.ihs_.at(name)->get_value(reinterpret_cast<char *>(&id), &rids,
                                     nullptr)) {
        return false;
    }
    return rids.size() == 1 &&
           sm.fhs_.at("items")->get_record(rids.front(), nullptr) != nullptr;
}

void restart_and_check(const std::filesystem::path &root,
                       bool candidate_must_exist) {
    unsetenv("RMDB_COMMIT_FAILURE_POINT");
    std::filesystem::current_path(root);
    DiskManager disk;
    BufferPoolManager pool(64, &disk);
    RmManager records(&disk, &pool);
    IxManager indexes(&disk, &pool);
    SmManager sm(&disk, &pool, &records, &indexes);
    LogManager log(&disk);
    sm.open_db("db");
    log.initialize_from_disk();
    pool.set_log_manager(&log);
    RecoveryManager recovery(&disk, &sm, &log);
    recovery.analyze();
    recovery.redo();
    recovery.undo();
    require(indexed_row_exists(sm, 1), "unrelated durable row was lost");
    require(indexed_row_exists(sm, 2) == candidate_must_exist,
            "candidate Heap/Index visibility disagrees with durable COMMIT");
    sm.close_db();
}

void run_boundary(const char *point, bool durable) {
    char path[] = "/tmp/rmdb-commit-boundary-XXXXXX";
    require(mkdtemp(path) != nullptr, "mkdtemp failed");
    const std::filesystem::path root(path);
    const pid_t child = fork();
    require(child >= 0, "fork failed");
    if (child == 0) {
        crash_at(root, point);
    }
    int status = 0;
    require(waitpid(child, &status, 0) == child, "waitpid failed");
    require(WIFEXITED(status) && WEXITSTATUS(status) == 86,
            std::string("failure point did not terminate at ") + point);
    restart_and_check(root, durable);
    std::filesystem::remove_all(root);
}

}  // namespace

int main() {
    const std::filesystem::path previous = std::filesystem::current_path();
    run_boundary("BEFORE_HEAP", false);
    std::filesystem::current_path(previous);
    run_boundary("AFTER_HEAP", false);
    std::filesystem::current_path(previous);
    run_boundary("AFTER_INDEX", false);
    std::filesystem::current_path(previous);
    run_boundary("AFTER_COMMIT_WRITE", false);
    std::filesystem::current_path(previous);
    run_boundary("AFTER_COMMIT_SYNC", true);
    std::filesystem::current_path(previous);
    run_boundary("AFTER_PUBLISH", true);
    std::filesystem::current_path(previous);
    run_boundary("BEFORE_ACK", true);
    std::filesystem::current_path(previous);
    std::cout << "commit failure injection tests passed\n";
}
