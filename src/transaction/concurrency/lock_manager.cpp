/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "lock_manager.h"

#include <algorithm>
#include <chrono>

namespace {
constexpr auto kLockWaitTimeout = std::chrono::seconds(5);
}

bool LockManager::is_stronger_or_equal(LockMode held, LockMode requested) const {
    if (held == requested) {
        return true;
    }
    if (held == LockMode::EXLUCSIVE) {
        return true;
    }
    if (held == LockMode::S_IX) {
        return requested == LockMode::SHARED ||
               requested == LockMode::INTENTION_SHARED ||
               requested == LockMode::INTENTION_EXCLUSIVE;
    }
    if (held == LockMode::SHARED) {
        return requested == LockMode::INTENTION_SHARED;
    }
    if (held == LockMode::INTENTION_EXCLUSIVE) {
        return requested == LockMode::INTENTION_SHARED;
    }
    return false;
}

bool LockManager::compatible(const LockRequestQueue &request_queue,
                             txn_id_t txn_id, LockMode lock_mode) const {
    for (const auto &request : request_queue.request_queue_) {
        if (!request.granted_ || request.txn_id_ == txn_id) {
            continue;
        }
        switch (lock_mode) {
            case LockMode::INTENTION_SHARED:
                if (request.lock_mode_ == LockMode::EXLUCSIVE) {
                    return false;
                }
                break;
            case LockMode::INTENTION_EXCLUSIVE:
                if (request.lock_mode_ == LockMode::SHARED ||
                    request.lock_mode_ == LockMode::EXLUCSIVE ||
                    request.lock_mode_ == LockMode::S_IX) {
                    return false;
                }
                break;
            case LockMode::SHARED:
                if (request.lock_mode_ == LockMode::INTENTION_EXCLUSIVE ||
                    request.lock_mode_ == LockMode::EXLUCSIVE ||
                    request.lock_mode_ == LockMode::S_IX) {
                    return false;
                }
                break;
            case LockMode::S_IX:
                if (request.lock_mode_ != LockMode::INTENTION_SHARED) {
                    return false;
                }
                break;
            case LockMode::EXLUCSIVE:
                return false;
        }
    }
    return true;
}

void LockManager::recompute_group_lock_mode(LockRequestQueue &request_queue) {
    bool has_s = false;
    bool has_x = false;
    bool has_is = false;
    bool has_ix = false;
    bool has_six = false;
    for (const auto &request : request_queue.request_queue_) {
        if (!request.granted_) {
            continue;
        }
        switch (request.lock_mode_) {
            case LockMode::SHARED:
                has_s = true;
                break;
            case LockMode::EXLUCSIVE:
                has_x = true;
                break;
            case LockMode::INTENTION_SHARED:
                has_is = true;
                break;
            case LockMode::INTENTION_EXCLUSIVE:
                has_ix = true;
                break;
            case LockMode::S_IX:
                has_six = true;
                break;
        }
    }

    if (has_x) {
        request_queue.group_lock_mode_ = GroupLockMode::X;
    } else if (has_six || (has_s && has_ix)) {
        request_queue.group_lock_mode_ = GroupLockMode::SIX;
    } else if (has_s) {
        request_queue.group_lock_mode_ = GroupLockMode::S;
    } else if (has_ix) {
        request_queue.group_lock_mode_ = GroupLockMode::IX;
    } else if (has_is) {
        request_queue.group_lock_mode_ = GroupLockMode::IS;
    } else {
        request_queue.group_lock_mode_ = GroupLockMode::NON_LOCK;
    }
}

