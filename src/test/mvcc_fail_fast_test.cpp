#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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

int record_value(const RmRecord &record) {
    int value = 0;
    std::memcpy(&value, record.data, sizeof(value));
    return value;
}

std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + path.string());
    }
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

void test_same_record_conflict_returns_within_50ms() {
    TransactionManager manager(nullptr, nullptr);
    Transaction *contender = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    Transaction *owner = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    const Rid rid{7, 9};
    manager.prepare_insert(owner, 31, rid, *make_int_record(81));

    auto attempt = std::async(std::launch::async, [&] {
        const auto started = std::chrono::steady_clock::now();
        try {
            manager.prepare_insert(
                contender, 31, rid, *make_int_record(82));
        } catch (TransactionAbortException &error) {
            return std::make_pair(
                error.GetAbortReason() == AbortReason::WRITE_CONFLICT,
                std::chrono::steady_clock::now() - started);
        }
        return std::make_pair(false,
                              std::chrono::steady_clock::now() - started);
    });

    const auto status = attempt.wait_for(50ms);
    if (status != std::future_status::ready) {
        manager.abort(owner, nullptr);
        attempt.wait();
        manager.abort(contender, nullptr);
        manager.release_transaction(owner);
        manager.release_transaction(contender);
        require(false,
                "same-record conflict waited instead of failing within 50ms");
    }

    const auto [write_conflict, elapsed] = attempt.get();
    require(write_conflict,
            "same-record contender did not receive WRITE_CONFLICT");
    require(elapsed < 50ms,
            "same-record WRITE_CONFLICT exceeded the 50ms bound");
    manager.abort(owner, nullptr);
    manager.abort(contender, nullptr);
    manager.release_transaction(owner);
    manager.release_transaction(contender);
}

void test_same_unique_key_conflict_returns_within_50ms() {
    TransactionManager manager(nullptr, nullptr);
    Transaction *contender = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    Transaction *owner = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    const std::vector<char> key{'f', 'a', 'i', 'l', '-', 'f', 'a', 's', 't'};
    manager.acquire_unique_key_intent(owner, 19, key);

    auto attempt = std::async(std::launch::async, [&] {
        const auto started = std::chrono::steady_clock::now();
        try {
            manager.acquire_unique_key_intent(contender, 19, key);
        } catch (TransactionAbortException &error) {
            return std::make_pair(
                error.GetAbortReason() == AbortReason::WRITE_CONFLICT,
                std::chrono::steady_clock::now() - started);
        }
        return std::make_pair(false,
                              std::chrono::steady_clock::now() - started);
    });

    const auto status = attempt.wait_for(50ms);
    if (status != std::future_status::ready) {
        manager.abort(owner, nullptr);
        attempt.wait();
        manager.abort(contender, nullptr);
        manager.release_transaction(owner);
        manager.release_transaction(contender);
        require(false,
                "same-unique-key conflict waited instead of failing within 50ms");
    }

    const auto [write_conflict, elapsed] = attempt.get();
    require(write_conflict,
            "same-unique-key contender did not receive WRITE_CONFLICT");
    require(elapsed < 50ms,
            "same-unique-key WRITE_CONFLICT exceeded the 50ms bound");
    manager.abort(owner, nullptr);
    manager.abort(contender, nullptr);
    manager.release_transaction(owner);
    manager.release_transaction(contender);
}

void test_pending_update_exposes_before_image_until_visible() {
    TransactionManager manager(nullptr, nullptr);
    Transaction *writer = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    const Rid rid{13, 5};
    manager.prepare_update(writer, 47, rid, *make_int_record(10),
                           *make_int_record(20));

    auto visible = manager.get_latest_committed_record(
        47, rid, make_int_record(20));
    require(visible != nullptr && record_value(*visible) == 10,
            "reader observed an UPDATE after-image before owner publication");

    manager.abort(writer, nullptr);
    manager.release_transaction(writer);
}

