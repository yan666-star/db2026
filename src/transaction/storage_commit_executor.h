#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "common/config.h"
#include "defs.h"
#include "transaction/transaction_write_batch.h"

class Context;
class RmFileHandle;
class SmManager;
class Transaction;
class TransactionManager;

struct ReservedInsert {
    TempRowId temp_id;
    Rid rid;
    std::vector<char> record;
};

struct PreparedStorageCommit {
    std::vector<StagedWrite> writes;
    std::vector<ReservedInsert> inserts;
    std::unordered_map<TempRowId, Rid> resolved_insert_rids;
    lsn_t greatest_row_lsn = INVALID_LSN;
    bool physical_apply_started = false;
};

class HeapSlotReservation {
   public:
    explicit HeapSlotReservation(RmFileHandle *file) : file_(file) {}
    ~HeapSlotReservation();

    HeapSlotReservation(const HeapSlotReservation &) = delete;
    HeapSlotReservation &operator=(const HeapSlotReservation &) = delete;

    std::vector<ReservedInsert> reserve(
        const std::vector<StagedWrite> &inserts);
    void consume() noexcept { reserved_rids_.clear(); }
    void release() noexcept;

   private:
    RmFileHandle *file_;
    std::vector<Rid> reserved_rids_;
};

class StorageCommitExecutor {
   public:
    StorageCommitExecutor(SmManager *sm_manager,
                          TransactionManager *transaction_manager)
        : sm_manager_(sm_manager), transaction_manager_(transaction_manager) {}

    PreparedStorageCommit prepare(Transaction *txn);
    void apply(PreparedStorageCommit *commit, Context *context);
    void rollback(PreparedStorageCommit *commit, Context *context) noexcept;

   private:
    SmManager *sm_manager_;
    TransactionManager *transaction_manager_;
    std::vector<std::unique_ptr<HeapSlotReservation>> reservations_;
};
