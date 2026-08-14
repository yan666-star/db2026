/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#include "rm_file_handle.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_set>

#include "common/perf_counters.h"
#include "errors.h"
#include "recovery/log_manager.h"
#include "transaction/transaction_manager.h"

namespace {

int read_int_key(const char *record, int offset) {
    int key = 0;
    std::memcpy(&key, record + offset, sizeof(key));
    return key;
}

}  // namespace

uint64_t RmFileHandle::encode_reserved_slot(const Rid &rid) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(rid.page_no)) << 32) |
           static_cast<uint32_t>(rid.slot_no);
}

RmFileHandle::RmFileHandle(DiskManager *disk_manager,
                           BufferPoolManager *buffer_pool_manager, int fd)
    : disk_manager_(disk_manager),
      buffer_pool_manager_(buffer_pool_manager),
      fd_(fd),
      mvcc_file_id_(next_mvcc_file_id_.fetch_add(1)) {
    disk_manager_->read_page(fd_, RM_FILE_HDR_PAGE,
                             reinterpret_cast<char *>(&file_hdr_),
                             sizeof(file_hdr_));
    disk_manager_->set_fd2pageno(fd_, file_hdr_.num_pages);
    initialize_free_page_candidates();
}

RmFileHdr RmFileHandle::get_file_hdr() const {
    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_latch_);
    std::scoped_lock metadata(allocation_latch_, free_pages_latch_);
    return file_hdr_;
}

int RmFileHandle::page_count() const {
    std::lock_guard<std::mutex> lock(allocation_latch_);
    return file_hdr_.num_pages;
}

void RmFileHandle::initialize_free_page_candidates() {
    std::unordered_set<page_id_t> visited;
    page_id_t page_no = file_hdr_.first_free_page_no;
    std::array<char, PAGE_SIZE> page{};
    while (page_no >= RM_FIRST_RECORD_PAGE &&
           page_no < file_hdr_.num_pages && visited.insert(page_no).second) {
        disk_manager_->read_page(fd_, page_no, page.data(), PAGE_SIZE);
        const auto *page_hdr = reinterpret_cast<const RmPageHdr *>(
            page.data() + Page::OFFSET_PAGE_HDR);
        if (page_hdr->num_records < file_hdr_.num_records_per_page) {
            free_page_candidates_.push_back(page_no);
            free_page_candidate_set_.insert(page_no);
        }
        page_no = page_hdr->next_free_page_no;
    }
    file_hdr_.first_free_page_no = free_page_candidates_.empty()
                                            ? RM_NO_PAGE
                                            : free_page_candidates_.back();
}

page_id_t RmFileHandle::pop_free_page_candidate() {
    std::lock_guard<std::mutex> lock(free_pages_latch_);
    if (free_page_candidates_.empty()) {
        file_hdr_.first_free_page_no = RM_NO_PAGE;
        return RM_NO_PAGE;
    }
    page_id_t page_no = free_page_candidates_.back();
    free_page_candidates_.pop_back();
    free_page_candidate_set_.erase(page_no);
    file_hdr_.first_free_page_no = free_page_candidates_.empty()
                                            ? RM_NO_PAGE
                                            : free_page_candidates_.back();
    return page_no;
}

void RmFileHandle::add_free_page_candidate(page_id_t page_no) {
    std::lock_guard<std::mutex> lock(free_pages_latch_);
    if (free_page_candidate_set_.insert(page_no).second) {
        free_page_candidates_.push_back(page_no);
    }
    file_hdr_.first_free_page_no = free_page_candidates_.empty()
                                            ? RM_NO_PAGE
                                            : free_page_candidates_.back();
}

void RmFileHandle::remove_free_page_candidate(page_id_t page_no) {
    std::lock_guard<std::mutex> lock(free_pages_latch_);
    if (free_page_candidate_set_.erase(page_no) == 0) {
        return;
    }
    auto it = std::find(free_page_candidates_.begin(),
                        free_page_candidates_.end(), page_no);
    if (it != free_page_candidates_.end()) {
        *it = free_page_candidates_.back();
        free_page_candidates_.pop_back();
    }
    file_hdr_.first_free_page_no = free_page_candidates_.empty()
                                            ? RM_NO_PAGE
                                            : free_page_candidates_.back();
}

RmPageReadHandle RmFileHandle::fetch_page_read(int page_no) const {
    if (page_no < RM_FIRST_RECORD_PAGE || page_no >= page_count()) {
        throw PageNotExistError(std::to_string(fd_), page_no);
    }
    ReadPageGuard guard =
        buffer_pool_manager_->fetch_page_read(PageId{fd_, page_no});
    if (!guard.is_valid()) {
        throw InternalError("Failed to fetch record page for read");
    }
    return RmPageReadHandle(&file_hdr_, std::move(guard));
}

