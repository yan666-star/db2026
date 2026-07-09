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
#include <limits>
#include <map>
#include <unordered_map>
#include <vector>

#include "execution_eval.h"
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class IndexScanExecutor : public AbstractExecutor {
   private:
    SmManager *sm_manager_;
    std::string tab_name_;
    TabMeta tab_;
    std::vector<std::string> index_col_names_;
    RmFileHandle *fh_;
    std::vector<ColMeta> cols_;
    size_t len_;
    IndexMeta index_meta_;
    std::vector<Condition> conds_;
    std::unordered_map<std::string, std::vector<Condition>> col2conds_;

    Rid rid_{-1, -1};
    std::unique_ptr<RecScan> scan_;
    std::unique_ptr<RmRecord> rec_;
    bool is_end_ = false;

    std::vector<std::unique_ptr<RmRecord>> batch_recs_;
    std::vector<Rid> batch_rids_;
    size_t batch_index_ = 0;
    std::unordered_map<int, std::vector<Rid>> batch_rids_map_;
    ScanPlan *scan_plan_ = nullptr;
    std::vector<char> lookup_key_;
    bool has_lookup_key_ = false;
    bool track_serializable_reads_ = true;

    bool uses_mvcc() const {
        return context_ != nullptr && context_->txn_mgr_ != nullptr &&
               context_->txn_mgr_->uses_mvcc(context_->txn_);
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
        if (!lock_reads_for_committed_visibility()) {
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

   public:
    IndexScanExecutor(SmManager *sm_manager, std::string tab_name, std::vector<Condition> conds,
                      std::vector<std::string> index_col_names, Context *context,
                      ScanPlan *scan_plan = nullptr,
                      bool track_serializable_reads = true) {
        sm_manager_ = sm_manager;
        context_ = context;
        scan_plan_ = scan_plan;
        track_serializable_reads_ = track_serializable_reads;
        tab_name_ = std::move(tab_name);
        tab_ = sm_manager_->db_.get_table(tab_name_);
        conds_ = std::move(conds);
        index_col_names_ = std::move(index_col_names);
        index_meta_ = *(tab_.get_index_meta(index_col_names_));
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        cols_ = tab_.cols;
        len_ = cols_.back().offset + cols_.back().len;

        std::map<CompOp, CompOp> swap_op = {
            {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
        };
        for (auto &cond : conds_) {
            if (cond.lhs_col.tab_name != tab_name_) {
                assert(!cond.is_rhs_val && cond.rhs_col.tab_name == tab_name_);
                std::swap(cond.lhs_col, cond.rhs_col);
                cond.op = swap_op.at(cond.op);
            }
            if (cond.is_rhs_val && cond.op != OP_NE) {
                auto it = std::find(index_col_names_.begin(), index_col_names_.end(), cond.lhs_col.col_name);
                if (it != index_col_names_.end()) {
                    col2conds_[cond.lhs_col.col_name].push_back(cond);
                }
            }
        }
        if (track_serializable_reads_ && context_->txn_mgr_ != nullptr) {
            context_->txn_mgr_->register_table_read(
                context_->txn_, fh_->GetMvccFileId(), conds_, cols_);
        }
    }

    void beginTuple() override {
        is_end_ = false;
        rec_.reset();
        scan_.reset();
        batch_recs_.clear();
        batch_rids_.clear();
        batch_rids_map_.clear();
        batch_index_ = 0;
        rid_ = {-1, -1};

        std::vector<std::string> full_index_col_names;
        for (const auto &col : index_meta_.cols) {
            full_index_col_names.push_back(col.name);
        }
        IxIndexHandle *ih =
            sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, full_index_col_names)).get();

        char *lower_key = new char[index_meta_.col_tot_len];
        char *upper_key = new char[index_meta_.col_tot_len];
        if (has_lookup_key_) {
            memcpy(lower_key, lookup_key_.data(), index_meta_.col_tot_len);
            memcpy(upper_key, lookup_key_.data(), index_meta_.col_tot_len);
        } else {
            build_lower_key(lower_key);
            build_upper_key(upper_key);
        }
        if (compare_index_key(upper_key, lower_key, index_meta_) < 0) {
            is_end_ = true;
            delete[] lower_key;
            delete[] upper_key;
            return;
        }

        auto lower_iid = ih->lower_bound(lower_key);
        auto upper_iid = ih->upper_bound(upper_key);
        scan_ = std::make_unique<IxScan>(ih, lower_iid, upper_iid, sm_manager_->get_bpm());
        delete[] lower_key;
        delete[] upper_key;

        while (!scan_->is_end()) {
            Rid r = scan_->rid();
            if (r.page_no >= 0) {
                batch_rids_map_[r.page_no].push_back(r);
            }
            scan_->next();
        }

        load_next_batch();
    }

    void nextTuple() override {
        if (is_end_ || !scan_) {
            return;
        }

        if (batch_index_ + 1 < batch_recs_.size()) {
            batch_index_++;
            rec_ = std::make_unique<RmRecord>(*batch_recs_[batch_index_]);
            rid_ = batch_rids_[batch_index_];
            return;
        }

        load_next_batch();
    }

    std::unique_ptr<RmRecord> Next() override {
        if (!rec_) {
            return nullptr;
        }
        return std::make_unique<RmRecord>(*rec_);
    }

    bool is_end() const override { return is_end_ || (scan_ && scan_->is_end() && rid_.slot_no == -1); }

    size_t tupleLen() const override { return len_; }

    Rid &rid() override { return rid_; }
    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols_, target); }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    bool set_index_lookup(const TabCol &target, const char *data, ColType type, int len) override {
        if (target.tab_name != tab_name_ || index_meta_.col_num != 1 ||
            index_meta_.cols[0].name != target.col_name ||
            index_meta_.cols[0].type != type || index_meta_.cols[0].len != len) {
            return false;
        }
        lookup_key_.assign(data, data + len);
        has_lookup_key_ = true;
        return true;
    }

   private:
    void load_next_batch() {
        batch_recs_.clear();
        batch_rids_.clear();
        batch_index_ = 0;
        rec_.reset();
        rid_ = {-1, -1};

        while (batch_recs_.empty() && !batch_rids_map_.empty()) {
            std::vector<std::unique_ptr<RmRecord>> tmp_batch_recs;
            std::vector<Rid> tmp_batch_rids;
            bool has_found = false;

            for (auto it = batch_rids_map_.begin(); it != batch_rids_map_.end();) {
                auto page_no = it->first;
                auto rids = std::move(it->second);
                it = batch_rids_map_.erase(it);
                if (rids.empty()) {
                    continue;
                }
                if (uses_mvcc()) {
                    for (const auto &rid : rids) {
                        auto rec = fh_->get_record(rid, context_);
                        if (rec == nullptr) {
                            continue;
                        }
                        tmp_batch_recs.push_back(std::move(rec));
                        tmp_batch_rids.push_back(rid);
                    }
                } else {
                    std::vector<Rid> locked =
                        lock_records_for_committed_read(rids);
                    std::vector<std::unique_ptr<RmRecord>> page_recs;
                    try {
                        page_recs = fh_->batch_get_records(page_no, rids, context_);
                    } catch (...) {
                        unlock_committed_read_records(locked);
                        throw;
                    }
                    unlock_committed_read_records(locked);
                    if (page_recs.size() != rids.size()) {
                        throw InternalError("Batch size mismatch in IndexScanExecutor");
                    }
                    tmp_batch_recs.insert(tmp_batch_recs.end(), std::make_move_iterator(page_recs.begin()),
                                          std::make_move_iterator(page_recs.end()));
                    tmp_batch_rids.insert(tmp_batch_rids.end(), rids.begin(), rids.end());
                }
                has_found = true;
                break;
            }

            if (!has_found) {
                break;
            }

            for (size_t i = 0; i < tmp_batch_recs.size(); ++i) {
                if (scan_plan_ != nullptr) {
                    scan_plan_->rows_++;
                }
                if (eval_conditions(*tmp_batch_recs[i], conds_, cols_)) {
                    if (track_serializable_reads_ &&
                        context_->txn_mgr_ != nullptr) {
                        context_->txn_mgr_->register_record_read(
                            context_->txn_, fh_->GetMvccFileId(),
                            tmp_batch_rids[i]);
                    }
                    batch_recs_.push_back(std::move(tmp_batch_recs[i]));
                    batch_rids_.push_back(tmp_batch_rids[i]);
                }
            }
        }

        if (!batch_recs_.empty()) {
            rec_ = std::make_unique<RmRecord>(*batch_recs_[0]);
            rid_ = batch_rids_[0];
        } else {
            is_end_ = true;
        }
    }

    void build_lower_key(char *key) {
        int offset = 0;
        bool range_applied = false;
        for (int i = 0; i < index_meta_.col_num; ++i) {
            auto col = index_meta_.cols[i];
            auto it = col2conds_.find(col.name);
            if (it != col2conds_.end() && !range_applied) {
                bool has_eq = false;
                Condition eq_cond;
                for (const auto &cond : it->second) {
                    if (cond.op == OP_EQ) {
                        eq_cond = cond;
                        has_eq = true;
                        break;
                    }
                }
                if (has_eq) {
                    write_condition_rhs_val_to_key(key + offset, eq_cond, col.len);
                } else {
                    Condition best_lower;
                    bool has_lower = false;
                    for (const auto &cond : it->second) {
                        if (cond.op == OP_GT || cond.op == OP_GE) {
                            if (!has_lower || is_better_lower_bound(cond, best_lower)) {
                                best_lower = cond;
                                has_lower = true;
                            }
                        }
                    }
                    if (has_lower) {
                        write_condition_rhs_val_to_key(key + offset, best_lower, col.len);
                        range_applied = true;
                    } else {
                        write_min_to_key(key + offset, col.type, col.len);
                        range_applied = true;
                    }
                }
            } else {
                write_min_to_key(key + offset, col.type, col.len);
            }
            offset += col.len;
        }
    }

    void build_upper_key(char *key) {
        int offset = 0;
        bool range_applied = false;
        for (int i = 0; i < index_meta_.col_num; ++i) {
            auto col = index_meta_.cols[i];
            auto it = col2conds_.find(col.name);
            if (it != col2conds_.end() && !range_applied) {
                bool has_eq = false;
                Condition eq_cond;
                for (const auto &cond : it->second) {
                    if (cond.op == OP_EQ) {
                        eq_cond = cond;
                        has_eq = true;
                        break;
                    }
                }
                if (has_eq) {
                    write_condition_rhs_val_to_key(key + offset, eq_cond, col.len);
                } else {
                    Condition best_upper;
                    bool has_upper = false;
                    for (const auto &cond : it->second) {
                        if (cond.op == OP_LT || cond.op == OP_LE) {
                            if (!has_upper || is_better_upper_bound(cond, best_upper)) {
                                best_upper = cond;
                                has_upper = true;
                            }
                        }
                    }
                    if (has_upper) {
                        write_condition_rhs_val_to_key(key + offset, best_upper, col.len);
                        range_applied = true;
                    } else {
                        write_max_to_key(key + offset, col.type, col.len);
                        range_applied = true;
                    }
                }
            } else {
                write_max_to_key(key + offset, col.type, col.len);
            }
            offset += col.len;
        }
    }

    void write_condition_rhs_val_to_key(char *key, const Condition &cond, int len = 0) {
        if (cond.rhs_val.raw) {
            memcpy(key, cond.rhs_val.raw->data, len);
            return;
        }
        switch (cond.rhs_val.type) {
            case TYPE_INT:
                memcpy(key, &cond.rhs_val.int_val, sizeof(int));
                break;
            case TYPE_FLOAT:
                memcpy(key, &cond.rhs_val.float_val, sizeof(float));
                break;
            default:
                memset(key, 0, len);
                if (len > 0 && !cond.rhs_val.str_val.empty()) {
                    int copy_len = std::min(static_cast<int>(cond.rhs_val.str_val.length()), len);
                    memcpy(key, cond.rhs_val.str_val.c_str(), copy_len);
                }
                break;
        }
    }

    void write_max_to_key(char *key, ColType type, int len) {
        switch (type) {
            case TYPE_INT: {
                int max_val = std::numeric_limits<int>::max();
                memcpy(key, &max_val, sizeof(int));
                break;
            }
            case TYPE_FLOAT: {
                float max_val = std::numeric_limits<float>::max();
                memcpy(key, &max_val, sizeof(float));
                break;
            }
            default:
                memset(key, 0xFF, len);
                break;
        }
    }

    void write_min_to_key(char *key, ColType type, int len) {
        switch (type) {
            case TYPE_INT: {
                int min_val = std::numeric_limits<int>::min();
                memcpy(key, &min_val, sizeof(int));
                break;
            }
            case TYPE_FLOAT: {
                float min_val = std::numeric_limits<float>::lowest();
                memcpy(key, &min_val, sizeof(float));
                break;
            }
            default:
                memset(key, 0, len);
                break;
        }
    }

    bool is_better_lower_bound(const Condition &a, const Condition &b) {
        if (a.op == OP_GE && b.op == OP_GT) return true;
        if (a.op == OP_GT && b.op == OP_GE) return false;
        return compare_values(a.rhs_val, b.rhs_val) > 0;
    }

    bool is_better_upper_bound(const Condition &a, const Condition &b) {
        if (a.op == OP_LE && b.op == OP_LT) return true;
        if (a.op == OP_LT && b.op == OP_LE) return false;
        return compare_values(a.rhs_val, b.rhs_val) < 0;
    }

    int compare_values(const Value &v1, const Value &v2) {
        switch (v1.type) {
            case TYPE_INT:
                if (v1.int_val < v2.int_val) return -1;
                if (v1.int_val > v2.int_val) return 1;
                return 0;
            case TYPE_FLOAT:
                if (v1.float_val < v2.float_val) return -1;
                if (v1.float_val > v2.float_val) return 1;
                return 0;
            case TYPE_STRING:
                return v1.str_val.compare(v2.str_val);
            default:
                return 0;
        }
    }

    int compare_index_key(const char *lhs, const char *rhs, const IndexMeta &meta) {
        int offset = 0;
        for (int i = 0; i < meta.col_num; ++i) {
            const auto &col = meta.cols[i];
            int cmp = compare_col_value(lhs + offset, rhs + offset, col.type, col.len);
            if (cmp != 0) {
                return cmp;
            }
            offset += col.len;
        }
        return 0;
    }
};
