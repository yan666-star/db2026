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
#include <cmath>
#include <mutex>

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class InsertExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;
    std::vector<Value> values_;
    RmFileHandle *fh_;
    std::string tab_name_;
    Rid rid_;
    SmManager *sm_manager_;
    WriteRecord *bulk_insert_record_ = nullptr;

   public:
    InsertExecutor(SmManager *sm_manager, const std::string &tab_name,
                   std::vector<Value> values, Context *context,
                   WriteRecord *bulk_insert_record = nullptr) {
        sm_manager_ = sm_manager;
        tab_ = sm_manager_->db_.get_table(tab_name);
        values_ = std::move(values);
        tab_name_ = tab_name;
        if (values_.size() != tab_.cols.size()) {
            throw InvalidValueCountError();
        }
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        context_ = context;
        bulk_insert_record_ = bulk_insert_record;
        if (bulk_insert_record_ != nullptr &&
            (bulk_insert_record_->GetWriteType() !=
                 WType::BULK_INSERT_TUPLES ||
             bulk_insert_record_->GetTableName() != tab_name_)) {
            throw InternalError("Invalid bulk insert undo record");
        }
    };

    std::unique_ptr<RmRecord> Next() override {
        RmRecord rec(fh_->get_file_hdr().record_size);
        memset(rec.data, 0, fh_->get_file_hdr().record_size);
        for (size_t i = 0; i < values_.size(); i++) {
            auto &col = tab_.cols[i];
            auto &val = values_[i];
            if (col.type != val.type) {
                if (col.type == TYPE_FLOAT && val.type == TYPE_INT) {
                    val.set_float(static_cast<float>(val.int_val));
                } else {
                    throw IncompatibleTypeError(coltype2str(col.type), coltype2str(val.type));
                }
            }
            if (col.type == TYPE_FLOAT &&
                !std::isfinite(val.float_val)) {
                throw RMDBError("FLOAT value must be finite");
            }
            val.init_raw(col.len);
            memcpy(rec.data + col.offset, val.raw->data, col.len);
        }

        bool uses_mvcc =
            context_->txn_mgr_ != nullptr &&
            context_->txn_mgr_->uses_mvcc(context_->txn_);
        std::unique_lock<std::mutex> unique_insert_guard;
        if (!tab_.indexes.empty()) {
            unique_insert_guard = fh_->acquire_logical_update_latch();
        }

        std::vector<std::vector<char>> index_keys;
        std::vector<std::string> index_names;
        index_keys.reserve(tab_.indexes.size());
        index_names.reserve(tab_.indexes.size());
        for (const auto &index : tab_.indexes) {
            index_keys.emplace_back(index.col_tot_len);
            auto &key = index_keys.back();
            int offset = 0;
            for (int j = 0; j < index.col_num; ++j) {
                memcpy(key.data() + offset,
                       rec.data + index.cols[j].offset,
                       index.cols[j].len);
                offset += index.cols[j].len;
            }
            index_names.push_back(
                sm_manager_->get_ix_manager()->get_index_name(
                    tab_name_, index.cols));
        }

        // Check every unique index before changing either the table or index.
        for (size_t i = 0; i < tab_.indexes.size(); ++i) {
            const auto &index = tab_.indexes[i];
            auto ih = sm_manager_->ihs_.at(index_names[i]).get();
            std::vector<Rid> result;
            if (ih->get_value(index_keys[i].data(), &result,
                              context_->txn_)) {
                if (uses_mvcc) {
                    for (const auto &dup_rid : result) {
                        context_->txn_mgr_->check_write_conflict(
                            context_->txn_, fh_->GetMvccFileId(), dup_rid);
                    }
                }
                throw RMDBError("failure");
            }
            if (uses_mvcc) {
                context_->txn_mgr_->check_unique_key_conflict(
                    context_->txn_, fh_->GetMvccFileId(), Rid{-1, -1}, rec,
                    index.cols);
            }
        }

        rid_ = fh_->insert_record(rec.data, context_, tab_name_);
        if (context_->txn_ != nullptr) {
            if (bulk_insert_record_ != nullptr) {
                bulk_insert_record_->AppendRid(rid_);
            } else {
                context_->txn_->append_write_record(
                    new WriteRecord(WType::INSERT_TUPLE, tab_name_, rid_));
            }
        }

        for (size_t i = 0; i < tab_.indexes.size(); ++i) {
            auto ih = sm_manager_->ihs_.at(index_names[i]).get();
            ih->insert_entry(index_keys[i].data(), rid_, context_->txn_);
        }
        return nullptr;
    }
    Rid &rid() override { return rid_; }
};
