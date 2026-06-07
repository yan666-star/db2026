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
#include "record/rm_scan.h"
#include "system/sm.h"
#include "optimizer/plan.h"
class SeqScanExecutor : public AbstractExecutor {
   private:
    ScanPlan *scan_plan_ = nullptr;
    FilterPlan *filter_plan_ = nullptr; //添加filter plan显示表示
    std::string tab_name_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<ColMeta> cols_;
    size_t len_;
    std::vector<Condition> fed_conds_;

    Rid rid_;
    std::unique_ptr<RecScan> scan_;
    std::vector<Rid> equality_rids_;
    size_t equality_pos_ = 0;
    bool using_equality_cache_ = false;
    bool enable_equality_cache_ = false;
    std::unique_ptr<RmRecord> current_rec_;
    bool is_end_ = true;

    SmManager *sm_manager_;

    bool fetch_cached_current() {
        while (equality_pos_ < equality_rids_.size()) {
            rid_ = equality_rids_[equality_pos_];
            auto rec = fh_->get_record(rid_, context_);
            if (scan_plan_ != nullptr) {
                scan_plan_->rows_++;
            }
            if (fed_conds_.empty() || eval_conditions(*rec, fed_conds_, cols_)) {
                if (filter_plan_ != nullptr) {
                    filter_plan_->rows_++;
                }
                current_rec_ = std::move(rec);
                is_end_ = false;
                return true;
            }
            equality_pos_++;
        }
        current_rec_.reset();
        is_end_ = true;
        return false;
    }

    bool fetch_current() {
    while (!scan_->is_end()) {
        rid_ = scan_->rid();
        auto rec = fh_->get_record(rid_, context_);

        if (scan_plan_ != nullptr) {
            scan_plan_->rows_++;//每读取一条原始记录，Scan rows++ 每输出一条满足条件记录，Filter rows++
        }

        if (fed_conds_.empty() || eval_conditions(*rec, fed_conds_, cols_)) {
            if (filter_plan_ != nullptr) {
                filter_plan_->rows_++;
            }
            current_rec_ = std::move(rec);
            is_end_ = false;
            return true;
        }
        scan_->next();
    }
    current_rec_.reset();
    is_end_ = true;
    return false;
    }

   public:
    SeqScanExecutor(SmManager *sm_manager,
                std::string tab_name,
                std::vector<Condition> conds,
                Context *context,
                ScanPlan *scan_plan = nullptr,
                FilterPlan *filter_plan = nullptr,
                bool enable_equality_cache = false) {
        sm_manager_ = sm_manager;
        tab_name_ = std::move(tab_name);
        conds_ = std::move(conds);
        TabMeta &tab = sm_manager_->db_.get_table(tab_name_);
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        cols_ = tab.cols;
        len_ = cols_.back().offset + cols_.back().len;

        context_ = context;
        fed_conds_ = conds_;
        scan_plan_ = scan_plan;
        enable_equality_cache_ = enable_equality_cache;
        filter_plan_ = filter_plan; //添加scan 和 filter plan显示表示
    }

    size_t tupleLen() const override { return len_; }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    bool is_end() const override { return is_end_; }

    void beginTuple() override {
        equality_rids_.clear();
        equality_pos_ = 0;
        using_equality_cache_ = false;
        for (const auto &cond : fed_conds_) {
            if (!enable_equality_cache_) {
                break;
            }
            if (!cond.is_rhs_val || cond.op != OP_EQ) {
                continue;
            }
            const ColMeta *col = find_col(cols_, cond.lhs_col);
            if (col->type != TYPE_INT || col->len != static_cast<int>(sizeof(int)) ||
                cond.rhs_val.type != TYPE_INT) {
                continue;
            }
            equality_rids_ = fh_->lookup_int_equal_records(col->offset, cond.rhs_val.int_val);
            using_equality_cache_ = true;
            fetch_cached_current();
            return;
        }
        scan_ = std::make_unique<RmScan>(fh_);
        fetch_current();
    }

    void nextTuple() override {
        if (is_end_) {
            return;
        }
        if (using_equality_cache_) {
            equality_pos_++;
            fetch_cached_current();
            return;
        }
        scan_->next();
        fetch_current();
    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end_ || current_rec_ == nullptr) {
            return nullptr;
        }
        return std::make_unique<RmRecord>(*current_rec_);
    }

    Rid &rid() override { return rid_; }
    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols_, target); }
};
