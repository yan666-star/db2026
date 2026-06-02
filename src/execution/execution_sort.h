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
#include <algorithm>
#include <vector>
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "execution_eval.h"
#include "index/ix.h"
#include "system/sm.h"
#include "optimizer/plan.h"

class SortExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    SortPlan *plan_ = nullptr;
    std::vector<ColMeta> sort_cols_;
    std::vector<bool> is_descs_;
    std::vector<std::unique_ptr<RmRecord>> tuples_;
    size_t cursor_ = 0;

    static int compare_string_col(const char *a, const char *b, int col_len) {
        std::string sa(a, col_len);
        std::string sb(b, col_len);
        size_t null_pos_a = sa.find('\0');
        if (null_pos_a != std::string::npos) {
            sa.erase(null_pos_a);
        }
        size_t null_pos_b = sb.find('\0');
        if (null_pos_b != std::string::npos) {
            sb.erase(null_pos_b);
        }
        while (!sa.empty() && sa.back() == ' ') {
            sa.pop_back();
        }
        while (!sb.empty() && sb.back() == ' ') {
            sb.pop_back();
        }
        if (sa < sb) {
            return -1;
        }
        if (sa > sb) {
            return 1;
        }
        return 0;
    }

    int compare_records(const RmRecord &a, const RmRecord &b) const {
        for (size_t i = 0; i < sort_cols_.size(); i++) {
            const auto &col = sort_cols_[i];
            int cmp;
            if (col.type == TYPE_STRING) {
                cmp = compare_string_col(a.data + col.offset, b.data + col.offset, col.len);
            } else {
                cmp = compare_col_value(a.data + col.offset, b.data + col.offset, col.type, col.len);
            }
            if (cmp != 0) {
                return is_descs_[i] ? -cmp : cmp;
            }
        }
        return compare_record_by_cols(a, b, prev_->cols());
    }

   public:
    SortExecutor(std::unique_ptr<AbstractExecutor> prev, TabCol sel_cols, bool is_desc, SortPlan *plan = nullptr) {
        prev_ = std::move(prev);
        plan_ = plan;
        sort_cols_.push_back(prev_->get_col_offset(sel_cols));
        is_descs_.push_back(is_desc);
    }

    SortExecutor(std::unique_ptr<AbstractExecutor> prev, SortPlan *plan)
        : prev_(std::move(prev)), plan_(plan) {
        for (size_t i = 0; i < plan_->sort_cols_.size(); i++) {
            sort_cols_.push_back(prev_->get_col_offset(plan_->sort_cols_[i]));
            is_descs_.push_back(plan_->is_descs_[i]);
        }
    }

    void beginTuple() override {
        tuples_.clear();
        cursor_ = 0;
        prev_->beginTuple();
        for (; !prev_->is_end(); prev_->nextTuple()) {
            auto rec = prev_->Next();
            if (rec != nullptr) {
                tuples_.push_back(std::move(rec));
            }
        }
        std::stable_sort(tuples_.begin(), tuples_.end(), [&](const std::unique_ptr<RmRecord> &a, const std::unique_ptr<RmRecord> &b) {
            return compare_records(*a, *b) < 0;
        });
        if (plan_ != nullptr) {
            plan_->rows_ = tuples_.size();
        }
    }

    void nextTuple() override {
        if (!is_end()) {
            cursor_++;
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) {
            return nullptr;
        }
        return std::make_unique<RmRecord>(*tuples_[cursor_]);
    }

    bool is_end() const override { return cursor_ >= tuples_.size(); }

    size_t tupleLen() const override { return prev_->tupleLen(); }

    const std::vector<ColMeta> &cols() const override { return prev_->cols(); }

    Rid &rid() override { return _abstract_rid; }
    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols(), target); }
};
