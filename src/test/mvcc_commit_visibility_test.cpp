#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>

#include <unistd.h>

#include "recovery/log_manager.h"
#include "transaction/transaction_manager.h"

namespace {

using namespace std::chrono_literals;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class BlockingSyncDiskManager : public DiskManager {
   public:
    void sync_log() override {
        std::unique_lock<std::mutex> lock(latch_);
        sync_started_ = true;
        cv_.notify_all();
        cv_.wait(lock, [&] { return allow_sync_; });
        lock.unlock();
        DiskManager::sync_log();
    }

    void wait_for_sync() {
        std::unique_lock<std::mutex> lock(latch_);
        require(cv_.wait_for(lock, 2s, [&] { return sync_started_; }),
                "commit did not reach WAL sync");
    }

    void release_sync() {
        std::lock_guard<std::mutex> lock(latch_);
        allow_sync_ = true;
        cv_.notify_all();
    }

   private:
    std::mutex latch_;
    std::condition_variable cv_;
    bool sync_started_ = false;
    bool allow_sync_ = false;
};

std::unique_ptr<RmRecord> make_int_record(int value) {
    auto record = std::make_unique<RmRecord>(sizeof(value));
    std::memcpy(record->data, &value, sizeof(value));
    return record;
}

void test_commit_is_hidden_until_wal_is_durable() {
    char directory_template[] = "/tmp/rmdb-mvcc-visible-XXXXXX";
    char *directory = mkdtemp(directory_template);
    require(directory != nullptr, "mkdtemp failed");
    const std::filesystem::path previous =
        std::filesystem::current_path();
    std::filesystem::current_path(directory);
    std::ofstream(LOG_FILE_NAME, std::ios::binary).close();

    BlockingSyncDiskManager disk;
    auto log = std::make_unique<LogManager>(&disk);
    TransactionManager manager(nullptr, nullptr);
    Transaction *writer = manager.begin(
        nullptr, log.get(), IsolationLevel::SNAPSHOT_ISOLATION);
    Rid rid{1, 2};
    auto inserted = make_int_record(73);
    manager.prepare_insert(writer, 0, rid, *inserted);
    writer->append_write_record(
        new WriteRecord(WType::INSERT_TUPLE, "probe", rid));

    auto commit = std::async(std::launch::async, [&] {
        manager.commit(writer, log.get());
        return true;
    });
    disk.wait_for_sync();

    Transaction observer(999, IsolationLevel::SNAPSHOT_ISOLATION);
    observer.set_start_ts(std::numeric_limits<timestamp_t>::max());
    auto before_durable = manager.get_visible_record(
        &observer, 0, rid, make_int_record(73));
    require(before_durable == nullptr,
            "MVCC version became visible before COMMIT WAL was durable");

    disk.release_sync();
    require(commit.wait_for(2s) == std::future_status::ready && commit.get(),
            "commit did not complete after WAL sync");
    auto after_durable = manager.get_visible_record(
        &observer, 0, rid, make_int_record(73));
    require(after_durable != nullptr,
            "durable MVCC version was not published");

    const int log_fd = disk.GetLogFd();
    if (log_fd >= 0) {
        disk.close_file(log_fd);
    }
    manager.release_transaction(writer);
    std::filesystem::current_path(previous);
    std::filesystem::remove_all(directory);
}

void test_snapshot_timestamp_excludes_incomplete_publication() {
    TxnRegistry registry;
    std::mutex latch;
    std::condition_variable cv;
    bool publication_entered = false;
    bool allow_publication = false;
    bool version_visible = false;

    auto publisher = std::async(std::launch::async, [&] {
        return registry.publish_commit([&](timestamp_t) {
            std::unique_lock<std::mutex> lock(latch);
            publication_entered = true;
            cv.notify_all();
            cv.wait(lock, [&] { return allow_publication; });
            version_visible = true;
        });
    });

    {
        std::unique_lock<std::mutex> lock(latch);
        require(cv.wait_for(lock, 2s, [&] { return publication_entered; }),
                "commit did not enter version publication");
    }

    auto during_publication = std::async(std::launch::async, [&] {
        const timestamp_t timestamp = registry.capture_snapshot_ts();
        std::lock_guard<std::mutex> lock(latch);
        return std::make_pair(timestamp, version_visible);
    });
    require(during_publication.wait_for(2s) == std::future_status::ready,
            "snapshot capture blocked behind version publication");
    const auto [during_ts, visible_during] = during_publication.get();
    require(during_ts == 0 && !visible_during,
            "snapshot timestamp escaped before version publication");

    {
        std::lock_guard<std::mutex> lock(latch);
        allow_publication = true;
        cv.notify_all();
    }
    require(publisher.wait_for(2s) == std::future_status::ready,
            "version publication did not complete");
    const timestamp_t commit_ts = publisher.get();
    const timestamp_t after_ts = registry.capture_snapshot_ts();
    require(version_visible && after_ts == commit_ts,
            "snapshot included a commit timestamp without its visible version");
}

void test_out_of_order_publication_advances_only_contiguous_prefix() {
    TxnRegistry registry;
    std::mutex latch;
    std::condition_variable cv;
    bool first_entered = false;
    bool allow_first = false;

    auto first = std::async(std::launch::async, [&] {
        return registry.publish_commit([&](timestamp_t commit_ts) {
            require(commit_ts == 1, "first commit received the wrong timestamp");
            std::unique_lock<std::mutex> lock(latch);
            first_entered = true;
            cv.notify_all();
            cv.wait(lock, [&] { return allow_first; });
        });
    });
    {
        std::unique_lock<std::mutex> lock(latch);
        require(cv.wait_for(lock, 2s, [&] { return first_entered; }),
                "first commit did not enter publication");
    }

    auto second = std::async(std::launch::async, [&] {
        return registry.publish_commit([](timestamp_t commit_ts) {
            require(commit_ts == 2,
                    "second commit received the wrong timestamp");
        });
    });
    require(second.wait_for(2s) == std::future_status::ready &&
                second.get() == 2,
            "second commit was serialized behind first publication");
    require(registry.capture_snapshot_ts() == 0,
            "out-of-order commit escaped across a publication gap");

    {
        std::lock_guard<std::mutex> lock(latch);
        allow_first = true;
        cv.notify_all();
    }
    require(first.wait_for(2s) == std::future_status::ready &&
                first.get() == 1,
            "first commit did not finish publication");
    require(registry.capture_snapshot_ts() == 2,
            "visible commit prefix did not cross a completed publication gap");
}

void test_record_intent_conflicts_fail_fast() {
    TransactionManager manager(nullptr, nullptr);
    Transaction *older = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    Transaction *younger = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    Rid rid{7, 9};
    auto younger_record = make_int_record(81);
    manager.prepare_insert(younger, 0, rid, *younger_record);

    auto older_attempt = std::async(std::launch::async, [&] {
        auto older_record = make_int_record(82);
        try {
            manager.prepare_insert(older, 0, rid, *older_record);
            return false;
        } catch (const TransactionAbortException &) {
            return true;
        }
    });
    const auto status = older_attempt.wait_for(50ms);
    if (status != std::future_status::ready) {
        manager.abort(younger, nullptr);
        older_attempt.wait();
        manager.abort(older, nullptr);
        manager.release_transaction(younger);
        manager.release_transaction(older);
        require(false, "older transaction waited behind a record intent");
    }
    require(older_attempt.get(),
            "record-intent contender did not receive WRITE_CONFLICT");
    manager.abort(younger, nullptr);
    manager.abort(older, nullptr);
    manager.release_transaction(younger);
    manager.release_transaction(older);
}

void test_unique_key_intent_conflicts_fail_fast() {
    TransactionManager manager(nullptr, nullptr);
    Transaction *older = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    Transaction *younger = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    const std::vector<char> key{'k', 'e', 'y'};
    manager.acquire_unique_key_intent(younger, 17, key);

    auto older_attempt = std::async(std::launch::async, [&] {
        try {
            manager.acquire_unique_key_intent(older, 17, key);
        } catch (TransactionAbortException &error) {
            return error.GetAbortReason() == AbortReason::WRITE_CONFLICT;
        }
        return false;
    });
    const auto status = older_attempt.wait_for(50ms);
    if (status != std::future_status::ready) {
        manager.abort(younger, nullptr);
        older_attempt.wait();
        manager.abort(older, nullptr);
        manager.release_transaction(younger);
        manager.release_transaction(older);
        require(false, "older transaction waited behind a unique intent");
    }
    require(older_attempt.get(),
            "unique-intent contender did not receive WRITE_CONFLICT");
    manager.abort(younger, nullptr);
    manager.abort(older, nullptr);
    manager.release_transaction(younger);
    manager.release_transaction(older);

    Transaction *first = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    Transaction *later = manager.begin(
        nullptr, nullptr, IsolationLevel::SNAPSHOT_ISOLATION);
    manager.acquire_unique_key_intent(first, 19, key);
    bool aborted = false;
    try {
        manager.acquire_unique_key_intent(later, 19, key);
    } catch (const TransactionAbortException &) {
        aborted = true;
    }
    require(aborted,
            "younger unique-key writer did not abort behind older owner");
    manager.abort(later, nullptr);
    manager.abort(first, nullptr);
    manager.release_transaction(later);
    manager.release_transaction(first);
}

}  // namespace

int main() {
    test_commit_is_hidden_until_wal_is_durable();
    test_snapshot_timestamp_excludes_incomplete_publication();
    test_out_of_order_publication_advances_only_contiguous_prefix();
    test_record_intent_conflicts_fail_fast();
    test_unique_key_intent_conflicts_fail_fast();
    std::cout << "MVCC commit visibility tests passed\n";
    return 0;
}
