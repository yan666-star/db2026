/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "transaction/transaction.h"
#include "transaction/concurrency/lock_manager.h"
#include "recovery/log_manager.h"

class TransactionManager;
namespace rmdb::execution {
class ResultSink;
}

class Context {
public:
    Context (LockManager *lock_mgr, LogManager *log_mgr,
            Transaction *txn, TransactionManager *txn_mgr = nullptr,
            IsolationLevel *session_isolation = nullptr,
            rmdb::execution::ResultSink *result_sink = nullptr)
        : lock_mgr_(lock_mgr), log_mgr_(log_mgr), txn_(txn),
          txn_mgr_(txn_mgr), session_isolation_(session_isolation),
          result_sink_(result_sink) {}

    LockManager *lock_mgr_;
    LogManager *log_mgr_;
    Transaction *txn_;
    TransactionManager *txn_mgr_;
    IsolationLevel *session_isolation_;
    rmdb::execution::ResultSink *result_sink_;
};
