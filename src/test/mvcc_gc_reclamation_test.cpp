#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

#include "transaction/transaction_manager.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::unique_ptr<RmRecord> make_int_record(int value) {
    auto record = std::make_unique<RmRecord>(sizeof(value));
    std::memcpy(record->data, &value, sizeof(value));
    return record;
}

std::weak_ptr<TxnControl> commit_insert(TransactionManager &manager,
                                        uint64_t file_id, const Rid &rid) {
    Transaction *writer = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    std::weak_ptr<TxnControl> owner = writer->get_control();
    manager.prepare_insert(writer, file_id, rid, *make_int_record(73));
    manager.commit(writer, nullptr);
    manager.release_transaction(writer);
    return owner;
}

void test_gc_releases_settled_final_versions() {
    TransactionManager manager(nullptr, nullptr);
    const auto insert_owner = commit_insert(manager, 41, Rid{3, 7});

    require(!insert_owner.expired(),
            "version history disappeared before garbage collection");

    manager.GarbageCollection();

    require(insert_owner.expired(),
            "GC retained the final committed row version and its owner");
}

void test_gc_preserves_insert_boundary_for_an_older_snapshot() {
    TransactionManager manager(nullptr, nullptr);
    Transaction *older = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    Transaction *writer = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    const Rid rid{4, 8};
    std::weak_ptr<TxnControl> writer_owner = writer->get_control();

    manager.prepare_insert(writer, 42, rid, *make_int_record(73));
    manager.commit(writer, nullptr);
    manager.release_transaction(writer);
    manager.GarbageCollection();

    auto visible_to_older = manager.get_visible_record(
        older, 42, rid, make_int_record(73));
    require(visible_to_older == nullptr,
            "GC exposed an INSERT to a snapshot older than its commit");
    require(!writer_owner.expired(),
            "GC released INSERT history still needed by an older snapshot");

    manager.abort(older, nullptr);
    manager.release_transaction(older);
    manager.GarbageCollection();

    require(writer_owner.expired(),
            "GC retained INSERT history after the safe watermark advanced");
}

}  // namespace

int main() {
    test_gc_releases_settled_final_versions();
    test_gc_preserves_insert_boundary_for_an_older_snapshot();
    std::cout << "MVCC GC reclamation tests passed\n";
    return 0;
}
