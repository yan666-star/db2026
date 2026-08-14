/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>
#include <unordered_set>
#include <vector>

#include "bitmap.h"
#include "common/context.h"
#include "rm_defs.h"
#include "rm_record_pool.h"
#include "storage/page_guard.h"

class RmManager;

/** A parsed record page whose pin and shared page latch are owned by guard. */
class RmPageReadHandle {
   public:
    RmPageReadHandle() = default;
    RmPageReadHandle(const RmPageReadHandle &) = delete;
    RmPageReadHandle &operator=(const RmPageReadHandle &) = delete;
    RmPageReadHandle(RmPageReadHandle &&) noexcept = default;
    RmPageReadHandle &operator=(RmPageReadHandle &&) noexcept = default;

    RmPageReadHandle(const RmFileHdr *file_hdr, ReadPageGuard guard)
        : guard_(std::move(guard)), file_hdr_(file_hdr) {
        const char *data = guard_.data();
        page_hdr = reinterpret_cast<const RmPageHdr *>(
            data + Page::OFFSET_PAGE_HDR);
        bitmap = data + sizeof(RmPageHdr) + Page::OFFSET_PAGE_HDR;
        slots = bitmap + file_hdr_->bitmap_size;
    }

    const char *get_slot(int slot_no) const {
        return slots + slot_no * file_hdr_->record_size;
    }
    page_id_t page_no() const {
        return guard_.get_page()->get_page_id().page_no;
    }
    void drop() {
        guard_.drop();
        page_hdr = nullptr;
        bitmap = nullptr;
        slots = nullptr;
    }

    const RmPageHdr *page_hdr = nullptr;
    const char *bitmap = nullptr;
    const char *slots = nullptr;

   private:
    ReadPageGuard guard_;
    const RmFileHdr *file_hdr_ = nullptr;
};

/** A parsed record page whose pin and exclusive page latch are owned by guard. */
class RmPageWriteHandle {
   public:
    RmPageWriteHandle() = default;
    RmPageWriteHandle(const RmPageWriteHandle &) = delete;
    RmPageWriteHandle &operator=(const RmPageWriteHandle &) = delete;
    RmPageWriteHandle(RmPageWriteHandle &&) noexcept = default;
    RmPageWriteHandle &operator=(RmPageWriteHandle &&) noexcept = default;

    RmPageWriteHandle(const RmFileHdr *file_hdr, WritePageGuard guard)
        : guard_(std::move(guard)), file_hdr_(file_hdr) {
        char *data = guard_.data();
        page_hdr = reinterpret_cast<RmPageHdr *>(
            data + Page::OFFSET_PAGE_HDR);
        bitmap = data + sizeof(RmPageHdr) + Page::OFFSET_PAGE_HDR;
        slots = bitmap + file_hdr_->bitmap_size;
    }

    char *get_slot(int slot_no) const {
        return slots + slot_no * file_hdr_->record_size;
    }
    page_id_t page_no() const {
        return guard_.get_page()->get_page_id().page_no;
    }
    Page *page() { return guard_.get_page(); }
    void mark_dirty() { guard_.mark_dirty(); }
    void set_page_lsn(lsn_t page_lsn) {
        guard_.set_page_lsn(page_lsn, true);
    }
    void drop() {
        guard_.drop();
        page_hdr = nullptr;
        bitmap = nullptr;
        slots = nullptr;
    }

    RmPageHdr *page_hdr = nullptr;
    char *bitmap = nullptr;
    char *slots = nullptr;

   private:
    WritePageGuard guard_;
    const RmFileHdr *file_hdr_ = nullptr;
};

struct PendingInsert {
    const char *data;
    size_t size;
};

enum class HeapMutationKind { INSERT, UPDATE, DELETE };

struct HeapMutation {
    HeapMutationKind kind;
    Rid rid;
    std::vector<char> before;
    std::vector<char> after;
};

struct HeapPageMutationBatch {
    int fd;
    page_id_t page_no;
    std::vector<HeapMutation> mutations;
};

