#pragma once

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "execution_defs.h"
#include "index/ix.h"
#include "system/sm.h"

/**
 * Generic prepared INSERT batch. Recognition happens from the prepared plan
 * tag; this executor contains no SQL text, statement-id, table, or column-name
 * special cases.
 */
class BatchInsertExecutor {
   public:
    BatchInsertExecutor(SmManager *sm_manager, std::string table_name,
                        std::vector<std::vector<Value>> value_rows,
                        Context *context)
        : sm_manager_(sm_manager),
          table_name_(std::move(table_name)),
          value_rows_(std::move(value_rows)),
          context_(context) {}

    void execute() {
        TabMeta table = sm_manager_->db_.get_table(table_name_);
        RmFileHandle *file = sm_manager_->fhs_.at(table_name_).get();
        const int record_size = file->get_file_hdr().record_size;
        const bool mvcc = context_->txn_mgr_ != nullptr &&
                          context_->txn_mgr_->uses_mvcc(context_->txn_);

        std::unique_lock<std::mutex> table_guard;
        if (!mvcc && !table.indexes.empty()) {
            table_guard = file->acquire_logical_update_latch();
        }

        std::vector<std::vector<char>> records;
        records.reserve(value_rows_.size());
        for (auto values : value_rows_) {
            if (values.size() != table.cols.size()) {
                throw InvalidValueCountError();
            }
            records.emplace_back(static_cast<size_t>(record_size), 0);
            std::vector<char> &record = records.back();
            for (size_t column_index = 0; column_index < values.size();
                 ++column_index) {
                ColMeta &column = table.cols[column_index];
                Value &value = values[column_index];
                if (column.type != value.type) {
                    if (column.type == TYPE_FLOAT && value.type == TYPE_INT) {
                        value.set_float(static_cast<float>(value.int_val));
                    } else {
                        throw IncompatibleTypeError(
                            coltype2str(column.type), coltype2str(value.type));
                    }
                }
                if (column.type == TYPE_FLOAT &&
                    !std::isfinite(value.float_val)) {
                    throw RMDBError("FLOAT value must be finite");
                }
                value.raw.reset();
                value.init_raw(column.len);
                std::memcpy(record.data() + column.offset,
                            value.raw->data, column.len);
            }
        }

        struct IndexWork {
            IxIndexHandle *handle;
            const IndexMeta *metadata;
            std::vector<std::vector<char>> keys;
        };
        std::vector<IndexWork> indexes;
        indexes.reserve(table.indexes.size());
        for (const IndexMeta &index : table.indexes) {
            const std::string index_name =
                sm_manager_->get_ix_manager()->get_index_name(
                    table_name_, index.cols);
            IndexWork work{sm_manager_->ihs_.at(index_name).get(), &index,
                           {}};
            work.keys.reserve(records.size());
            std::unordered_set<std::string> batch_keys;
            batch_keys.reserve(records.size() * 2 + 1);
            for (const std::vector<char> &record : records) {
                work.keys.emplace_back(index.col_tot_len);
                std::vector<char> &key = work.keys.back();
                int offset = 0;
                for (const ColMeta &column : index.cols) {
                    std::memcpy(key.data() + offset,
                                record.data() + column.offset, column.len);
                    offset += column.len;
                }
                std::string binary_key(key.data(), key.size());
                if (!batch_keys.insert(std::move(binary_key)).second) {
                    throw RMDBError("failure");
                }

                if (mvcc) {
                    RmRecord candidate(record_size,
                                       const_cast<char *>(record.data()));
                    context_->txn_mgr_->check_unique_key_conflict(
                        context_->txn_, work.handle->GetFd(), Rid{-1, -1},
                        candidate, index.cols);
                }
            }
            if (work.handle->contains_any_entries_batch(work.keys)) {
                throw RMDBError("failure");
            }
            indexes.push_back(std::move(work));
        }

        std::vector<PendingInsert> pending;
        pending.reserve(records.size());
        for (const std::vector<char> &record : records) {
            pending.push_back(PendingInsert{record.data(), record.size()});
        }
        std::vector<Rid> rids =
            file->insert_records(pending, context_, table_name_);

        for (IndexWork &index : indexes) {
            std::vector<std::pair<std::vector<char>, Rid>> entries;
            entries.reserve(rids.size());
            for (size_t row = 0; row < rids.size(); ++row) {
                entries.emplace_back(std::move(index.keys[row]), rids[row]);
            }
            index.handle->insert_entries_batch(std::move(entries),
                                               context_->txn_);
        }
    }

   private:
    SmManager *sm_manager_;
    std::string table_name_;
    std::vector<std::vector<Value>> value_rows_;
    Context *context_;
};
