#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/config.h"
#include "record/rm_defs.h"
#include "transaction/txn_registry.h"

/**
 * Runtime-only history for physical index mappings removed by a key-changing
 * UPDATE or DELETE. Recovery starts without active old snapshots, so this
 * structure deliberately does not alter the on-disk B+Tree or WAL formats.
 */
class IndexVersionStore {
   public:
    struct Entry {
        std::vector<char> old_key;
        Rid rid;
        txn_id_t owner = INVALID_TXN_ID;
        std::shared_ptr<TxnControl> owner_control;
        timestamp_t valid_until = INVALID_TS;
    };

    void retain(int index_id, std::vector<char> old_key, const Rid &rid,
                const std::shared_ptr<TxnControl> &owner);
    std::vector<Entry> snapshot(int index_id) const;
    bool conflicts_with_snapshot(int index_id, const std::vector<char> &key,
                                 const Rid &target_rid, txn_id_t txn_id,
                                 timestamp_t start_ts) const;
    void finalize(const std::shared_ptr<TxnControl> &owner,
                  timestamp_t commit_ts);
    void discard(const std::shared_ptr<TxnControl> &owner);
    void garbage_collect(timestamp_t watermark);

   private:
    static constexpr size_t kShardCount = 64;
    struct Shard {
        mutable std::mutex latch;
        std::unordered_map<int, std::vector<Entry>> by_index;
    };
    std::array<Shard, kShardCount> shards_;
    std::atomic<size_t> entry_count_{0};

    static size_t shard_index(int index_id) {
        return std::hash<int>{}(index_id) & (kShardCount - 1);
    }
};