RmPageWriteHandle RmFileHandle::fetch_page_write(int page_no) const {
    if (page_no < RM_FIRST_RECORD_PAGE || page_no >= page_count()) {
        throw PageNotExistError(std::to_string(fd_), page_no);
    }
    if (rmdb_perf::enabled()) {
        rmdb_perf::shared_counters().heap_page_write_guards.fetch_add(
            1, std::memory_order_relaxed);
    }
    WritePageGuard guard =
        buffer_pool_manager_->fetch_page_write(PageId{fd_, page_no});
    if (!guard.is_valid()) {
        throw InternalError("Failed to fetch record page for write");
    }
    return RmPageWriteHandle(&file_hdr_, std::move(guard));
}

bool RmFileHandle::is_record(const Rid &rid) const {
    return record_exists(rid);
}

std::unique_ptr<RmRecord> RmFileHandle::get_record(
    const Rid &rid, Context *context) const {
    RmPageReadHandle page = fetch_page_read(rid.page_no);
    bool exists = rid.slot_no >= 0 &&
                  rid.slot_no < file_hdr_.num_records_per_page &&
                  Bitmap::is_set(page.bitmap, rid.slot_no);
    std::unique_ptr<RmRecord> physical_record;
    if (exists) {
        physical_record = std::make_unique<RmRecord>(file_hdr_.record_size);
        std::memcpy(physical_record->data, page.get_slot(rid.slot_no),
                    file_hdr_.record_size);
    }
    page.drop();

    if (context != nullptr && context->txn_mgr_ != nullptr &&
        context->txn_mgr_->uses_mvcc(context->txn_)) {
        return context->txn_mgr_->get_visible_record(
            context->txn_, mvcc_file_id_, rid, std::move(physical_record));
    }
    if (context != nullptr && context->txn_mgr_ != nullptr) {
        return context->txn_mgr_->get_latest_committed_record(
            mvcc_file_id_, rid, std::move(physical_record));
    }
    if (!exists) {
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }
    return physical_record;
}

std::vector<std::unique_ptr<RmRecord>> RmFileHandle::batch_get_records(
    int page_no, std::vector<Rid> &rids, Context *context,
    RmRecordPool *record_pool) const {
    std::vector<std::unique_ptr<RmRecord>> records;
    if (rids.empty()) {
        return records;
    }

    RmPageReadHandle page = fetch_page_read(page_no);
    std::vector<Rid> valid_rids;
    valid_rids.reserve(rids.size());
    records.reserve(rids.size());
    for (const Rid &rid : rids) {
        if (rid.page_no != page_no || rid.slot_no < 0 ||
            rid.slot_no >= file_hdr_.num_records_per_page ||
            !Bitmap::is_set(page.bitmap, rid.slot_no)) {
            continue;
        }
        auto record = record_pool == nullptr
                          ? std::make_unique<RmRecord>(file_hdr_.record_size)
                          : record_pool->acquire(file_hdr_.record_size);
        std::memcpy(record->data, page.get_slot(rid.slot_no),
                    file_hdr_.record_size);
        records.push_back(std::move(record));
        valid_rids.push_back(rid);
    }
    page.drop();

    if (context != nullptr && context->txn_mgr_ != nullptr) {
        context->txn_mgr_->filter_visible_records(
            context->txn_, mvcc_file_id_, valid_rids, records);
    }
    rids = std::move(valid_rids);
    return records;
}

bool RmFileHandle::read_next_page_batch(
    page_id_t page_no, std::vector<Rid> *rids,
    std::vector<std::unique_ptr<RmRecord>> *records,
    RmRecordPool *record_pool) const {
    if (rids == nullptr || records == nullptr) {
        throw InternalError("Heap page batch output must not be null");
    }
    rids->clear();
    records->clear();
    if (page_no < RM_FIRST_RECORD_PAGE || page_no >= page_count()) {
        return false;
    }

    RmPageReadHandle page = fetch_page_read(page_no);
    rids->reserve(page.page_hdr->num_records);
    records->reserve(page.page_hdr->num_records);
    int slot_no = -1;
    while ((slot_no = Bitmap::next_bit(
                true, page.bitmap, file_hdr_.num_records_per_page,
                slot_no)) < file_hdr_.num_records_per_page) {
        rids->push_back(Rid{page_no, slot_no});
        auto record = record_pool == nullptr
                          ? std::make_unique<RmRecord>(file_hdr_.record_size)
                          : record_pool->acquire(file_hdr_.record_size);
        std::memcpy(record->data, page.get_slot(slot_no),
                    file_hdr_.record_size);
        records->push_back(std::move(record));
    }
    return true;
}

