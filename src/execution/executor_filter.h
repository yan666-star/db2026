/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the
Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <memory>
#include <vector>

#include "execution_eval.h"
#include "executor_abstract.h"
#include "optimizer/plan.h"

class FilterExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    std::vector<Condition> conds_;
    FilterPlan *plan_ = nullptr;
    std::unique_ptr<RmRecord> current_;
    bool is_end_ = true;

    void fetch_next() {
        current_.reset();
        while (!prev_->is_end()) {
            auto rec = prev_->Next();
            if (rec != nullptr && eval_conditions(*rec, conds_, prev_->cols())) {
                current_ = std::move(rec);
                is_end_ = false;
                if (plan_ != nullptr) {
                    plan_->rows_++;
                }
                return;
            }
            prev_->nextTuple();
        }
        is_end_ = true;
    }

   public:
    FilterExecutor(std::unique_ptr<AbstractExecutor> prev, std::vector<Condition> conds, FilterPlan *plan)
        : prev_(std::move(prev)), conds_(std::move(conds)), plan_(plan) {}

    void beginTuple() override {
        prev_->beginTuple();
        is_end_ = false;
        fetch_next();
    }

    void nextTuple() override {
        if (is_end_) {
            return;
        }
        prev_->nextTuple();
        fetch_next();
    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end_ || current_ == nullptr) {
            return nullptr;
        }
        return std::make_unique<RmRecord>(*current_);
    }

    bool is_end() const override { return is_end_; }

    size_t tupleLen() const override { return prev_->tupleLen(); }

    const std::vector<ColMeta> &cols() const override { return prev_->cols(); }

    Rid &rid() override { return _abstract_rid; }

    ColMeta get_col_offset(const TabCol &target) override { return prev_->get_col_offset(target); }
};
