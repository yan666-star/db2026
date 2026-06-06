/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "transaction_manager.h"

#include <algorithm>

#include "common/context.h"
#include "record/rm_file_handle.h"
#include "system/sm_manager.h"

std::unordered_map<txn_id_t, Transaction *> TransactionManager::txn_map = {};

static void clear_write_set(Transaction *txn) {
    auto write_set = txn->get_write_set();
    for (auto it = write_set->begin(); it != write_set->end();) {
        delete *it;
        it = write_set->erase(it);
    }
}

Transaction *TransactionManager::begin(Transaction *txn, LogManager *log_manager) {
    if (txn == nullptr) {
        txn_id_t txn_id = next_txn_id_.fetch_add(1);
        txn = new Transaction(txn_id);
        {
            std::lock_guard<std::mutex> lock(latch_);
            txn_map[txn_id] = txn;
        }
        {
            std::lock_guard<std::mutex> lock(checkpoint_latch_);
            active_txns_.insert(txn_id);
        }
        if (log_manager != nullptr) {
            BeginLogRecord begin_log(txn_id);
            lsn_t lsn = log_manager->add_log_to_buffer(&begin_log);
            txn->set_prev_lsn(lsn);
        }
    }
    txn->set_state(TransactionState::GROWING);
    return txn;
}

void TransactionManager::commit(Transaction *txn, LogManager *log_manager) {
    if (txn == nullptr || txn->get_state() == TransactionState::COMMITTED ||
        txn->get_state() == TransactionState::ABORTED) {
        return;
    }

    if (log_manager != nullptr) {
        CommitLogRecord commit_log(txn->get_transaction_id());
        commit_log.prev_lsn_ = txn->get_prev_lsn();
        lsn_t lsn = log_manager->add_log_to_buffer(&commit_log);
        txn->set_prev_lsn(lsn);
        log_manager->flush_log_to_disk();
    }

    for (const auto &lock_id : *txn->get_lock_set()) {
        lock_manager_->unlock(txn, lock_id);
    }
    txn->get_lock_set()->clear();
    clear_write_set(txn);
    txn->set_state(TransactionState::COMMITTED);
    finish_transaction(txn);
}

void TransactionManager::abort(Transaction *txn, LogManager *log_manager) {
    if (txn == nullptr || txn->get_state() == TransactionState::COMMITTED ||
        txn->get_state() == TransactionState::ABORTED) {
        return;
    }

    Context context(lock_manager_, log_manager, txn);
    auto write_set = txn->get_write_set();
    while (!write_set->empty()) {
        WriteRecord *write_record = write_set->back();
        sm_manager_->rollback(write_record, &context);
        write_set->pop_back();
        delete write_record;
    }

    if (log_manager != nullptr) {
        // Rollback operations are not represented by compensation log
        // records in this framework. Make the restored table/index state
        // durable before the ABORT record says recovery may skip this txn.
        sm_manager_->flush_for_checkpoint();
        AbortLogRecord abort_log(txn->get_transaction_id());
        abort_log.prev_lsn_ = txn->get_prev_lsn();
        lsn_t lsn = log_manager->add_log_to_buffer(&abort_log);
        txn->set_prev_lsn(lsn);
        log_manager->flush_log_to_disk();
    }

    for (const auto &lock_id : *txn->get_lock_set()) {
        lock_manager_->unlock(txn, lock_id);
    }
    txn->get_lock_set()->clear();
    txn->set_state(TransactionState::ABORTED);
    finish_transaction(txn);
}

void TransactionManager::finish_transaction(Transaction *txn) {
    {
        std::lock_guard<std::mutex> lock(checkpoint_latch_);
        active_txns_.erase(txn->get_transaction_id());
    }
    checkpoint_cv_.notify_all();
}

void TransactionManager::release_transaction(Transaction *txn) {
    if (txn == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(latch_);
        auto it = txn_map.find(txn->get_transaction_id());
        if (it != txn_map.end() && it->second == txn) {
            txn_map.erase(it);
        }
    }
    delete txn;
}

void TransactionManager::enter_statement(txn_id_t txn_id) {
    std::unique_lock<std::mutex> lock(checkpoint_latch_);
    checkpoint_cv_.wait(lock, [this, txn_id] {
        return !checkpoint_in_progress_ ||
               (txn_id != INVALID_TXN_ID &&
                active_txns_.find(txn_id) != active_txns_.end());
    });
    active_statements_++;
}

void TransactionManager::leave_statement() {
    {
        std::lock_guard<std::mutex> lock(checkpoint_latch_);
        if (active_statements_ > 0) {
            active_statements_--;
        }
    }
    checkpoint_cv_.notify_all();
}

std::vector<txn_id_t> TransactionManager::begin_static_checkpoint() {
    checkpoint_serial_latch_.lock();
    std::unique_lock<std::mutex> lock(checkpoint_latch_);
    checkpoint_in_progress_ = true;
    checkpoint_cv_.wait(lock, [this] {
        return active_statements_ == 0 && active_txns_.empty();
    });

    try {
        return {};
    } catch (...) {
        checkpoint_in_progress_ = false;
        lock.unlock();
        checkpoint_cv_.notify_all();
        checkpoint_serial_latch_.unlock();
        throw;
    }
}

void TransactionManager::end_static_checkpoint() {
    {
        std::lock_guard<std::mutex> lock(checkpoint_latch_);
        checkpoint_in_progress_ = false;
    }
    checkpoint_cv_.notify_all();
    checkpoint_serial_latch_.unlock();
}
