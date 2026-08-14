#include "transaction/index_version_store.h"

#include <algorithm>

void IndexVersionStore::retain(int index_id, std::vector<char> old_key,
                               const Rid &rid,
                               const std::shared_ptr<TxnControl> &owner) {
    if (owner == nullptr) {
        return;
    }
    Shard &shard = shards_[shard_index(index_id)];
    bool inserted = false;
    {
        std::lock_guard<std::mutex> lock(shard.latch);
        auto &entries = shard.by_index[index_id];
        auto duplicate = std::find_if(
            entries.begin(), entries.end(), [&](const Entry &entry) {
                return entry.owner_control == owner && entry.rid == rid &&
                       entry.old_key == old_key;
            });
        if (duplicate == entries.end()) {
            entries.push_back(
                Entry{std::move(old_key), rid, owner->id, owner, INVALID_TS});
            inserted = true;
        }
    }
    if (inserted) {
        entry_count_.fetch_add(1, std::memory_order_relaxed);
    }
}

std::vector<IndexVersionStore::Entry> IndexVersionStore::snapshot(
    int index_id) const {
    const Shard &shard = shards_[shard_index(index_id)];
    std::lock_guard<std::mutex> lock(shard.latch);
    auto found = shard.by_index.find(index_id);
    return found == shard.by_index.end() ? std::vector<Entry>{}
                                         : found->second;
}

bool IndexVersionStore::conflicts_with_snapshot(
    int index_id, const std::vector<char> &key, const Rid &target_rid,
    txn_id_t txn_id, timestamp_t start_ts) const {
    if (entry_count_.load(std::memory_order_relaxed) == 0) {
        return false;
    }
    const Shard &shard = shards_[shard_index(index_id)];
    std::lock_guard<std::mutex> lock(shard.latch);
    auto found = shard.by_index.find(index_id);
    if (found == shard.by_index.end()) {
        return false;
    }
    return std::any_of(found->second.begin(), found->second.end(),
                       [&](const Entry &entry) {
                           if (entry.old_key != key ||
                               (entry.rid == target_rid) ||
                               entry.owner == txn_id) {
                               return false;
                           }
                           return entry.valid_until == INVALID_TS ||
                                  start_ts < entry.valid_until;
                       });
}

void IndexVersionStore::finalize(
    const std::shared_ptr<TxnControl> &owner, timestamp_t commit_ts) {
    if (owner == nullptr) {
        return;
    }
    for (Shard &shard : shards_) {
        std::lock_guard<std::mutex> lock(shard.latch);
        for (auto &[index_id, entries] : shard.by_index) {
            static_cast<void>(index_id);
            for (Entry &entry : entries) {
                if (entry.owner_control == owner &&
                    entry.valid_until == INVALID_TS) {
                    entry.valid_until = commit_ts;
                }
            }
        }
    }
}

void IndexVersionStore::discard(const std::shared_ptr<TxnControl> &owner) {
    if (owner == nullptr) {
        return;
    }
    size_t removed = 0;
    for (Shard &shard : shards_) {
        std::lock_guard<std::mutex> lock(shard.latch);
        for (auto index = shard.by_index.begin();
             index != shard.by_index.end();) {
            auto &entries = index->second;
            const size_t old_size = entries.size();
            entries.erase(
                std::remove_if(entries.begin(), entries.end(),
                               [&](const Entry &entry) {
                                   return entry.owner_control == owner &&
                                          entry.valid_until == INVALID_TS;
                               }),
                entries.end());
            removed += old_size - entries.size();
            if (entries.empty()) {
                index = shard.by_index.erase(index);
            } else {
                ++index;
            }
        }
    }
    entry_count_.fetch_sub(removed, std::memory_order_relaxed);
}

void IndexVersionStore::garbage_collect(timestamp_t watermark) {
    if (entry_count_.load(std::memory_order_relaxed) == 0) {
        return;
    }
    size_t removed = 0;
    for (Shard &shard : shards_) {
        std::lock_guard<std::mutex> lock(shard.latch);
        for (auto index = shard.by_index.begin();
             index != shard.by_index.end();) {
            auto &entries = index->second;
            const size_t old_size = entries.size();
            entries.erase(
                std::remove_if(entries.begin(), entries.end(),
                               [&](const Entry &entry) {
                                   return entry.valid_until != INVALID_TS &&
                                          entry.valid_until <= watermark;
                               }),
                entries.end());
            removed += old_size - entries.size();
            if (entries.empty()) {
                index = shard.by_index.erase(index);
            } else {
                ++index;
            }
        }
    }
    entry_count_.fetch_sub(removed, std::memory_order_relaxed);
}
