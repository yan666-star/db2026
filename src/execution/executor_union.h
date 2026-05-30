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

#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "execution_eval.h"
#include "executor_abstract.h"
#include "optimizer/plan.h"

class UnionExecutor : public AbstractExecutor {
   private:
    UnionPlan *plan_ = nullptr;
    std::vector<std::unique_ptr<AbstractExecutor>> branch_execs_;
    std::vector<ColMeta> cols_;
    size_t len_ = 0;
    std::vector<std::unique_ptr<RmRecord>> tuples_;
    size_t cursor_ = 0;

    static void convert_value(char *dst, const ColMeta &dst_col, const char *src, const ColMeta &src_col) {
        if (src_col.type == TYPE_INT && dst_col.type == TYPE_FLOAT) {
            *(float *)dst = static_cast<float>(*(const int *)src);
            return;
        }
        if (src_col.type == TYPE_STRING && dst_col.type == TYPE_STRING) {
            memset(dst, 0, dst_col.len);
            memcpy(dst, src, src_col.len);
            return;
        }
        memcpy(dst, src, dst_col.len);
    }

    std::unique_ptr<RmRecord> unify_row(const RmRecord &rec, const std::vector<ColMeta> &src_cols) {
        auto unified = std::make_unique<RmRecord>(len_);
        size_t dst_off = 0;
        for (size_t i = 0; i < cols_.size(); i++) {
            convert_value(unified->data + dst_off, cols_[i], rec.data + src_cols[i].offset, src_cols[i]);
            dst_off += cols_[i].len;
        }
        return unified;
    }

   public:
    UnionExecutor(std::vector<std::unique_ptr<AbstractExecutor>> branch_execs, UnionPlan *plan)
        : plan_(plan), branch_execs_(std::move(branch_execs)) {
        cols_ = plan_->out_cols_;
        len_ = plan_->len_;
    }

    void beginTuple() override {
        tuples_.clear();
        cursor_ = 0;
        std::unordered_set<std::string> seen;

        for (auto &exec : branch_execs_) {
            exec->beginTuple();
            const auto &src_cols = exec->cols();
            for (; !exec->is_end(); exec->nextTuple()) {
                auto rec = exec->Next();
                if (rec == nullptr) {
                    continue;
                }
                auto unified = unify_row(*rec, src_cols);
                std::string key(unified->data, len_);
                if (seen.insert(key).second) {
                    tuples_.push_back(std::move(unified));
                }
            }
        }

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

    size_t tupleLen() const override { return len_; }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    Rid &rid() override { return _abstract_rid; }

    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols_, target); }
};
