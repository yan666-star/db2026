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
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"
#include "optimizer/plan.h"
#include <limits>

class ProjectionExecutor : public AbstractExecutor {
   private:
    ProjectionPlan *plan_ = nullptr;//添加projection plan显示表示
    std::unique_ptr<AbstractExecutor> prev_;
    std::vector<ColMeta> cols_;
    size_t len_;
    std::vector<size_t> sel_idxs_;
    int limit_;
    int result_idx_ = 0;
    bool is_sel_all_ = false;

   public:
    ProjectionExecutor(std::unique_ptr<AbstractExecutor> prev,
                   const std::vector<TabCol> &sel_cols,
                   ProjectionPlan *plan = nullptr) {
        prev_ = std::move(prev);
        plan_ = plan;
        limit_ = (plan_ != nullptr) ? plan_->limit_num_ : -1;
        if (limit_ == -1) {
            limit_ = std::numeric_limits<int>::max();
        }

        size_t curr_offset = 0;
        auto &prev_cols = prev_->cols();
        for (auto &sel_col : sel_cols) {
            auto pos = get_col(prev_cols, sel_col);
            sel_idxs_.push_back(pos - prev_cols.begin());
            auto col = *pos;
            col.name = sel_col.col_name;
            col.tab_name = sel_col.tab_name;
            col.offset = curr_offset;
            curr_offset += col.len;
            cols_.push_back(col);
        }
        len_ = curr_offset;

        is_sel_all_ = true;
        if (sel_idxs_.size() != prev_cols.size()) {
            is_sel_all_ = false;
        } else {
            for (size_t i = 0; i < sel_idxs_.size(); i++) {
                if (sel_idxs_[i] != i) {
                    is_sel_all_ = false;
                    break;
                }
            }
        }
    }

    size_t tupleLen() const override { return len_; }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    bool is_end() const override {
        return prev_->is_end() || result_idx_ >= limit_;
    }

    void beginTuple() override {
        prev_->beginTuple();
        result_idx_ = 0;
        if (limit_ == 0) {
            return;
        }
        if (!prev_->is_end() && plan_ != nullptr) {
            plan_->rows_++;
        }
    }

    void nextTuple() override {
        prev_->nextTuple();
        result_idx_++;
        if (!prev_->is_end() && plan_ != nullptr && result_idx_ < limit_) {
            plan_->rows_++;
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) {
            return nullptr;
        }

        auto rec = prev_->Next();
        if (rec == nullptr) {
            return nullptr;
        }
        if (is_sel_all_) {
            return rec;
        }

        auto &prev_cols = prev_->cols();
        auto projected = std::make_unique<RmRecord>(len_);
        size_t offset = 0;
        for (size_t idx : sel_idxs_) {
            const auto &col = prev_cols[idx];
            memcpy(projected->data + offset, rec->data + col.offset, col.len);
            offset += col.len;
        }
        return projected;
    }

    Rid &rid() override { return _abstract_rid; }
    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols_, target); }
};