std::vector<Rid> RmFileHandle::all_record_slots() {
    const int num_pages = page_count();
    std::vector<Rid> slots;
    if (num_pages <= RM_FIRST_RECORD_PAGE) {
        return slots;
    }
    slots.reserve(static_cast<size_t>(num_pages - RM_FIRST_RECORD_PAGE) *
                  file_hdr_.num_records_per_page);
    for (int page_no = RM_FIRST_RECORD_PAGE; page_no < num_pages; ++page_no) {
        for (int slot_no = 0; slot_no < file_hdr_.num_records_per_page;
             ++slot_no) {
            slots.push_back(Rid{page_no, slot_no});
        }
    }
    return slots;
}

std::vector<Rid> RmFileHandle::lookup_int_equal_records(int offset,
                                                        int value) {
    std::vector<Rid> result;
    if (offset < 0 || offset + static_cast<int>(sizeof(int)) >
                          file_hdr_.record_size) {
        return result;
    }
    const int num_pages = page_count();
    for (int page_no = RM_FIRST_RECORD_PAGE; page_no < num_pages; ++page_no) {
        RmPageReadHandle page = fetch_page_read(page_no);
        int slot_no = -1;
        while ((slot_no = Bitmap::next_bit(
                    true, page.bitmap, file_hdr_.num_records_per_page,
                    slot_no)) < file_hdr_.num_records_per_page) {
            if (read_int_key(page.get_slot(slot_no), offset) == value) {
                result.push_back(Rid{page_no, slot_no});
            }
        }
    }
    return result;
}

RmPageWriteHandle RmFileHandle::create_new_page_handle() {
    PageId page_id{fd_, INVALID_PAGE_ID};
    WritePageGuard guard = buffer_pool_manager_->new_page_guarded(&page_id);
    if (!guard.is_valid()) {
        throw InternalError("Failed to allocate record page");
    }
    RmPageWriteHandle page(&file_hdr_, std::move(guard));
    page.page_hdr->next_free_page_no = RM_NO_PAGE;
    page.page_hdr->num_records = 0;
    Bitmap::init(page.bitmap, file_hdr_.bitmap_size);
    page.mark_dirty();
    file_hdr_.num_pages = std::max(file_hdr_.num_pages, page_id.page_no + 1);
    return page;
}

RmPageWriteHandle RmFileHandle::acquire_insert_page() {
    for (;;) {
        page_id_t page_no = pop_free_page_candidate();
        if (page_no != RM_NO_PAGE) {
            RmPageWriteHandle page = fetch_page_write(page_no);
            if (page.page_hdr->num_records < file_hdr_.num_records_per_page) {
                return page;
            }
            page.page_hdr->next_free_page_no = RM_NO_PAGE;
            page.mark_dirty();
            continue;
        }

        std::unique_lock<std::mutex> allocation(allocation_latch_);
        page_no = pop_free_page_candidate();
        if (page_no != RM_NO_PAGE) {
            allocation.unlock();
            RmPageWriteHandle page = fetch_page_write(page_no);
            if (page.page_hdr->num_records < file_hdr_.num_records_per_page) {
                return page;
            }
            page.page_hdr->next_free_page_no = RM_NO_PAGE;
            page.mark_dirty();
            continue;
        }
        return create_new_page_handle();
    }
}

Rid RmFileHandle::insert_record(char *buf, Context *context) {
    return insert_record_internal(buf, context, nullptr);
}

Rid RmFileHandle::insert_record(char *buf, Context *context,
                                const std::string &table_name) {
    return insert_record_internal(buf, context, &table_name);
}

