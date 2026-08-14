#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "record/rm_defs.h"
#include "transaction/txn_registry.h"

struct PreparedStorageCommit;
class TransactionManager;

struct VisibleVersion {
    bool exists = false;
    bool deleted = false;
    std::vector<char> data;
};

class MvccStore {
   public:
    static constexpr size_t kShardCount = 256;

    struct Version {
        txn_id_t owner = INVALID_TXN_ID;
        std::shared_ptr<TxnControl> owner_control;
        timestamp_t commit_ts = INVALID_TS;
        bool before_deleted = true;
        std::vector<char> before;
        bool deleted = false;
        std::vector<char> data;
        std::string table_name;
    };

    bool try_acquire_record_intent(const std::shared_ptr<TxnControl> &txn,
                                   RecordKey key);
    bool try_acquire_unique_intent(const std::shared_ptr<TxnControl> &txn,
                                   int index_id,
                                   std::string_view binary_key);
    VisibleVersion resolve(const std::shared_ptr<TxnControl> &reader,
                           RecordKey key, const RmRecord *physical) const;
    VisibleVersion resolve_latest(RecordKey key,
                                  const RmRecord *physical) const;
    void install_pending(const std::shared_ptr<TxnControl> &txn,
                         const PreparedStorageCommit &commit);
    void publish(const std::shared_ptr<TxnControl> &txn,
                 timestamp_t commit_ts);
    void abort(const std::shared_ptr<TxnControl> &txn) noexcept;

    bool any_versions() const noexcept {
        return any_versions_ever_.load(std::memory_order_acquire);
    }

   private:
    friend class TransactionManager;

    struct UniqueIntentKey {
        int index_id;
        std::string key;

        bool operator==(const UniqueIntentKey &other) const noexcept {
            return index_id == other.index_id && key == other.key;
        }
    };

    struct UniqueIntentKeyHash {
        size_t operator()(const UniqueIntentKey &intent) const noexcept {
            size_t seed = std::hash<int>{}(intent.index_id);
            seed ^= std::hash<std::string>{}(intent.key) + 0x9e3779b9 +
                    (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    struct Shard {
        mutable std::mutex latch;
        std::unordered_map<RecordKey, std::vector<Version>, RecordKeyHash>
            record_versions;
        std::unordered_map<RecordKey, std::shared_ptr<TxnControl>, RecordKeyHash>
            record_owners;
        std::unordered_map<UniqueIntentKey, std::shared_ptr<TxnControl>,
                           UniqueIntentKeyHash>
            unique_owners;
        std::unordered_set<RecordKey, RecordKeyHash> gc_dirty_keys;
    };

    static size_t shard_index(const RecordKey &key) noexcept {
        return RecordKeyHash{}(key) & (kShardCount - 1);
    }
    static size_t shard_index(const UniqueIntentKey &key) noexcept {
        return UniqueIntentKeyHash{}(key) & (kShardCount - 1);
    }
    static bool owner_is_resolved(
        const std::shared_ptr<TxnControl> &owner) noexcept;
    void release_intents(const std::shared_ptr<TxnControl> &txn) noexcept;

    std::array<Shard, kShardCount> shards_;
    std::atomic<bool> any_versions_ever_{false};
};