void test_insert_closes_ssi_cycle_at_statement_boundary() {
    TransactionManager manager(nullptr, nullptr);
    Transaction *first = manager.begin(
        nullptr, nullptr, IsolationLevel::SERIALIZABLE);
    Transaction *current = manager.begin(
        nullptr, nullptr, IsolationLevel::SERIALIZABLE);

    const uint64_t record_file = 71;
    const Rid written_rid{4, 2};
    manager.prepare_insert(
        first, record_file, written_rid, *make_int_record(10));
    manager.register_record_read(current, record_file, written_rid);

    const uint64_t predicate_file = 72;
    manager.register_table_read(first, predicate_file, {}, {});

    bool aborted_current = false;
    try {
        manager.check_insert_conflict(
            current, predicate_file, *make_int_record(20));
    } catch (TransactionAbortException &error) {
        aborted_current =
            error.GetAbortReason() == AbortReason::SERIALIZATION_FAILURE;
    }
    require(aborted_current,
            "INSERT completing an SSI cycle did not abort its current transaction");

    manager.abort(first, nullptr);
    manager.abort(current, nullptr);
    manager.release_transaction(first);
    manager.release_transaction(current);
}

void test_insert_closes_three_transaction_dangerous_structure() {
    TransactionManager manager(nullptr, nullptr);
    Transaction *incoming = manager.begin(
        nullptr, nullptr, IsolationLevel::SERIALIZABLE);
    Transaction *current = manager.begin(
        nullptr, nullptr, IsolationLevel::SERIALIZABLE);
    Transaction *outgoing = manager.begin(
        nullptr, nullptr, IsolationLevel::SERIALIZABLE);

    const uint64_t incoming_predicate_file = 81;
    const uint64_t outgoing_predicate_file = 82;
    manager.register_table_read(incoming, incoming_predicate_file, {}, {});
    manager.register_table_read(current, outgoing_predicate_file, {}, {});

    // Establish current ->rw outgoing, then settle outgoing before the
    // incoming transaction.  The next INSERT adds incoming ->rw current and
    // must abort the current statement transaction under the finals rule.
    manager.check_insert_conflict(
        outgoing, outgoing_predicate_file, *make_int_record(30));
    manager.commit(outgoing, nullptr);

    bool aborted_current = false;
    try {
        manager.check_insert_conflict(
            current, incoming_predicate_file, *make_int_record(40));
    } catch (TransactionAbortException &error) {
        aborted_current =
            error.GetAbortReason() == AbortReason::SERIALIZATION_FAILURE;
    }
    require(aborted_current,
            "three-transaction INSERT danger did not abort the current statement");

    manager.abort(incoming, nullptr);
    manager.abort(current, nullptr);
    manager.release_transaction(incoming);
    manager.release_transaction(current);
    manager.release_transaction(outgoing);
}

void test_si_source_has_no_wait_or_global_publication_contract(
    const std::filesystem::path &root) {
    const std::string manager_h = read_file(
        root / "src" / "transaction" / "transaction_manager.h");
    const std::string manager_cpp = read_file(
        root / "src" / "transaction" / "transaction_manager.cpp");
    const std::string store_h = read_file(
        root / "src" / "transaction" / "mvcc_store.h");
    const std::string store_cpp = read_file(
        root / "src" / "transaction" / "mvcc_store.cpp");
    const std::string coordination = manager_h + manager_cpp + store_h + store_cpp;

    require(coordination.find("mvcc_cv_.wait") == std::string::npos,
            "SI path still waits on mvcc_cv_");
    require(coordination.find("shard.cv.wait") == std::string::npos &&
                coordination.find("UniqueIntentShard") == std::string::npos,
            "unique intents still use a condition variable");
    require(coordination.find("wait_for_pending_writer") == std::string::npos,
            "record conflicts still route through transaction waiting");
    require(coordination.find("txn_state_latch_") == std::string::npos,
            "SI path still uses the global transaction-state latch");
    require(coordination.find("commit_apply_latch_") == std::string::npos,
            "SI path still uses the global publisher/apply latch");
    require(store_h.find("kShardCount = 256") != std::string::npos,
            "MVCC record/unique intent store is not split into 256 shards");
    require(store_cpp.find("sleep_for") == std::string::npos &&
                store_cpp.find("sleep_until") == std::string::npos,
            "MVCC conflicts must never sleep");
}

}  // namespace

int main(int argc, char **argv) {
    require(argc == 2, "expected repository source directory");
    test_same_record_conflict_returns_within_50ms();
    test_same_unique_key_conflict_returns_within_50ms();
    test_pending_update_exposes_before_image_until_visible();
    test_insert_closes_ssi_cycle_at_statement_boundary();
    test_insert_closes_three_transaction_dangerous_structure();
    test_si_source_has_no_wait_or_global_publication_contract(argv[1]);
    std::cout << "MVCC fail-fast tests passed\n";
    return 0;
}