Rid RmFileHandle::insert_record_internal(
    char *buf, Context *context, const std::string *table_name) {
    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_latch_);
    for (;;) {
        RmPageWriteHandle page = acquire_insert_page();
        const int page_no = page.page_no();
        bool reusable = true;
        lsn_t change_lsn = INVALID_LSN;
        try {
            int slot_no = -1;
            {
                std::lock_guard<std::mutex> reservations(reservation_latch_);
                while ((slot_no = Bitmap::next_bit(
                            false, page.bitmap,
                            file_hdr_.num_records_per_page, slot_no)) <
                       file_hdr_.num_records_per_page) {
                    if (reserved_insert_slots_.count(encode_reserved_slot(
                            Rid{page_no, slot_no})) == 0) {
                        break;
                    }
                }
            }
            if (slot_no >= file_hdr_.num_records_per_page) {
                page.drop();
                remove_free_page_candidate(page_no);
                continue;
            }
            const Rid rid{page_no, slot_no};

            if (context != nullptr && context->txn_mgr_ != nullptr &&
                context->txn_mgr_->uses_mvcc(context->txn_) &&
                table_name != nullptr) {
                RmRecord record(file_hdr_.record_size, buf);
                context->txn_mgr_->prepare_insert(
                    context->txn_, mvcc_file_id_, rid, record);
            }

            if (context != nullptr && context->txn_ != nullptr &&
                context->log_mgr_ != nullptr && table_name != nullptr) {
                RmRecord record(file_hdr_.record_size, buf);
                InsertLogRecord log_record(context->txn_->get_transaction_id(),
                                           record, rid, *table_name);
                log_record.prev_lsn_ = context->txn_->get_prev_lsn();
                change_lsn = context->log_mgr_->add_log_to_buffer(&log_record);
                context->txn_->set_prev_lsn(change_lsn);
            }

            Bitmap::set(page.bitmap, slot_no);
            std::memcpy(page.get_slot(slot_no), buf, file_hdr_.record_size);
            page.page_hdr->num_records++;
            reusable =
                page.page_hdr->num_records < file_hdr_.num_records_per_page;
            if (!reusable) {
                page.page_hdr->next_free_page_no = RM_NO_PAGE;
            }
            if (change_lsn != INVALID_LSN) {
                page.set_page_lsn(change_lsn);
            }
            page.mark_dirty();
            page.drop();
            if (reusable) {
                add_free_page_candidate(page_no);
            }
            return rid;
        } catch (...) {
            reusable = page.page_hdr != nullptr &&
                       page.page_hdr->num_records <
                           file_hdr_.num_records_per_page;
            page.drop();
            if (reusable) {
                add_free_page_candidate(page_no);
            }
            throw;
        }
    }
}

