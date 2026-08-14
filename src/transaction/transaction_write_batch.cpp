/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You may obtain a copy of the Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "transaction/transaction_write_batch.h"

#include <algorithm>
#include <functional>
#include <utility>

#include "errors.h"

size_t TransactionWriteBatch::RecordIdentityHash::operator()(
    const RecordIdentity &identity) const noexcept {
    const size_t file_hash = std::hash<uint64_t>{}(identity.file_id);
    const size_t page_hash = std::hash<int>{}(identity.rid.page_no);
    const size_t slot_hash = std::hash<int>{}(identity.rid.slot_no);
    return file_hash ^ (page_hash << 1) ^ (slot_hash << 2);
}

TempRowId TransactionWriteBatch::stage_insert(std::string table_name,
                                              uint64_t file_id,
                                              std::vector<char> record) {
    ensure_active();
    const TempRowId temp_id = next_temp_id_++;
    temp_positions_.emplace(temp_id, writes_.size());
    writes_.push_back({LogicalWriteKind::INSERT, std::move(table_name), file_id,
                       std::nullopt, temp_id, {}, std::move(record)});
    return temp_id;
}

void TransactionWriteBatch::stage_update(std::string table_name, uint64_t file_id,
                                         Rid rid, std::vector<char> before,
                                         std::vector<char> after) {
    ensure_active();
    const RecordIdentity identity{file_id, rid};
    const auto found = record_positions_.find(identity);
    if (found == record_positions_.end()) {
        record_positions_.emplace(identity, writes_.size());
        writes_.push_back({LogicalWriteKind::UPDATE, std::move(table_name), file_id,
                           rid, 0, std::move(before), std::move(after)});
        return;
    }

    StagedWrite &write = writes_[found->second];
    if (write.kind == LogicalWriteKind::DELETE) {
        write.kind = LogicalWriteKind::UPDATE;
    }
    write.after = std::move(after);
}

void TransactionWriteBatch::stage_delete(std::string table_name, uint64_t file_id,
                                         Rid rid, std::vector<char> before) {
    ensure_active();
    const RecordIdentity identity{file_id, rid};
    const auto found = record_positions_.find(identity);
    if (found == record_positions_.end()) {
        record_positions_.emplace(identity, writes_.size());
        writes_.push_back({LogicalWriteKind::DELETE, std::move(table_name), file_id,
                           rid, 0, std::move(before), {}});
        return;
    }

    StagedWrite &write = writes_[found->second];
    if (write.kind == LogicalWriteKind::UPDATE) {
        write.kind = LogicalWriteKind::DELETE;
        write.after.clear();
    }
}

void TransactionWriteBatch::stage_update(TempRowId temp_id,
                                         std::vector<char> after) {
    ensure_active();
    const auto found = temp_positions_.find(temp_id);
    if (found == temp_positions_.end()) {
        throw InternalError("Unknown transaction-local row id");
    }
    writes_[found->second].after = std::move(after);
}

void TransactionWriteBatch::stage_delete(TempRowId temp_id) {
    ensure_active();
    const auto found = temp_positions_.find(temp_id);
    if (found == temp_positions_.end()) {
        throw InternalError("Unknown transaction-local row id");
    }
    erase_write(found->second);
}

OverlayKind TransactionWriteBatch::lookup(uint64_t file_id, Rid rid,
                                          std::vector<char> *value) const {
    const auto found = record_positions_.find(RecordIdentity{file_id, rid});
    if (found == record_positions_.end()) {
        return OverlayKind::ABSENT;
    }

    const StagedWrite &write = writes_[found->second];
    if (write.kind == LogicalWriteKind::DELETE) {
        return OverlayKind::DELETED;
    }
    if (value != nullptr) {
        *value = write.after;
    }
    return OverlayKind::VALUE;
}

const std::vector<StagedWrite> &TransactionWriteBatch::writes() const noexcept {
    return writes_;
}

std::vector<StagedWrite> TransactionWriteBatch::freeze() {
    ensure_active();
    const auto first_insert = std::stable_partition(
        writes_.begin(), writes_.end(),
        [](const StagedWrite &write) { return write.rid.has_value(); });
    std::stable_sort(writes_.begin(), first_insert,
                     [](const StagedWrite &lhs, const StagedWrite &rhs) {
                         if (lhs.file_id != rhs.file_id) {
                             return lhs.file_id < rhs.file_id;
                         }
                         if (lhs.rid->page_no != rhs.rid->page_no) {
                             return lhs.rid->page_no < rhs.rid->page_no;
                         }
                         return lhs.rid->slot_no < rhs.rid->slot_no;
                     });
    std::stable_sort(first_insert, writes_.end(),
                     [](const StagedWrite &lhs, const StagedWrite &rhs) {
                         return lhs.temp_id < rhs.temp_id;
                     });

    active_ = false;
    record_positions_.clear();
    temp_positions_.clear();
    return std::move(writes_);
}

void TransactionWriteBatch::discard() noexcept {
    active_ = false;
    writes_.clear();
    record_positions_.clear();
    temp_positions_.clear();
}

void TransactionWriteBatch::ensure_active() const {
    if (!active_) {
        throw InternalError("Transaction write batch is no longer active");
    }
}

void TransactionWriteBatch::erase_write(size_t position) {
    const StagedWrite &write = writes_[position];
    if (write.rid.has_value()) {
        record_positions_.erase(RecordIdentity{write.file_id, *write.rid});
    } else {
        temp_positions_.erase(write.temp_id);
    }
    writes_.erase(writes_.begin() + position);
    for (size_t index = position; index < writes_.size(); ++index) {
        const StagedWrite &remaining = writes_[index];
        if (remaining.rid.has_value()) {
            record_positions_[RecordIdentity{remaining.file_id, *remaining.rid}] = index;
        } else {
            temp_positions_[remaining.temp_id] = index;
        }
    }
}
