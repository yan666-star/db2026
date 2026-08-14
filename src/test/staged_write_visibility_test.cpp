#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "system/sm.h"
#include "execution/executor_delete.h"
#include "execution/executor_index_scan.h"
#include "execution/executor_insert.h"
#include "execution/executor_seq_scan.h"
#include "execution/executor_update.h"
#include "record/rm_manager.h"
#include "record/rm_scan.h"
#include "transaction/transaction_manager.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::vector<Value> row_values(int id, int payload) {
    Value id_value;
    id_value.set_int(id);
    Value payload_value;
    payload_value.set_int(payload);
    return {id_value, payload_value};
}

std::unique_ptr<RmRecord> make_row(int id, int payload) {
    auto record = std::make_unique<RmRecord>(2 * sizeof(int));
    std::memcpy(record->data, &id, sizeof(id));
    std::memcpy(record->data + sizeof(id), &payload, sizeof(payload));
    return record;
}

std::pair<int, int> decode_row(const RmRecord &record) {
    std::pair<int, int> row;
    std::memcpy(&row.first, record.data, sizeof(row.first));
    std::memcpy(&row.second, record.data + sizeof(row.first),
                sizeof(row.second));
    return row;
}

Rid seed_row(SmManager *system_manager, RmFileHandle *file, int id,
             int payload) {
    auto record = make_row(id, payload);
    Rid rid = file->insert_record(record->data, nullptr);
    const auto &index = system_manager->db_.get_table("items").indexes.front();
    const std::string index_name =
        system_manager->get_ix_manager()->get_index_name("items", index.cols);
    system_manager->ihs_.at(index_name)->insert_entry(record->data, rid,
                                                          nullptr);
    return rid;
}

std::map<int, int> scan_rows(SmManager *system_manager, Context *context) {
    SeqScanExecutor scan(system_manager, "items", {}, context);
    std::map<int, int> rows;
    scan.beginTuple();
    while (!scan.is_end()) {
        auto record = scan.Next();
        require(record != nullptr, "scan returned an empty visible row");
        rows.insert(decode_row(*record));
        scan.nextTuple();
    }
    return rows;
}

bool raw_index_contains(SmManager *system_manager, int id) {
    const auto &index = system_manager->db_.get_table("items").indexes.front();
    const std::string index_name =
        system_manager->get_ix_manager()->get_index_name("items", index.cols);
    std::vector<Rid> found;
    return system_manager->ihs_.at(index_name)->get_value(
        reinterpret_cast<char *>(&id), &found, nullptr);
}

std::map<int, int> point_lookup(SmManager *system_manager, Context *context,
                                int id) {
    Condition condition;
    condition.lhs_col = {"items", "id"};
    condition.op = OP_EQ;
    condition.is_rhs_val = true;
    condition.rhs_val.set_int(id);
    condition.rhs_val.init_raw(sizeof(id));
    IndexScanExecutor scan(system_manager, "items", {condition}, {"id"},
                           context);
    std::map<int, int> rows;
    scan.beginTuple();
    while (!scan.is_end()) {
        auto record = scan.Next();
        require(record != nullptr, "index scan returned an empty visible row");
        rows.insert(decode_row(*record));
        scan.nextTuple();
    }
    return rows;
}

std::map<int, int> index_rows(SmManager *system_manager, Context *context) {
    IndexScanExecutor scan(system_manager, "items", {}, {"id"}, context);
    std::map<int, int> rows;
    scan.beginTuple();
    while (!scan.is_end()) {
        auto record = scan.Next();
        require(record != nullptr,
                "range index scan returned an empty visible row");
        rows.insert(decode_row(*record));
        scan.nextTuple();
    }
    return rows;
}

void test_si_executor_writes_are_private_until_commit() {
    char directory_template[] = "/tmp/rmdb-staged-writes-XXXXXX";
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

    system_manager.create_db("staged_db");
    system_manager.open_db("staged_db");
    system_manager.create_table(
        "items", {ColDef{"id", TYPE_INT, static_cast<int>(sizeof(int))},
                  ColDef{"payload", TYPE_INT,
                         static_cast<int>(sizeof(int))}},
        nullptr);
    system_manager.create_index("items", {"id"}, nullptr);
    RmFileHandle *file = system_manager.fhs_.at("items").get();
    const Rid update_rid = seed_row(&system_manager, file, 1, 10);
    const Rid delete_rid = seed_row(&system_manager, file, 2, 20);

    Transaction *writer = transaction_manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    Transaction *old_snapshot = transaction_manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    Context writer_context(&lock_manager, nullptr, writer,
                           &transaction_manager);
    Context old_context(&lock_manager, nullptr, old_snapshot,
                        &transaction_manager);

    InsertExecutor insert(&system_manager, "items", row_values(3, 30),
                          &writer_context);
    insert.Next();

    SetClause set_payload;
    set_payload.lhs = {"items", "payload"};
    set_payload.rhs.set_int(11);
    set_payload.rhs.init_raw(sizeof(int));
    UpdateExecutor update(&system_manager, "items", {set_payload}, {},
                          {update_rid}, &writer_context);
    update.Next();

    DeleteExecutor erase(&system_manager, "items", {}, {delete_rid},
                         &writer_context);
    erase.Next();

    require(file->record_exists(update_rid) &&
                decode_row(*file->get_record(update_rid, nullptr)).second == 10,
            "staged update changed the physical heap before commit");
    require(file->record_exists(delete_rid),
            "staged delete changed the physical heap before commit");
    require(!raw_index_contains(&system_manager, 3),
            "staged insert changed the physical index before commit");
    require(raw_index_contains(&system_manager, 2),
            "staged delete changed the physical index before commit");

    require(scan_rows(&system_manager, &writer_context) ==
                std::map<int, int>{{1, 11}, {3, 30}},
            "writer did not see its complete staged overlay");
    require(point_lookup(&system_manager, &writer_context, 3) ==
                std::map<int, int>{{3, 30}},
            "writer index scan did not see its staged insert");
    require(index_rows(&system_manager, &writer_context) ==
                std::map<int, int>{{1, 11}, {3, 30}},
            "writer range index scan did not merge its staged overlay");
    require(scan_rows(&system_manager, &old_context) ==
                std::map<int, int>{{1, 10}, {2, 20}},
            "another SI transaction observed uncommitted staged writes");

    transaction_manager.commit(writer, nullptr);
    Transaction *new_snapshot = transaction_manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    Context new_context(&lock_manager, nullptr, new_snapshot,
                        &transaction_manager);
    require(scan_rows(&system_manager, &new_context) ==
                std::map<int, int>{{1, 11}, {3, 30}},
            "commit did not publish the complete staged result");
    require(point_lookup(&system_manager, &new_context, 3) ==
                std::map<int, int>{{3, 30}},
            "commit did not materialize the staged index entry");

    transaction_manager.abort(old_snapshot, nullptr);
    transaction_manager.abort(new_snapshot, nullptr);
    transaction_manager.release_transaction(writer);
    transaction_manager.release_transaction(old_snapshot);
    transaction_manager.release_transaction(new_snapshot);
    system_manager.close_db();
    std::filesystem::current_path(previous);
    std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
    test_si_executor_writes_are_private_until_commit();
    std::cout << "staged write visibility tests passed\n";
    return 0;
}