std::vector<Rid> RmFileHandle::insert_records(
    const std::vector<PendingInsert> &records, Context *context,
    const std::string &table_name) {
    for (const PendingInsert &record : records) {
        if (record.data == nullptr ||
            record.size != static_cast<size_t>(file_hdr_.record_size)) {
            throw InvalidRecordSizeError(static_cast<int>(record.size));
        }
    }

    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_latch_);
    std::vector<Rid> result;
    result.reserve(records.size());
    size_t record_index = 0;
    while (record_index < records.size()) {
        RmPageWriteHandle page = acquire_insert_page();
        const int page_no = page.page_no();
        bool reusable = true;
        lsn_t page_change_lsn = INVALID_LSN;
        try {
            std::vector<Rid> page_rids;
            page_rids.reserve(static_cast<size_t>(
                file_hdr_.num_records_per_page -
                page.page_hdr->num_records));
            {
                std::lock_guard<std::mutex> reservations(reservation_latch_);
                int slot_no = -1;
                while (record_index + page_rids.size() < records.size() &&
                       (slot_no = Bitmap::next_bit(
                            false, page.bitmap,
                            file_hdr_.num_records_per_page, slot_no)) <
                           file_hdr_.num_records_per_page) {
                    Rid candidate{page_no, slot_no};
                    if (reserved_insert_slots_.count(
                            encode_reserved_slot(candidate)) == 0) {
                        page_rids.push_back(candidate);
                    }
                }
            }
            if (page_rids.empty()) {
                page.drop();
                remove_free_page_candidate(page_no);
                continue;
            }

            if (context != nullptr && context->txn_mgr_ != nullptr &&
                context->txn_mgr_->uses_mvcc(context->txn_)) {
                std::vector<RmRecord> page_records(page_rids.size());
                for (size_t row = 0; row < page_rids.size(); ++row) {
                    const PendingInsert &pending =
                        records[record_index + row];
                    // A non-owning RmRecord view is sufficient: prepare_inserts
                    // copies every version while the batch call is active.
                    page_records[row].data =
                        const_cast<char *>(pending.data);
                    page_records[row].size = file_hdr_.record_size;
                }
                context->txn_mgr_->prepare_inserts(
                    context->txn_, mvcc_file_id_, page_rids, page_records);
            }

            if (context != nullptr && context->txn_ != nullptr &&
                context->log_mgr_ != nullptr) {
                std::vector<std::unique_ptr<InsertLogRecord>> owned_logs;
                std::vector<LogRecord *> logs;
                owned_logs.reserve(page_rids.size());
                logs.reserve(page_rids.size());
                for (size_t row = 0; row < page_rids.size(); ++row) {
                    const PendingInsert &pending =
                        records[record_index + row];
                    RmRecord record(
                        file_hdr_.record_size,
                        const_cast<char *>(pending.data));
                    auto log_record = std::make_unique<InsertLogRecord>(
                        context->txn_->get_transaction_id(), record,
                        page_rids[row], table_name);
                    log_record->prev_lsn_ =
                        row == 0 ? context->txn_->get_prev_lsn()
                                 : INVALID_LSN;
                    logs.push_back(log_record.get());
                    owned_logs.push_back(std::move(log_record));
                }
                std::vector<lsn_t> lsns =
                    context->log_mgr_->add_logs_to_buffer(logs);
                if (!lsns.empty()) {
                    page_change_lsn = lsns.back();
                    context->txn_->set_prev_lsn(page_change_lsn);
                }
            }

            // Register undo before exposing any slot on this page. If a later
            // heap page or index batch fails, transaction abort can remove
            // every already-written row. rollback_insert is idempotent for a
            // RID whose bitmap bit was never set, so pre-registration is also
            // safe if an exception occurs before the physical copy loop.
            if (context != nullptr && context->txn_ != nullptr) {
                if (context->txn_mgr_ != nullptr &&
                    context->txn_mgr_->uses_mvcc(context->txn_)) {
                    for (const Rid &rid : page_rids) {
                        context->txn_->append_write_record(new WriteRecord(
                            WType::INSERT_TUPLE, table_name, rid));
                    }
                } else {
                    auto *undo = new WriteRecord(
                        WType::BULK_INSERT_TUPLES, table_name);
                    for (const Rid &rid : page_rids) {
                        undo->AppendRid(rid);
                    }
                    context->txn_->append_write_record(undo);
                }
            }

            for (size_t row = 0; row < page_rids.size(); ++row) {
                const PendingInsert &pending = records[record_index + row];
                Bitmap::set(page.bitmap, page_rids[row].slot_no);
                std::memcpy(page.get_slot(page_rids[row].slot_no),
                            pending.data,
                            file_hdr_.record_size);
                page.page_hdr->num_records++;
                result.push_back(page_rids[row]);
            }
            record_index += page_rids.size();
            reusable = page.page_hdr->num_records <
                       file_hdr_.num_records_per_page;
            if (!reusable) {
                page.page_hdr->next_free_page_no = RM_NO_PAGE;
            }
            if (page_change_lsn != INVALID_LSN) {
                page.set_page_lsn(page_change_lsn);
            }
            page.mark_dirty();
            page.drop();
            if (reusable) {
                add_free_page_candidate(page_no);
            }
        } catch (...) {
            reusable = page.page_hdr != nullptr &&
                       page.page_hdr->num_records <
                           file_hdr_.num_records_per_page;
            page.drop();
            if (reusable) {
                add_free_page_candidate(page_no);
            }
            throw;
        }
    }
    return result;
}

std::vector<Rid> RmFileHandle::reserve_insert_slots(size_t count) {
    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_latch_);
    std::vector<Rid> reserved;
    reserved.reserve(count);
    try {
        const int existing_pages = page_count();
        for (int page_no = RM_FIRST_RECORD_PAGE;
             page_no < existing_pages && reserved.size() < count; ++page_no) {
            RmPageReadHandle page = fetch_page_read(page_no);
            std::lock_guard<std::mutex> reservation_guard(reservation_latch_);
            int slot_no = -1;
            while (reserved.size() < count &&
                   (slot_no = Bitmap::next_bit(
                        false, page.bitmap, file_hdr_.num_records_per_page,
                        slot_no)) < file_hdr_.num_records_per_page) {
                Rid rid{page_no, slot_no};
                if (reserved_insert_slots_.insert(
                        encode_reserved_slot(rid)).second) {
                    reserved.push_back(rid);
                }
            }
        }

        // Future page identities are reservation metadata only. Holding the
        // allocation latch prevents a direct inserter from materializing the
        // candidate page before its reserved slots are registered.
        {
            std::scoped_lock future_slots(allocation_latch_,
                                          reservation_latch_);
            int page_no = file_hdr_.num_pages;
            while (reserved.size() < count) {
                for (int slot_no = 0;
                     slot_no < file_hdr_.num_records_per_page &&
                     reserved.size() < count;
                     ++slot_no) {
                    Rid rid{page_no, slot_no};
                    if (reserved_insert_slots_.insert(
                            encode_reserved_slot(rid)).second) {
                        reserved.push_back(rid);
                    }
                }
                ++page_no;
            }
        }
        return reserved;
    } catch (...) {
        release_reserved_slots(reserved);
        throw;
    }
}

