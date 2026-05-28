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

class SortExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    SortPlan *plan_ = nullptr;
    ColMeta cols_;                              // 框架中只支持一个键排序，需要自行修改数据结构支持多个键排序
    std::vector<std::unique_ptr<RmRecord>> tuples_;
    size_t cursor_ = 0;
    bool is_desc_;

   public:
    SortExecutor(std::unique_ptr<AbstractExecutor> prev, TabCol sel_cols, bool is_desc, SortPlan *plan = nullptr) {
        prev_ = std::move(prev);
        plan_ = plan;
        cols_ = prev_->get_col_offset(sel_cols);
        is_desc_ = is_desc;
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
        std::sort(tuples_.begin(), tuples_.end(), [&](const std::unique_ptr<RmRecord> &a, const std::unique_ptr<RmRecord> &b) {
            int cmp = compare_col_value(a->data + cols_.offset, b->data + cols_.offset, cols_.type, cols_.len);
            return is_desc_ ? (cmp > 0) : (cmp < 0);
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