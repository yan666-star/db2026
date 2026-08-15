#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "record/rm_defs.h"
#include "transaction/txn_defs.h"

enum class TxnVisibilityState : uint8_t {
    ACTIVE,
    VALIDATING,
    APPLYING,
    DURABLE,
    VISIBLE,
    ABORTED
};

struct RecordKey {
    uint64_t file_id;
    Rid rid;

    bool operator==(const RecordKey &other) const noexcept {
        return file_id == other.file_id && rid == other.rid;
    }
};

struct RecordKeyHash {
    size_t operator()(const RecordKey &key) const noexcept {
        const uint64_t rid_bits =
            (static_cast<uint64_t>(static_cast<uint32_t>(key.rid.page_no))
             << 32) |
            static_cast<uint32_t>(key.rid.slot_no);
        size_t seed = std::hash<uint64_t>{}(key.file_id);
        seed ^= std::hash<uint64_t>{}(rid_bits) + 0x9e3779b9 +
                (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct TxnControl {
    explicit TxnControl(txn_id_t txn_id,
                        IsolationLevel level = IsolationLevel::READ_COMMITTED)
        : id(txn_id), isolation_level(level) {}

    txn_id_t id;
    IsolationLevel isolation_level;
    timestamp_t start_ts = 0;
    std::atomic<timestamp_t> commit_ts{INVALID_TS};
    std::atomic<TxnVisibilityState> state{TxnVisibilityState::ACTIVE};
    std::atomic<bool> entered_apply{false};
    std::atomic<bool> cleanup_done{false};

    // SERIALIZABLE dependency sets are never entered by SNAPSHOT ISOLATION.
    std::mutex serializable_latch;
    std::unordered_set<txn_id_t> incoming_rw;
    std::unordered_set<txn_id_t> outgoing_rw;

    void track_record_intent(RecordKey key);
    void track_unique_intent(int index_id, std::string binary_key);
    std::vector<RecordKey> record_intents_snapshot() const;
    std::vector<std::pair<int, std::string>> unique_intents_snapshot() const;
    void clear_intents();

   private:
    mutable std::mutex intents_latch_;
    std::unordered_set<RecordKey, RecordKeyHash> record_intents_;
    std::vector<std::pair<int, std::string>> unique_intents_;
};

class TxnRegistry {
   public:
    void attach(const std::shared_ptr<TxnControl> &control);
    std::shared_ptr<TxnControl> find(txn_id_t txn_id) const;
    void release(txn_id_t txn_id);
    std::vector<std::shared_ptr<TxnControl>> snapshot() const;

    timestamp_t current_commit_ts() const noexcept;
    timestamp_t next_commit_ts() noexcept;
    void advance_commit_ts(timestamp_t timestamp) noexcept;

    // A snapshot reads only the greatest contiguous timestamp whose commit
    // has finished publishing. Committers may publish out of order, but a gap
    // cannot escape into a snapshot timestamp.
    timestamp_t capture_snapshot_ts() const;
    timestamp_t publish_commit(
        const std::function<void(timestamp_t)> &publish);

   private:
    void complete_commit_ts(timestamp_t commit_ts);

    mutable std::shared_mutex latch_;
    std::unordered_map<txn_id_t, std::shared_ptr<TxnControl>> controls_;
    std::mutex publication_latch_;
    std::unordered_set<timestamp_t> completed_commit_ts_;
    std::atomic<timestamp_t> allocated_commit_clock_{0};
    std::atomic<timestamp_t> commit_clock_{0};
};
