#include "transaction/txn_registry.h"

#include <algorithm>

void TxnControl::track_record_intent(RecordKey key) {
    std::lock_guard<std::mutex> lock(intents_latch_);
    record_intents_.insert(key);
}

void TxnControl::track_unique_intent(int index_id, std::string binary_key) {
    std::lock_guard<std::mutex> lock(intents_latch_);
    const auto duplicate = std::find_if(
        unique_intents_.begin(), unique_intents_.end(),
        [&](const auto &entry) {
            return entry.first == index_id && entry.second == binary_key;
        });
    if (duplicate == unique_intents_.end()) {
        unique_intents_.emplace_back(index_id, std::move(binary_key));
    }
}

std::vector<RecordKey> TxnControl::record_intents_snapshot() const {
    std::lock_guard<std::mutex> lock(intents_latch_);
    return {record_intents_.begin(), record_intents_.end()};
}

std::vector<std::pair<int, std::string>>
TxnControl::unique_intents_snapshot() const {
    std::lock_guard<std::mutex> lock(intents_latch_);
    return unique_intents_;
}

void TxnControl::clear_intents() {
    std::lock_guard<std::mutex> lock(intents_latch_);
    record_intents_.clear();
    unique_intents_.clear();
}

void TxnRegistry::attach(const std::shared_ptr<TxnControl> &control) {
    if (control == nullptr) {
        return;
    }
    std::unique_lock<std::shared_mutex> lock(latch_);
    controls_[control->id] = control;
}

std::shared_ptr<TxnControl> TxnRegistry::find(txn_id_t txn_id) const {
    std::shared_lock<std::shared_mutex> lock(latch_);
    auto found = controls_.find(txn_id);
    return found == controls_.end() ? nullptr : found->second;
}

void TxnRegistry::release(txn_id_t txn_id) {
    std::unique_lock<std::shared_mutex> lock(latch_);
    controls_.erase(txn_id);
}

std::vector<std::shared_ptr<TxnControl>> TxnRegistry::snapshot() const {
    std::shared_lock<std::shared_mutex> lock(latch_);
    std::vector<std::shared_ptr<TxnControl>> result;
    result.reserve(controls_.size());
    for (const auto &[txn_id, control] : controls_) {
        static_cast<void>(txn_id);
        result.push_back(control);
    }
    return result;
}

timestamp_t TxnRegistry::current_commit_ts() const noexcept {
    return commit_clock_.load(std::memory_order_acquire);
}

timestamp_t TxnRegistry::next_commit_ts() noexcept {
    return commit_clock_.fetch_add(1, std::memory_order_acq_rel) + 1;
}

void TxnRegistry::advance_commit_ts(timestamp_t timestamp) noexcept {
    timestamp_t current = commit_clock_.load(std::memory_order_relaxed);
    while (current < timestamp &&
           !commit_clock_.compare_exchange_weak(
               current, timestamp, std::memory_order_release,
               std::memory_order_relaxed)) {
    }
}
