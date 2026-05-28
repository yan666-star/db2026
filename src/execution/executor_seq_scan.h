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
    std::unique_ptr<RmRecord> current_rec_;
    bool is_end_ = true;

    SmManager *sm_manager_;

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
                FilterPlan *filter_plan = nullptr) {
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
        filter_plan_ = filter_plan; //添加scan 和 filter plan显示表示
    }

    size_t tupleLen() const override { return len_; }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    bool is_end() const override { return is_end_; }

    void beginTuple() override {
        scan_ = std::make_unique<RmScan>(fh_);
        fetch_current();
    }

    void nextTuple() override {
        if (is_end_) {
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
