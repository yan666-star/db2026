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
#include "execution_defs.h"
#include "execution_eval.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"
#include "optimizer/plan.h"
class NestedLoopJoinExecutor : public AbstractExecutor {
   private:
    JoinPlan *plan_ = nullptr;
    std::unique_ptr<AbstractExecutor> left_;
    std::unique_ptr<AbstractExecutor> right_;
    size_t len_;
    std::vector<ColMeta> cols_;

    std::vector<Condition> fed_conds_;
    std::unique_ptr<RmRecord> current_rec_;
    bool is_end_ = true;

    static const ColMeta *find_exact_col(const std::vector<ColMeta> &cols, const TabCol &target) {
        auto it = std::find_if(cols.begin(), cols.end(), [&](const ColMeta &col) {
            return col.tab_name == target.tab_name && col.name == target.col_name;
        });
        return it == cols.end() ? nullptr : &*it;
    }

    bool prepare_index_lookup() {
        auto left_rec = left_->Next();
        if (left_rec == nullptr) {
            return false;
        }
        const auto &left_cols = left_->cols();
        const auto &right_cols = right_->cols();
        for (const auto &cond : fed_conds_) {
            if (cond.is_rhs_val || cond.op != OP_EQ) {
                continue;
            }
            const TabCol *left_col = nullptr;
            const TabCol *right_col = nullptr;
            if (find_exact_col(left_cols, cond.lhs_col) != nullptr &&
                find_exact_col(right_cols, cond.rhs_col) != nullptr) {
                left_col = &cond.lhs_col;
                right_col = &cond.rhs_col;
            } else if (find_exact_col(left_cols, cond.rhs_col) != nullptr &&
                       find_exact_col(right_cols, cond.lhs_col) != nullptr) {
                left_col = &cond.rhs_col;
                right_col = &cond.lhs_col;
            }
            if (left_col == nullptr) {
                continue;
            }
            const ColMeta *outer_meta = find_exact_col(left_cols, *left_col);
            if (right_->set_index_lookup(
                    *right_col,
                    left_rec->data + outer_meta->offset,
                    outer_meta->type,
                    outer_meta->len)) {
                return true;
            }
        }
        return false;
    }

    std::unique_ptr<RmRecord> join_records(const RmRecord &left_rec, const RmRecord &right_rec) {
        auto joined = std::make_unique<RmRecord>(len_);
        memcpy(joined->data, left_rec.data, left_->tupleLen());
        memcpy(joined->data + left_->tupleLen(), right_rec.data, right_->tupleLen());
        return joined;
    }

    void find_match() {
        while (!left_->is_end()) {
            while (!right_->is_end()) {
                auto left_rec = left_->Next();
                auto right_rec = right_->Next();
                auto joined = join_records(*left_rec, *right_rec);
                if (fed_conds_.empty() || eval_conditions(*joined, fed_conds_, cols_)) {
                    if (plan_ != nullptr) {
                        plan_->rows_++;
                    }
                    current_rec_ = std::move(joined);
                    is_end_ = false;
                    return;
                }
                right_->nextTuple();
            }
            left_->nextTuple();
            if (left_->is_end()) {
                break;
            }
            prepare_index_lookup();
            right_->beginTuple();
        }
        current_rec_.reset();
        is_end_ = true;
    }

   public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right,
                           std::vector<Condition> conds, JoinPlan *plan = nullptr) {
        left_ = std::move(left);
        right_ = std::move(right);
        plan_ = plan;
        len_ = left_->tupleLen() + right_->tupleLen();
        cols_ = left_->cols();
        auto right_cols = right_->cols();
        for (auto &col : right_cols) {
            col.offset += left_->tupleLen();
        }

        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());
        fed_conds_ = std::move(conds);
    }

    size_t tupleLen() const override { return len_; }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    bool is_end() const override { return is_end_; }

    void beginTuple() override {
        left_->beginTuple();
        if (left_->is_end()) {
            is_end_ = true;
            return;
        }
        prepare_index_lookup();
        right_->beginTuple();
        find_match();
    }

    void nextTuple() override {
        if (is_end_) {
            return;
        }
        right_->nextTuple();
        find_match();
    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end_ || current_rec_ == nullptr) {
            return nullptr;
        }
        return std::make_unique<RmRecord>(*current_rec_);
    }

    Rid &rid() override { return _abstract_rid; }
    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols_, target); }
};