void RmFileHandle::apply_reserved_inserts(
    const std::vector<std::pair<Rid, std::vector<char>>> &records,
    lsn_t page_lsn) {
    std::map<page_id_t,
             std::vector<const std::pair<Rid, std::vector<char>> *>>
        by_page;
    for (const auto &record : records) {
        if (record.second.size() !=
            static_cast<size_t>(file_hdr_.record_size)) {
            throw InvalidRecordSizeError(
                static_cast<int>(record.second.size()));
        }
        by_page[record.first.page_no].push_back(&record);
    }

    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_latch_);
    if (!by_page.empty()) {
        ensure_page_exists(by_page.rbegin()->first);
    }
    for (auto &[page_no, page_records] : by_page) {
        RmPageWriteHandle page = fetch_page_write(page_no);
        bool reusable = true;
        {
            std::lock_guard<std::mutex> reservation_guard(reservation_latch_);
            for (const auto *record : page_records) {
                const Rid &rid = record->first;
                if (rid.slot_no < 0 ||
                    rid.slot_no >= file_hdr_.num_records_per_page ||
                    Bitmap::is_set(page.bitmap, rid.slot_no) ||
                    reserved_insert_slots_.count(
                        encode_reserved_slot(rid)) == 0) {
                    throw InternalError("Reserved Heap slot was reused");
                }
            }
            for (const auto *record : page_records) {
                const Rid &rid = record->first;
                Bitmap::set(page.bitmap, rid.slot_no);
                std::memcpy(page.get_slot(rid.slot_no),
                            record->second.data(), file_hdr_.record_size);
                page.page_hdr->num_records++;
                reserved_insert_slots_.erase(encode_reserved_slot(rid));
            }
            reusable = page.page_hdr->num_records <
                       file_hdr_.num_records_per_page;
        }
        if (!reusable) {
            page.page_hdr->next_free_page_no = RM_NO_PAGE;
        }
        if (page_lsn != INVALID_LSN) {
            page.set_page_lsn(page_lsn);
        }
        page.mark_dirty();
        page.drop();
        if (reusable) {
            add_free_page_candidate(page_no);
        } else {
            remove_free_page_candidate(page_no);
        }
    }
}

void RmFileHandle::apply_page_batch(const HeapPageMutationBatch &batch,
                                    lsn_t page_lsn) {
    if (batch.fd != fd_ || batch.page_no < RM_FIRST_RECORD_PAGE) {
        throw InternalError("Heap page batch targets the wrong file or page");
    }
    if (batch.mutations.empty()) {
        return;
    }
    for (const HeapMutation &mutation : batch.mutations) {
        if (mutation.rid.page_no != batch.page_no ||
            mutation.rid.slot_no < 0 ||
            mutation.rid.slot_no >= file_hdr_.num_records_per_page) {
            throw InternalError("Heap page batch contains an invalid RID");
        }
        if ((mutation.kind == HeapMutationKind::INSERT ||
             mutation.kind == HeapMutationKind::UPDATE) &&
            mutation.after.size() !=
                static_cast<size_t>(file_hdr_.record_size)) {
            throw InvalidRecordSizeError(
                static_cast<int>(mutation.after.size()));
        }
        if (mutation.kind != HeapMutationKind::INSERT &&
            mutation.before.size() !=
                static_cast<size_t>(file_hdr_.record_size)) {
            throw InvalidRecordSizeError(
                static_cast<int>(mutation.before.size()));
        }
    }

    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_latch_);
    ensure_page_exists(batch.page_no);
    RmPageWriteHandle page = fetch_page_write(batch.page_no);
    bool reusable = true;
    {
        std::lock_guard<std::mutex> reservations(reservation_latch_);
        std::unordered_set<int> slots;
        for (const HeapMutation &mutation : batch.mutations) {
            if (!slots.insert(mutation.rid.slot_no).second) {
                throw InternalError("Heap page batch repeats a slot");
            }
            const bool exists =
                Bitmap::is_set(page.bitmap, mutation.rid.slot_no);
            if (mutation.kind == HeapMutationKind::INSERT) {
                if (exists || reserved_insert_slots_.count(
                                  encode_reserved_slot(mutation.rid)) == 0) {
                    throw InternalError("Reserved Heap slot was reused");
                }
            } else if (!exists ||
                       std::memcmp(page.get_slot(mutation.rid.slot_no),
                                   mutation.before.data(),
                                   file_hdr_.record_size) != 0) {
                throw InternalError("Heap page batch before image changed");
            }
        }

        // No page byte is changed until every mutation above is validated.
        for (const HeapMutation &mutation : batch.mutations) {
            char *slot = page.get_slot(mutation.rid.slot_no);
            if (mutation.kind == HeapMutationKind::INSERT) {
                Bitmap::set(page.bitmap, mutation.rid.slot_no);
                std::memcpy(slot, mutation.after.data(), file_hdr_.record_size);
                ++page.page_hdr->num_records;
                reserved_insert_slots_.erase(
                    encode_reserved_slot(mutation.rid));
            } else if (mutation.kind == HeapMutationKind::UPDATE) {
                std::memcpy(slot, mutation.after.data(), file_hdr_.record_size);
            } else {
                Bitmap::reset(page.bitmap, mutation.rid.slot_no);
                --page.page_hdr->num_records;
            }
        }
        reusable = page.page_hdr->num_records <
                   file_hdr_.num_records_per_page;
        if (!reusable) {
            page.page_hdr->next_free_page_no = RM_NO_PAGE;
        }
    }
    if (page_lsn != INVALID_LSN) {
        page.set_page_lsn(page_lsn);
    }
    page.mark_dirty();
    page.drop();
    if (reusable) {
        add_free_page_candidate(batch.page_no);
    } else {
        remove_free_page_candidate(batch.page_no);
    }
}

