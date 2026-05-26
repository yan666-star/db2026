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
        std::lock_guard<std::mutex> lock(latch_);
        txn_map[txn_id] = txn;
    }
    txn->set_state(TransactionState::GROWING);
    return txn;
}

void TransactionManager::commit(Transaction *txn, LogManager *log_manager) {
    if (txn == nullptr) {
        return;
    }

    for (const auto &lock_id : *txn->get_lock_set()) {
        lock_manager_->unlock(txn, lock_id);
    }
    txn->get_lock_set()->clear();
    clear_write_set(txn);
    txn->set_state(TransactionState::COMMITTED);
}

void TransactionManager::abort(Transaction *txn, LogManager *log_manager) {
    if (txn == nullptr) {
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

    for (const auto &lock_id : *txn->get_lock_set()) {
        lock_manager_->unlock(txn, lock_id);
    }
    txn->get_lock_set()->clear();
    txn->set_state(TransactionState::ABORTED);
}
