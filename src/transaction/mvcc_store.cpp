#include "transaction/mvcc_store.h"

#include <algorithm>
#include <cstring>
#include <set>

#include "transaction/storage_commit_executor.h"

namespace {

VisibleVersion physical_version(const RmRecord *physical) {
    VisibleVersion result;
    if (physical == nullptr) {
        return result;
    }
    result.exists = true;
    result.data.assign(physical->data, physical->data + physical->size);
    return result;
}

}  // namespace

bool MvccStore::owner_is_resolved(
    const std::shared_ptr<TxnControl> &owner) noexcept {
    if (owner == nullptr) {
        return true;
    }
    const TxnVisibilityState state =
        owner->state.load(std::memory_order_acquire);
    return state == TxnVisibilityState::VISIBLE ||
           state == TxnVisibilityState::ABORTED;
}

bool MvccStore::try_acquire_record_intent(
    const std::shared_ptr<TxnControl> &txn, RecordKey key) {
    if (txn == nullptr) {
        return false;
    }
    Shard &shard = shards_[shard_index(key)];
    {
        std::lock_guard<std::mutex> lock(shard.latch);
        auto owner = shard.record_owners.find(key);
        if (owner != shard.record_owners.end()) {
            if (owner->second == txn) {
                return true;
            }
            if (!owner_is_resolved(owner->second)) {
                return false;
            }
            owner->second = txn;
        } else {
            shard.record_owners.emplace(key, txn);
        }
    }
    txn->track_record_intent(key);
    return true;
}

bool MvccStore::try_acquire_unique_intent(
    const std::shared_ptr<TxnControl> &txn, int index_id,
    std::string_view binary_key) {
    if (txn == nullptr) {
        return false;
    }
    UniqueIntentKey key{index_id,
                        std::string(binary_key.data(), binary_key.size())};
    Shard &shard = shards_[shard_index(key)];
    {
        std::lock_guard<std::mutex> lock(shard.latch);
        auto owner = shard.unique_owners.find(key);
        if (owner != shard.unique_owners.end()) {
            if (owner->second == txn) {
                return true;
            }
            if (!owner_is_resolved(owner->second)) {
                return false;
            }
            owner->second = txn;
        } else {
            shard.unique_owners.emplace(key, txn);
        }
    }
    txn->track_unique_intent(index_id, key.key);
    return true;
}

VisibleVersion MvccStore::resolve(
    const std::shared_ptr<TxnControl> &reader, RecordKey key,
    const RmRecord *physical) const {
    const Shard &shard = shards_[shard_index(key)];
    std::lock_guard<std::mutex> lock(shard.latch);
    auto found = shard.record_versions.find(key);
    if (found == shard.record_versions.end()) {
        return physical_version(physical);
    }

    const timestamp_t read_ts = reader == nullptr ? INVALID_TS : reader->start_ts;
    const Version *visible = nullptr;
    for (const Version &version : found->second) {
        if (reader != nullptr && version.owner_control == reader &&
            version.commit_ts == INVALID_TS) {
            return VisibleVersion{!version.deleted, version.deleted,
                                  version.data};
        }
        timestamp_t commit_ts = version.commit_ts;
        if (version.owner_control != nullptr) {
            const TxnVisibilityState state =
                version.owner_control->state.load(std::memory_order_acquire);
            if (state != TxnVisibilityState::VISIBLE) {
                continue;
            }
            commit_ts = version.owner_control->commit_ts.load(
                std::memory_order_relaxed);
        }
        if (commit_ts != INVALID_TS && commit_ts <= read_ts &&
            (visible == nullptr || commit_ts > visible->commit_ts)) {
            visible = &version;
        }
    }
    if (visible == nullptr || visible->deleted) {
        return VisibleVersion{};
    }
    return VisibleVersion{true, false, visible->data};
}