class RmFileHandle {
    friend class RmScan;
    friend class RmManager;

   private:
    DiskManager *disk_manager_;
    BufferPoolManager *buffer_pool_manager_;
    int fd_;
    inline static std::atomic<uint64_t> next_mvcc_file_id_{0};
    uint64_t mvcc_file_id_;
    RmFileHdr file_hdr_;

    // DML owns this latch in shared mode. Header/free-list persistence owns it
    // exclusively, so a temporarily leased non-full page cannot disappear
    // from the on-disk free-page chain.
    mutable std::shared_mutex lifecycle_latch_;
    mutable std::mutex allocation_latch_;
    mutable std::mutex free_pages_latch_;
    std::vector<page_id_t> free_page_candidates_;
    std::unordered_set<page_id_t> free_page_candidate_set_;
    std::mutex logical_update_latch_;
    mutable std::mutex reservation_latch_;
    std::unordered_set<uint64_t> reserved_insert_slots_;

   public:
    RmFileHandle(DiskManager *disk_manager,
                 BufferPoolManager *buffer_pool_manager, int fd);

    RmFileHdr get_file_hdr() const;
    int GetFd() const { return fd_; }
    uint64_t GetMvccFileId() const { return mvcc_file_id_; }
    std::unique_lock<std::mutex> acquire_logical_update_latch() {
        return std::unique_lock<std::mutex>(logical_update_latch_);
    }

    bool is_record(const Rid &rid) const;
    std::unique_ptr<RmRecord> get_record(const Rid &rid,
                                         Context *context) const;
    std::vector<std::unique_ptr<RmRecord>> batch_get_records(
        int page_no, std::vector<Rid> &rids, Context *context,
        RmRecordPool *record_pool = nullptr) const;
    bool read_next_page_batch(
        page_id_t page_no, std::vector<Rid> *rids,
        std::vector<std::unique_ptr<RmRecord>> *records,
        RmRecordPool *record_pool = nullptr) const;

    std::vector<Rid> lookup_int_equal_records(int offset, int value);
    std::vector<Rid> all_record_slots();

    Rid insert_record(char *buf, Context *context);
    Rid insert_record(char *buf, Context *context,
                      const std::string &table_name);
    std::vector<Rid> insert_records(
        const std::vector<PendingInsert> &records, Context *context,
        const std::string &table_name);
    std::vector<Rid> reserve_insert_slots(size_t count);
    void apply_reserved_inserts(
        const std::vector<std::pair<Rid, std::vector<char>>> &records,
        lsn_t page_lsn = INVALID_LSN);
    void apply_page_batch(const HeapPageMutationBatch &batch,
                          lsn_t page_lsn);
    void release_reserved_slots(const std::vector<Rid> &rids) noexcept;
    void insert_record(const Rid &rid, char *buf);
    void delete_record(const Rid &rid, Context *context,
                       lsn_t page_lsn = INVALID_LSN);
    void update_record(const Rid &rid, char *buf, Context *context,
                       lsn_t page_lsn = INVALID_LSN);

    bool record_exists(const Rid &rid) const;
    void upsert_record_for_recovery(const Rid &rid, const char *buf);
    void delete_record_for_recovery(const Rid &rid);
    void rebuild_free_page_list();
    void flush_file_header();

    RmPageReadHandle fetch_page_read(int page_no) const;
    RmPageWriteHandle fetch_page_write(int page_no) const;

   private:
    Rid insert_record_internal(char *buf, Context *context,
                               const std::string *table_name);
    RmPageWriteHandle acquire_insert_page();
    RmPageWriteHandle create_new_page_handle();
    void ensure_page_exists(int page_no);

    int page_count() const;
    void initialize_free_page_candidates();
    page_id_t pop_free_page_candidate();
    void add_free_page_candidate(page_id_t page_no);
    void remove_free_page_candidate(page_id_t page_no);
    void rebuild_persisted_free_list_locked();
    static uint64_t encode_reserved_slot(const Rid &rid);
};
