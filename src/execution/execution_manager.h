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

#include <atomic>
#include <cassert>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "execution_defs.h"
#include "record/rm.h"
#include "system/sm.h"
#include "common/context.h"
#include "common/common.h"
#include "optimizer/plan.h"
#include "executor_abstract.h"
#include "transaction/transaction_manager.h"
#include "optimizer/planner.h"

class Planner;

class QlManager {
   private:
    SmManager *sm_manager_;
    TransactionManager *txn_mgr_;
    Planner *planner_;
    std::mutex checkpoint_create_latch_;
    std::atomic<bool> checkpoint_available_{false};

    void create_static_checkpoint_internal(LogManager *log_manager);

   public:
    QlManager(SmManager *sm_manager, TransactionManager *txn_mgr, Planner *planner) 
        : sm_manager_(sm_manager),  txn_mgr_(txn_mgr), planner_(planner) {}

    void run_mutli_query(std::shared_ptr<Plan> plan, Context *context);
    void run_cmd_utility(std::shared_ptr<Plan> plan, txn_id_t *txn_id, Context *context);
    void select_from(std::unique_ptr<AbstractExecutor> executorTreeRoot, std::vector<TabCol> sel_cols,
                        Context *context);
    void explain_analyze(std::unique_ptr<AbstractExecutor> executorTreeRoot,
                     std::shared_ptr<Plan> plan,
                     Context *context);

    void create_static_checkpoint(LogManager *log_manager);
    void ensure_prepared_checkpoint(LogManager *log_manager);
    void set_checkpoint_available(bool available);

    void run_dml(std::unique_ptr<AbstractExecutor> exec);
};
