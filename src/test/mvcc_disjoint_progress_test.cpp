#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>

#include "transaction/transaction_manager.h"

namespace {

using namespace std::chrono_literals;

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

size_t planned_shard(uint64_t file_id, const Rid &rid) {
    const uint64_t rid_bits =
        (static_cast<uint64_t>(static_cast<uint32_t>(rid.page_no)) << 32) |
        static_cast<uint32_t>(rid.slot_no);
    size_t seed = std::hash<uint64_t>{}(file_id);
    seed ^= std::hash<uint64_t>{}(rid_bits) + 0x9e3779b9 +
            (seed << 6) + (seed >> 2);
    return seed & 255;
}

Rid find_different_shard(uint64_t file_id, const Rid &owner_rid) {
    const size_t owner_shard = planned_shard(file_id, owner_rid);
    for (int slot = 0; slot < 4096; ++slot) {
        Rid candidate{owner_rid.page_no + 1, slot};
        if (planned_shard(file_id, candidate) != owner_shard) {
            return candidate;
        }
    }
    require(false, "could not construct a hash-distinct record key");
    return Rid{};
}

void test_disjoint_shard_progresses_while_owner_is_paused() {
    TransactionManager manager(nullptr, nullptr);
    Transaction *contender = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    Transaction *paused_owner = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    constexpr uint64_t file_id = 41;
    const Rid owner_rid{11, 7};
    const Rid disjoint_rid = find_different_shard(file_id, owner_rid);

    std::promise<void> owner_acquired;
    std::promise<void> release_owner;
    auto release_signal = release_owner.get_future().share();
    auto owner = std::async(std::launch::async, [&] {
        manager.prepare_insert(
            paused_owner, file_id, owner_rid, *make_int_record(101));
        owner_acquired.set_value();
        release_signal.wait();
    });
    owner_acquired.get_future().wait();

    auto disjoint = std::async(std::launch::async, [&] {
        manager.prepare_insert(
            contender, file_id, disjoint_rid, *make_int_record(202));
        return true;
    });
    const bool progressed =
        disjoint.wait_for(50ms) == std::future_status::ready && disjoint.get();

    release_owner.set_value();
    owner.get();
    require(progressed,
            "disjoint MVCC shard did not progress while owner was paused");

    manager.abort(contender, nullptr);
    manager.abort(paused_owner, nullptr);
    manager.release_transaction(contender);
    manager.release_transaction(paused_owner);
}

}  // namespace

int main() {
    test_disjoint_shard_progresses_while_owner_is_paused();
    std::cout << "MVCC disjoint progress tests passed\n";
    return 0;
}
