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
class ProjectionExecutor : public AbstractExecutor {
   private:
    ProjectionPlan *plan_ = nullptr;//添加projection plan显示表示
    std::unique_ptr<AbstractExecutor> prev_;
    std::vector<ColMeta> cols_;
    size_t len_;
    std::vector<size_t> sel_idxs_;

   public:
    ProjectionExecutor(std::unique_ptr<AbstractExecutor> prev,
                   const std::vector<TabCol> &sel_cols,
                   ProjectionPlan *plan = nullptr) {
        prev_ = std::move(prev);
        plan_ = plan;

        size_t curr_offset = 0;
        auto &prev_cols = prev_->cols();
        for (auto &sel_col : sel_cols) {
            auto pos = get_col(prev_cols, sel_col);
            sel_idxs_.push_back(pos - prev_cols.begin());
            auto col = *pos;
            col.offset = curr_offset;
            curr_offset += col.len;
            cols_.push_back(col);
        }
        len_ = curr_offset;
    }

    size_t tupleLen() const override { return len_; }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    bool is_end() const override { return prev_->is_end(); }

    void beginTuple() override {
        prev_->beginTuple();
        if (!prev_->is_end() && plan_ != nullptr) {
            plan_->rows_++;
        }
    }

    void nextTuple() override {
        prev_->nextTuple();
        if (!prev_->is_end() && plan_ != nullptr) {
            plan_->rows_++;
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        auto rec = prev_->Next();
        if (rec == nullptr) {
            return nullptr;
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
};