void RmFileHandle::release_reserved_slots(
    const std::vector<Rid> &rids) noexcept {
    {
        std::lock_guard<std::mutex> reservation_guard(reservation_latch_);
        for (const Rid &rid : rids) {
            reserved_insert_slots_.erase(encode_reserved_slot(rid));
        }
    }
    try {
        for (const Rid &rid : rids) {
            add_free_page_candidate(rid.page_no);
        }
    } catch (...) {
        // Reservation release is best-effort during stack unwinding. The
        // persisted free list is rebuilt at the next checkpoint/open.
    }
}

void RmFileHandle::ensure_page_exists(int page_no) {
    std::unique_lock<std::mutex> allocation(allocation_latch_);
    while (file_hdr_.num_pages <= page_no) {
        RmPageWriteHandle page = create_new_page_handle();
        const int new_page_no = page.page_no();
        page.drop();
        add_free_page_candidate(new_page_no);
    }
}

void RmFileHandle::insert_record(const Rid &rid, char *buf) {
    if (rid.page_no < RM_FIRST_RECORD_PAGE || rid.slot_no < 0 ||
        rid.slot_no >= file_hdr_.num_records_per_page) {
        throw InternalError("Invalid RID for record insert");
    }
    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_latch_);
    ensure_page_exists(rid.page_no);
    RmPageWriteHandle page = fetch_page_write(rid.page_no);
    const bool existed = Bitmap::is_set(page.bitmap, rid.slot_no);
    Bitmap::set(page.bitmap, rid.slot_no);
    std::memcpy(page.get_slot(rid.slot_no), buf, file_hdr_.record_size);
    if (!existed) {
        page.page_hdr->num_records++;
    }
    const bool reusable =
        page.page_hdr->num_records < file_hdr_.num_records_per_page;
    if (!reusable) {
        page.page_hdr->next_free_page_no = RM_NO_PAGE;
    }
    page.mark_dirty();
    page.drop();
    if (reusable) {
        add_free_page_candidate(rid.page_no);
    } else {
        remove_free_page_candidate(rid.page_no);
    }
}

void RmFileHandle::delete_record(const Rid &rid, Context *context,
                                 lsn_t page_lsn) {
    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_latch_);
    RmPageWriteHandle page = fetch_page_write(rid.page_no);
    if (rid.slot_no < 0 || rid.slot_no >= file_hdr_.num_records_per_page ||
        !Bitmap::is_set(page.bitmap, rid.slot_no)) {
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }
    Bitmap::reset(page.bitmap, rid.slot_no);
    page.page_hdr->num_records--;
    if (page_lsn == INVALID_LSN && context != nullptr &&
        context->txn_ != nullptr) {
        page_lsn = context->txn_->get_prev_lsn();
    }
    if (page_lsn != INVALID_LSN) {
        page.set_page_lsn(page_lsn);
    }
    page.mark_dirty();
    page.drop();
    add_free_page_candidate(rid.page_no);
}

void RmFileHandle::update_record(const Rid &rid, char *buf,
                                 Context *context, lsn_t page_lsn) {
    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_latch_);
    RmPageWriteHandle page = fetch_page_write(rid.page_no);
    if (rid.slot_no < 0 || rid.slot_no >= file_hdr_.num_records_per_page ||
        !Bitmap::is_set(page.bitmap, rid.slot_no)) {
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }
    std::memcpy(page.get_slot(rid.slot_no), buf, file_hdr_.record_size);
    if (page_lsn == INVALID_LSN && context != nullptr &&
        context->txn_ != nullptr) {
        page_lsn = context->txn_->get_prev_lsn();
    }
    if (page_lsn != INVALID_LSN) {
        page.set_page_lsn(page_lsn);
    }
    page.mark_dirty();
}