bool LockManager::lock(Transaction *txn, const LockDataId &lock_data_id,
                       LockMode lock_mode) {
    if (txn == nullptr) {
        return true;
    }
    if (txn->get_state() == TransactionState::COMMITTED ||
        txn->get_state() == TransactionState::ABORTED) {
        return false;
    }
    if (txn->get_state() == TransactionState::SHRINKING) {
        throw TransactionAbortException(txn->get_transaction_id(),
                                        AbortReason::LOCK_ON_SHIRINKING);
    }
    if (txn->get_state() == TransactionState::DEFAULT) {
        txn->set_state(TransactionState::GROWING);
    }

    std::unique_lock<std::mutex> guard(latch_);
    auto &request_queue = lock_table_[lock_data_id];
    const txn_id_t txn_id = txn->get_transaction_id();

    auto existing = std::find_if(
        request_queue.request_queue_.begin(), request_queue.request_queue_.end(),
        [txn_id](const LockRequest &request) {
            return request.txn_id_ == txn_id;
        });

    if (existing != request_queue.request_queue_.end()) {
        if (existing->granted_ && is_stronger_or_equal(existing->lock_mode_, lock_mode)) {
            return true;
        }
        const LockMode old_mode = existing->lock_mode_;
        const bool old_granted = existing->granted_;
        const auto deadline = std::chrono::steady_clock::now() + kLockWaitTimeout;
        const bool granted = request_queue.cv_.wait_until(guard, deadline, [&] {
            return compatible(request_queue, txn_id, lock_mode);
        });
        if (!granted) {
            existing->lock_mode_ = old_mode;
            existing->granted_ = old_granted;
            recompute_group_lock_mode(request_queue);
            request_queue.cv_.notify_all();
            throw TransactionAbortException(txn_id, AbortReason::DEADLOCK_PREVENTION);
        }
        existing->lock_mode_ = lock_mode;
        existing->granted_ = true;
        recompute_group_lock_mode(request_queue);
        txn->get_lock_set()->insert(lock_data_id);
        return true;
    }

    request_queue.request_queue_.emplace_back(txn_id, lock_mode);
    auto request_it = std::prev(request_queue.request_queue_.end());
    const auto deadline = std::chrono::steady_clock::now() + kLockWaitTimeout;
    const bool granted = request_queue.cv_.wait_until(guard, deadline, [&] {
        return compatible(request_queue, txn_id, lock_mode);
    });
    if (!granted) {
        request_queue.request_queue_.erase(request_it);
        recompute_group_lock_mode(request_queue);
        request_queue.cv_.notify_all();
        if (request_queue.request_queue_.empty()) {
            lock_table_.erase(lock_data_id);
        }
        throw TransactionAbortException(txn_id, AbortReason::DEADLOCK_PREVENTION);
    }

    request_it->granted_ = true;
    recompute_group_lock_mode(request_queue);
    txn->get_lock_set()->insert(lock_data_id);
    return true;
}

bool LockManager::lock_shared_on_record(Transaction* txn, const Rid& rid, int tab_fd) {
    return lock(txn, LockDataId(tab_fd, rid, LockDataType::RECORD), LockMode::SHARED);
}

bool LockManager::lock_exclusive_on_record(Transaction* txn, const Rid& rid, int tab_fd) {
    return lock(txn, LockDataId(tab_fd, rid, LockDataType::RECORD), LockMode::EXLUCSIVE);
}

bool LockManager::lock_shared_on_table(Transaction* txn, int tab_fd) {
    return lock(txn, LockDataId(tab_fd, LockDataType::TABLE), LockMode::SHARED);
}

bool LockManager::lock_exclusive_on_table(Transaction* txn, int tab_fd) {
    return lock(txn, LockDataId(tab_fd, LockDataType::TABLE), LockMode::EXLUCSIVE);
}

bool LockManager::lock_IS_on_table(Transaction* txn, int tab_fd) {
    return lock(txn, LockDataId(tab_fd, LockDataType::TABLE), LockMode::INTENTION_SHARED);
}

bool LockManager::lock_IX_on_table(Transaction* txn, int tab_fd) {
    return lock(txn, LockDataId(tab_fd, LockDataType::TABLE), LockMode::INTENTION_EXCLUSIVE);
}

bool LockManager::unlock(Transaction* txn, LockDataId lock_data_id) {
    if (txn == nullptr) {
        return true;
    }

    std::unique_lock<std::mutex> guard(latch_);
    auto table_it = lock_table_.find(lock_data_id);
    if (table_it == lock_table_.end()) {
        txn->get_lock_set()->erase(lock_data_id);
        return true;
    }

    auto &request_queue = table_it->second;
    const txn_id_t txn_id = txn->get_transaction_id();
    auto request_it = std::find_if(
        request_queue.request_queue_.begin(), request_queue.request_queue_.end(),
        [txn_id](const LockRequest &request) {
            return request.txn_id_ == txn_id;
        });
    if (request_it == request_queue.request_queue_.end()) {
        txn->get_lock_set()->erase(lock_data_id);
        return true;
    }

    request_queue.request_queue_.erase(request_it);
    txn->get_lock_set()->erase(lock_data_id);
    recompute_group_lock_mode(request_queue);
    request_queue.cv_.notify_all();
    if (request_queue.request_queue_.empty()) {
        lock_table_.erase(table_it);
    }
    return true;
}