VisibleVersion MvccStore::resolve_latest(RecordKey key,
                                         const RmRecord *physical) const {
    const Shard &shard = shards_[shard_index(key)];
    std::lock_guard<std::mutex> lock(shard.latch);
    auto found = shard.record_versions.find(key);
    if (found == shard.record_versions.end()) {
        return physical_version(physical);
    }

    const Version *latest = nullptr;
    const Version *pending = nullptr;
    timestamp_t latest_ts = INVALID_TS;
    bool pending_insert = false;
    for (const Version &version : found->second) {
        timestamp_t commit_ts = version.commit_ts;
        if (version.owner_control != nullptr) {
            const TxnVisibilityState state =
                version.owner_control->state.load(std::memory_order_acquire);
            if (state != TxnVisibilityState::VISIBLE) {
                if (state != TxnVisibilityState::ABORTED) {
                    pending = &version;
                }
                pending_insert = pending_insert ||
                                 (version.before_deleted && !version.deleted &&
                                  state != TxnVisibilityState::ABORTED);
                continue;
            }
            commit_ts = version.owner_control->commit_ts.load(
                std::memory_order_relaxed);
        }
        if (commit_ts != INVALID_TS &&
            (latest == nullptr || commit_ts > latest_ts)) {
            latest = &version;
            latest_ts = commit_ts;
        }
    }
    if (pending != nullptr) {
        return pending->before_deleted
                   ? VisibleVersion{}
                   : VisibleVersion{true, false, pending->before};
    }
    if ((latest != nullptr && latest->deleted) ||
        (latest == nullptr && pending_insert)) {
        return VisibleVersion{};
    }
    if (physical != nullptr) {
        return physical_version(physical);
    }
    return latest == nullptr ? VisibleVersion{}
                             : VisibleVersion{true, false, latest->data};
}

void MvccStore::install_pending(
    const std::shared_ptr<TxnControl> &txn,
    const PreparedStorageCommit &commit) {
    // StorageCommitExecutor calls TransactionManager::prepare_* for every
    // resolved write. Those calls install/merge the pending version under the
    // owning shard; this interface is the commit-batch seam and intentionally
    // does not duplicate them.
    static_cast<void>(txn);
    static_cast<void>(commit);
}

void MvccStore::release_intents(
    const std::shared_ptr<TxnControl> &txn) noexcept {
    if (txn == nullptr) {
        return;
    }
    for (const RecordKey &key : txn->record_intents_snapshot()) {
        Shard &shard = shards_[shard_index(key)];
        std::lock_guard<std::mutex> lock(shard.latch);
        auto owner = shard.record_owners.find(key);
        if (owner != shard.record_owners.end() && owner->second == txn) {
            shard.record_owners.erase(owner);
        }
    }
    for (const auto &[index_id, binary_key] :
         txn->unique_intents_snapshot()) {
        UniqueIntentKey key{index_id, binary_key};
        Shard &shard = shards_[shard_index(key)];
        std::lock_guard<std::mutex> lock(shard.latch);
        auto owner = shard.unique_owners.find(key);
        if (owner != shard.unique_owners.end() && owner->second == txn) {
            shard.unique_owners.erase(owner);
        }
    }
    txn->clear_intents();
}

void MvccStore::publish(const std::shared_ptr<TxnControl> &txn,
                        timestamp_t commit_ts) {
    if (txn == nullptr) {
        return;
    }
    for (const RecordKey &key : txn->record_intents_snapshot()) {
        Shard &shard = shards_[shard_index(key)];
        std::lock_guard<std::mutex> lock(shard.latch);
        auto history = shard.record_versions.find(key);
        if (history == shard.record_versions.end()) {
            continue;
        }
        for (Version &version : history->second) {
            if (version.owner_control == txn &&
                version.commit_ts == INVALID_TS) {
                version.commit_ts = commit_ts;
            }
        }
    }
    txn->commit_ts.store(commit_ts, std::memory_order_relaxed);
    txn->state.store(TxnVisibilityState::DURABLE, std::memory_order_relaxed);
    txn->state.store(TxnVisibilityState::VISIBLE, std::memory_order_release);
    release_intents(txn);
}

void MvccStore::abort(const std::shared_ptr<TxnControl> &txn) noexcept {
    if (txn == nullptr) {
        return;
    }
    txn->state.store(TxnVisibilityState::ABORTED, std::memory_order_release);
    for (const RecordKey &key : txn->record_intents_snapshot()) {
        Shard &shard = shards_[shard_index(key)];
        std::lock_guard<std::mutex> lock(shard.latch);
        auto history = shard.record_versions.find(key);
        if (history == shard.record_versions.end()) {
            continue;
        }
        auto &versions = history->second;
        versions.erase(
            std::remove_if(versions.begin(), versions.end(),
                           [&](const Version &version) {
                               return version.owner_control == txn;
                           }),
            versions.end());
        if (versions.empty()) {
            shard.record_versions.erase(history);
        }
    }
    release_intents(txn);
    txn->cleanup_done.store(true, std::memory_order_release);
}