bool RmFileHandle::record_exists(const Rid &rid) const {
    if (rid.page_no < RM_FIRST_RECORD_PAGE || rid.page_no >= page_count() ||
        rid.slot_no < 0 || rid.slot_no >= file_hdr_.num_records_per_page) {
        return false;
    }
    RmPageReadHandle page = fetch_page_read(rid.page_no);
    return Bitmap::is_set(page.bitmap, rid.slot_no);
}

void RmFileHandle::upsert_record_for_recovery(const Rid &rid,
                                              const char *buf) {
    if (rid.page_no < RM_FIRST_RECORD_PAGE || rid.slot_no < 0 ||
        rid.slot_no >= file_hdr_.num_records_per_page) {
        throw InternalError("Invalid RID in recovery log");
    }
    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_latch_);
    ensure_page_exists(rid.page_no);
    RmPageWriteHandle page = fetch_page_write(rid.page_no);
    if (!Bitmap::is_set(page.bitmap, rid.slot_no)) {
        Bitmap::set(page.bitmap, rid.slot_no);
        page.page_hdr->num_records++;
    }
    std::memcpy(page.get_slot(rid.slot_no), buf, file_hdr_.record_size);
    const bool reusable =
        page.page_hdr->num_records < file_hdr_.num_records_per_page;
    if (!reusable) {
        page.page_hdr->next_free_page_no = RM_NO_PAGE;
    }
    page.mark_dirty();
    page.drop();
    if (reusable) {
        add_free_page_candidate(rid.page_no);
    } else {
        remove_free_page_candidate(rid.page_no);
    }
}

void RmFileHandle::delete_record_for_recovery(const Rid &rid) {
    if (rid.page_no < RM_FIRST_RECORD_PAGE || rid.page_no >= page_count() ||
        rid.slot_no < 0 || rid.slot_no >= file_hdr_.num_records_per_page) {
        return;
    }
    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_latch_);
    RmPageWriteHandle page = fetch_page_write(rid.page_no);
    if (!Bitmap::is_set(page.bitmap, rid.slot_no)) {
        return;
    }
    Bitmap::reset(page.bitmap, rid.slot_no);
    page.page_hdr->num_records--;
    page.mark_dirty();
    page.drop();
    add_free_page_candidate(rid.page_no);
}

void RmFileHandle::rebuild_persisted_free_list_locked() {
    std::vector<page_id_t> candidates;
    {
        std::lock_guard<std::mutex> free_lock(free_pages_latch_);
        candidates = free_page_candidates_;
    }
    std::sort(candidates.begin(), candidates.end());
    for (size_t i = 0; i < candidates.size(); ++i) {
        RmPageWriteHandle page = fetch_page_write(candidates[i]);
        page.page_hdr->next_free_page_no =
            i + 1 < candidates.size() ? candidates[i + 1] : RM_NO_PAGE;
        page.mark_dirty();
    }
    std::lock_guard<std::mutex> free_lock(free_pages_latch_);
    file_hdr_.first_free_page_no =
        candidates.empty() ? RM_NO_PAGE : candidates.front();
}

void RmFileHandle::rebuild_free_page_list() {
    std::unique_lock<std::shared_mutex> lifecycle(lifecycle_latch_);
    const int num_pages = page_count();
    std::vector<page_id_t> candidates;
    for (int page_no = RM_FIRST_RECORD_PAGE; page_no < num_pages; ++page_no) {
        RmPageReadHandle page = fetch_page_read(page_no);
        if (page.page_hdr->num_records < file_hdr_.num_records_per_page) {
            candidates.push_back(page_no);
        }
    }
    {
        std::lock_guard<std::mutex> free_lock(free_pages_latch_);
        free_page_candidates_ = candidates;
        free_page_candidate_set_.clear();
        free_page_candidate_set_.insert(candidates.begin(), candidates.end());
    }
    rebuild_persisted_free_list_locked();
}

void RmFileHandle::flush_file_header() {
    std::unique_lock<std::shared_mutex> lifecycle(lifecycle_latch_);
    rebuild_persisted_free_list_locked();
    std::scoped_lock metadata(allocation_latch_, free_pages_latch_);
    disk_manager_->write_page(
        fd_, RM_FILE_HDR_PAGE, reinterpret_cast<const char *>(&file_hdr_),
        sizeof(file_hdr_));
}
