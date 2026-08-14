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
#include "record/rm_record_pool.h"
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
    bool bulk_read_requested_ = false;
    bool bulk_table_locked_ = false;
    bool track_serializable_reads_ = true;
    RmRecordPool record_pool_;
    std::vector<std::unique_ptr<RmRecord>> batch_recs_;
    std::vector<Rid> batch_rids_;
    size_t batch_index_ = 0;
    std::unique_ptr<RmRecord> current_rec_;
    bool is_end_ = true;
    bool staged_inserts_loaded_ = false;

    SmManager *sm_manager_;

    bool uses_mvcc() const {
        return context_ != nullptr && context_->txn_mgr_ != nullptr &&
               context_->txn_mgr_->uses_mvcc(context_->txn_);
    }

    void recycle_record(std::unique_ptr<RmRecord> &record) {
        record_pool_.release(std::move(record));
    }

    void recycle_batch_records() {
        recycle_record(current_rec_);
        for (auto &record : batch_recs_) {
            recycle_record(record);
        }
        batch_recs_.clear();
        batch_rids_.clear();
        batch_index_ = 0;
    }

    bool lock_reads_for_committed_visibility() const {
        return context_ != nullptr && context_->txn_ != nullptr &&
               context_->lock_mgr_ != nullptr &&
               context_->txn_mgr_ != nullptr &&
               !context_->txn_mgr_->uses_mvcc(context_->txn_);
    }

    std::vector<Rid> lock_records_for_committed_read(
        const std::vector<Rid> &rids) {
        std::vector<Rid> locked;
        if (!lock_reads_for_committed_visibility() || bulk_table_locked_) {
            return locked;
        }
        context_->lock_mgr_->lock_IS_on_table(context_->txn_, fh_->GetFd());
        locked = rids;
        std::sort(locked.begin(), locked.end(), [](const Rid &lhs, const Rid &rhs) {
            return lhs.page_no == rhs.page_no ? lhs.slot_no < rhs.slot_no
                                             : lhs.page_no < rhs.page_no;
        });
        try {
            for (const auto &rid : locked) {
                context_->lock_mgr_->lock_shared_on_record(
                    context_->txn_, rid, fh_->GetFd());
            }
        } catch (...) {
            unlock_committed_read_records(locked);
            throw;
        }
        return locked;
    }

    void unlock_committed_read_records(const std::vector<Rid> &locked) {
        if (locked.empty() || context_ == nullptr ||
            context_->txn_ == nullptr || context_->lock_mgr_ == nullptr) {
            return;
        }
        for (const auto &rid : locked) {
            context_->lock_mgr_->unlock(
                context_->txn_,
                LockDataId(fh_->GetFd(), rid, LockDataType::RECORD));
        }
    }

    std::unique_ptr<RmRecord> read_record_committed(const Rid &rid) {
        bool locked = false;
        if (lock_reads_for_committed_visibility() && !bulk_table_locked_) {
            context_->lock_mgr_->lock_IS_on_table(context_->txn_, fh_->GetFd());
            context_->lock_mgr_->lock_shared_on_record(context_->txn_, rid, fh_->GetFd());
            locked = true;
        }
        try {
            auto rec = fh_->get_record(rid, context_);
            if (locked) {
                context_->lock_mgr_->unlock(
                    context_->txn_,
                    LockDataId(fh_->GetFd(), rid, LockDataType::RECORD));
            }
            return rec;
        } catch (...) {
            if (locked) {
                context_->lock_mgr_->unlock(
                    context_->txn_,
                    LockDataId(fh_->GetFd(), rid, LockDataType::RECORD));
            }
            throw;
        }
    }

    bool fetch_cached_current() {
        while (equality_pos_ < equality_rids_.size()) {
            rid_ = equality_rids_[equality_pos_];
            auto rec = read_record_committed(rid_);
            if (rec == nullptr) {
                equality_pos_++;
                continue;
            }
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

    bool load_next_page_batch() {
        recycle_batch_records();

        while (scan_ != nullptr && !scan_->is_end() && batch_recs_.empty()) {
            int batch_size = scan_->get_batch_num();
            if (batch_size <= 0) {
                break;
            }

            std::vector<Rid> page_rids;
            page_rids.reserve(static_cast<size_t>(batch_size));
            for (int i = 0; i < batch_size; ++i) {
                page_rids.push_back(scan_->rid());
                scan_->next();
            }

            const int page_no = page_rids[0].page_no;
            std::vector<Rid> locked =
                lock_records_for_committed_read(page_rids);
            std::vector<std::unique_ptr<RmRecord>> page_recs;
            try {
                page_recs = fh_->batch_get_records(
                    page_no, page_rids, context_, &record_pool_);
            } catch (...) {
                unlock_committed_read_records(locked);
                throw;
            }
            unlock_committed_read_records(locked);

            for (size_t i = 0; i < page_recs.size(); ++i) {
                if (scan_plan_ != nullptr) {
                    scan_plan_->rows_++;
                }
                if (fed_conds_.empty() ||
                    eval_conditions(*page_recs[i], fed_conds_, cols_)) {
                    if (filter_plan_ != nullptr) {
                        filter_plan_->rows_++;
                    }
                    if (track_serializable_reads_ &&
                        context_->txn_mgr_ != nullptr) {
                        context_->txn_mgr_->register_record_read(
                            context_->txn_, fh_->GetMvccFileId(),
                            page_rids[i]);
                    }
                    batch_recs_.push_back(std::move(page_recs[i]));
                    batch_rids_.push_back(page_rids[i]);
                } else {
                    recycle_record(page_recs[i]);
                }
            }
        }

        if (batch_recs_.empty() && !staged_inserts_loaded_ && uses_mvcc()) {
            staged_inserts_loaded_ = true;
            for (const StagedWrite &write :
                 context_->txn_->write_batch().writes()) {
                if (write.kind != LogicalWriteKind::INSERT ||
                    write.file_id != fh_->GetMvccFileId()) {
                    continue;
                }
                auto record = std::make_unique<RmRecord>(
                    static_cast<int>(write.after.size()),
                    const_cast<char *>(write.after.data()));
                if (scan_plan_ != nullptr) {
                    scan_plan_->rows_++;
                }
                if (fed_conds_.empty() ||
                    eval_conditions(*record, fed_conds_, cols_)) {
                    if (filter_plan_ != nullptr) {
                        filter_plan_->rows_++;
                    }
                    batch_recs_.push_back(std::move(record));
                    batch_rids_.push_back(Rid{-1, -1});
                }
            }
        }

        if (batch_recs_.empty()) {
            current_rec_.reset();
            is_end_ = true;
            return false;
        }

        current_rec_ = std::move(batch_recs_[0]);
        rid_ = batch_rids_[0];
        is_end_ = false;
        return true;
    }

   public:
    SeqScanExecutor(SmManager *sm_manager,
                std::string tab_name,
                std::vector<Condition> conds,
                Context *context,
                ScanPlan *scan_plan = nullptr,
                FilterPlan *filter_plan = nullptr,
                bool enable_equality_cache = false,
                bool track_serializable_reads = true) {
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
        track_serializable_reads_ = track_serializable_reads;
        if (track_serializable_reads_ && context_->txn_mgr_ != nullptr) {
            context_->txn_mgr_->register_table_read(
                context_->txn_, fh_->GetMvccFileId(), fed_conds_, cols_);
        }
    }

    size_t tupleLen() const override { return len_; }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    bool is_end() const override { return is_end_; }

    void beginTuple() override {
        recycle_batch_records();
        scan_.reset();
        staged_inserts_loaded_ = false;
        bulk_table_locked_ = false;
        if (bulk_read_requested_ &&
            lock_reads_for_committed_visibility() &&
            !context_->txn_->get_txn_mode()) {
            bulk_table_locked_ = context_->lock_mgr_->lock_shared_on_table(
                context_->txn_, fh_->GetFd());
        }

        equality_rids_.clear();
        equality_pos_ = 0;
        using_equality_cache_ = false;
        for (const auto &cond : fed_conds_) {
            if (!enable_equality_cache_ ||
                (context_->txn_mgr_ != nullptr &&
                 context_->txn_mgr_->uses_mvcc(context_->txn_))) {
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
        load_next_page_batch();
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

        if (batch_index_ + 1 < batch_recs_.size()) {
            recycle_record(current_rec_);
            batch_index_++;
            current_rec_ = std::move(batch_recs_[batch_index_]);
            rid_ = batch_rids_[batch_index_];
            return;
        }

        load_next_page_batch();
    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end_ || current_rec_ == nullptr) {
            return nullptr;
        }
        return std::make_unique<RmRecord>(*current_rec_);
    }

    const RmRecord *current_record() const override {
        return current_rec_.get();
    }

    void enable_bulk_read() override { bulk_read_requested_ = true; }

    Rid &rid() override { return rid_; }
    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols_, target); }
};
