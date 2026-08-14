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

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "defs.h"

using TempRowId = uint64_t;

enum class LogicalWriteKind { INSERT, UPDATE, DELETE };
enum class OverlayKind { ABSENT, VALUE, DELETED };

struct RecordIdentity {
    uint64_t file_id;
    Rid rid;

    friend bool operator==(const RecordIdentity &lhs, const RecordIdentity &rhs) {
        return lhs.file_id == rhs.file_id && lhs.rid == rhs.rid;
    }
};

struct StagedWrite {
    LogicalWriteKind kind;
    std::string table_name;
    uint64_t file_id;
    std::optional<Rid> rid;
    TempRowId temp_id;
    std::vector<char> before;
    std::vector<char> after;
};

class TransactionWriteBatch {
   public:
    TempRowId stage_insert(std::string table_name, uint64_t file_id,
                           std::vector<char> record);
    void stage_update(std::string table_name, uint64_t file_id, Rid rid,
                      std::vector<char> before, std::vector<char> after);
    void stage_delete(std::string table_name, uint64_t file_id, Rid rid,
                      std::vector<char> before);
    void stage_update(TempRowId temp_id, std::vector<char> after);
    void stage_delete(TempRowId temp_id);
    OverlayKind lookup(uint64_t file_id, Rid rid,
                       std::vector<char> *value) const;
    const std::vector<StagedWrite> &writes() const noexcept;
    std::vector<StagedWrite> freeze();
    void discard() noexcept;

   private:
    struct RecordIdentityHash {
        size_t operator()(const RecordIdentity &identity) const noexcept;
    };

    void ensure_active() const;
    void erase_write(size_t position);

    TempRowId next_temp_id_{1};
    bool active_{true};
    std::vector<StagedWrite> writes_;
    std::unordered_map<RecordIdentity, size_t, RecordIdentityHash> record_positions_;
    std::unordered_map<TempRowId, size_t> temp_positions_;
};
