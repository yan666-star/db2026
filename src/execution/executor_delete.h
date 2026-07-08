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
#include <mutex>
#include <algorithm>
#include "execution_defs.h"
#include "execution_eval.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class DeleteExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<Rid> rids_;
    std::string tab_name_;
    SmManager *sm_manager_;
    bool done_ = false;

    bool should_lock_non_mvcc() const {
        return context_ != nullptr && context_->txn_ != nullptr &&
               context_->lock_mgr_ != nullptr && context_->txn_mgr_ != nullptr &&
               !context_->txn_mgr_->uses_mvcc(context_->txn_);
    }

    void lock_write_records() {
        if (!should_lock_non_mvcc()) {
            return;
        }
        context_->lock_mgr_->lock_IX_on_table(context_->txn_, fh_->GetFd());
        std::sort(rids_.begin(), rids_.end(), [](const Rid &lhs, const Rid &rhs) {
            return lhs.page_no == rhs.page_no ? lhs.slot_no < rhs.slot_no
                                             : lhs.page_no < rhs.page_no;
        });
        for (const auto &rid : rids_) {
            context_->lock_mgr_->lock_exclusive_on_record(context_->txn_, rid, fh_->GetFd());
        }
    }

   public:
    DeleteExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<Condition> conds,
                   std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = std::move(conds);
        rids_ = std::move(rids);
        context_ = context;
    }

    std::unique_ptr<RmRecord> Next() override {
        if (done_) {
            return nullptr;
        }
        done_ = true;
        lock_write_records();
        for (auto &rid : rids_) {
            bool uses_mvcc =
                context_->txn_mgr_ != nullptr &&
                context_->txn_mgr_->uses_mvcc(context_->txn_);
            std::unique_lock<std::mutex> delete_guard;
            if (!uses_mvcc) {
                delete_guard = fh_->acquire_logical_update_latch();
            }

            auto rec = fh_->get_record(rid, context_);
            if (rec == nullptr) {
                continue;
            }
            if (!uses_mvcc && !conds_.empty() &&
                !eval_conditions(*rec, conds_, tab_.cols)) {
                continue;
            }
            RmRecord old_rec(*rec);
            if (uses_mvcc) {
                context_->txn_mgr_->prepare_delete(
                    context_->txn_, fh_->GetMvccFileId(), rid, old_rec,
                    tab_name_);
            }
            if (context_->txn_ != nullptr && context_->log_mgr_ != nullptr) {
                DeleteLogRecord log_record(
                    context_->txn_->get_transaction_id(), old_rec, rid, tab_name_);
                log_record.prev_lsn_ = context_->txn_->get_prev_lsn();
                lsn_t lsn = context_->log_mgr_->add_log_to_buffer(&log_record);
                context_->txn_->set_prev_lsn(lsn);
            }
            if (context_->txn_ != nullptr) {
                context_->txn_->append_write_record(
                    new WriteRecord(WType::DELETE_TUPLE, tab_name_, rid, old_rec));
            }
            if (!uses_mvcc) {
                for (auto &index : tab_.indexes) {
                    auto ih =
                        sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                    char *key = new char[index.col_tot_len];
                    int offset = 0;
                    for (int j = 0; j < index.col_num; ++j) {
                        memcpy(key + offset, rec->data + index.cols[j].offset, index.cols[j].len);
                        offset += index.cols[j].len;
                    }
                    ih->delete_entry(key, context_->txn_);
                    delete[] key;
                }
                fh_->delete_record(rid, context_);
            }
        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};
