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

class UpdateExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<Rid> rids_;
    std::string tab_name_;
    std::vector<SetClause> set_clauses_;
    SmManager *sm_manager_;
    bool done_ = false;

    bool uses_mvcc() const {
        return context_->txn_mgr_ != nullptr &&
               context_->txn_mgr_->uses_mvcc(context_->txn_);
    }

   public:
    UpdateExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<SetClause> set_clauses,
                   std::vector<Condition> conds, std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        set_clauses_ = std::move(set_clauses);
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

        for (auto &rid : rids_) {
            auto rec = fh_->get_record(rid, context_);
            if (rec == nullptr) {
                continue;
            }
            RmRecord old_rec(*rec);
            auto rec_new = std::make_unique<RmRecord>(*rec);
            for (auto &set_clause : set_clauses_) {
                auto col = tab_.get_col(set_clause.lhs.col_name);
                if (!set_clause.is_arithmetic) {
                    memcpy(rec_new->data + col->offset,
                           set_clause.rhs.raw->data, col->len);
                    continue;
                }

                auto rhs_col = tab_.get_col(set_clause.rhs_col.col_name);
                if (col->type == TYPE_INT) {
                    int current;
                    memcpy(&current, rec->data + rhs_col->offset,
                           sizeof(current));
                    int operand = set_clause.rhs.int_val;
                    int result;
                    switch (set_clause.arithmetic_op) {
                        case '+': result = current + operand; break;
                        case '-': result = current - operand; break;
                        case '*': result = current * operand; break;
                        case '/':
                            if (operand == 0) {
                                throw RMDBError("failure");
                            }
                            result = current / operand;
                            break;
                        default: throw RMDBError("failure");
                    }
                    memcpy(rec_new->data + col->offset, &result,
                           sizeof(result));
                } else if (col->type == TYPE_FLOAT) {
                    float current;
                    memcpy(&current, rec->data + rhs_col->offset,
                           sizeof(current));
                    float operand = set_clause.rhs.float_val;
                    float result;
                    switch (set_clause.arithmetic_op) {
                        case '+': result = current + operand; break;
                        case '-': result = current - operand; break;
                        case '*': result = current * operand; break;
                        case '/':
                            if (operand == 0.0f) {
                                throw RMDBError("failure");
                            }
                            result = current / operand;
                            break;
                        default: throw RMDBError("failure");
                    }
                    memcpy(rec_new->data + col->offset, &result,
                           sizeof(result));
                } else {
                    throw RMDBError("failure");
                }
            }

            bool mvcc = uses_mvcc();
            if (mvcc) {
                context_->txn_mgr_->check_write_conflict(
                    context_->txn_, fh_->GetMvccFileId(), rid);
            }

            int max_key_len = 0;
            for (auto &index : tab_.indexes) {
                max_key_len = std::max(max_key_len, index.col_tot_len);
            }
            char *key = max_key_len > 0 ? new char[max_key_len] : nullptr;
            for (auto &index : tab_.indexes) {
                if (mvcc) {
                    context_->txn_mgr_->check_unique_key_conflict(
                        context_->txn_, fh_->GetMvccFileId(), rid,
                        *rec_new, index.cols);
                }
                auto ih =
                    sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                int offset = 0;
                for (int i = 0; i < index.col_num; ++i) {
                    memcpy(key + offset, rec_new->data + index.cols[i].offset, index.cols[i].len);
                    offset += index.cols[i].len;
                }
                std::vector<Rid> dup;
                if (ih->get_value(key, &dup, context_->txn_) &&
                    !(dup.size() == 1 && dup[0] == rid)) {
                    if (mvcc) {
                        for (const auto &dup_rid : dup) {
                            if (!(dup_rid == rid)) {
                                context_->txn_mgr_->check_write_conflict(
                                    context_->txn_, fh_->GetMvccFileId(), dup_rid);
                            }
                        }
                    }
                    delete[] key;
                    throw RMDBError("failure");
                }
            }

            if (mvcc) {
                context_->txn_mgr_->prepare_update(
                    context_->txn_, fh_->GetMvccFileId(), rid, old_rec,
                    *rec_new, tab_name_);
            }
            if (context_->txn_ != nullptr && context_->log_mgr_ != nullptr) {
                UpdateLogRecord log_record(
                    context_->txn_->get_transaction_id(), old_rec, *rec_new, rid, tab_name_);
                log_record.prev_lsn_ = context_->txn_->get_prev_lsn();
                lsn_t lsn = context_->log_mgr_->add_log_to_buffer(&log_record);
                context_->txn_->set_prev_lsn(lsn);
            }
            if (context_->txn_ != nullptr) {
                context_->txn_->append_write_record(
                    new WriteRecord(WType::UPDATE_TUPLE, tab_name_, rid, old_rec));
            }

            if (!mvcc) {
                fh_->update_record(rid, rec_new->data, context_);

                for (auto &index : tab_.indexes) {
                    auto ih =
                        sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                    int offset = 0;
                    for (int i = 0; i < index.col_num; ++i) {
                        memcpy(key + offset, rec->data + index.cols[i].offset, index.cols[i].len);
                        offset += index.cols[i].len;
                    }
                    std::vector<Rid> old_rids;
                    if (ih->get_value(key, &old_rids, context_->txn_) && !old_rids.empty() &&
                        old_rids[0] == rid) {
                        ih->delete_entry(key, context_->txn_);
                    }
                    offset = 0;
                    for (int i = 0; i < index.col_num; ++i) {
                        memcpy(key + offset, rec_new->data + index.cols[i].offset, index.cols[i].len);
                        offset += index.cols[i].len;
                    }
                    ih->insert_entry(key, rid, context_->txn_);
                }
            }
            delete[] key;
        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};
