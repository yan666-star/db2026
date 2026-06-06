/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_file_handle.h"

#include <cstring>

#include "errors.h"
#include "recovery/log_manager.h"

/**
 * @description: 获取当前表中记录号为rid的记录
 * @param {Rid&} rid 记录号，指定记录的位置
 * @param {Context*} context
 * @return {unique_ptr<RmRecord>} rid对应的记录对象指针
 */
std::unique_ptr<RmRecord> RmFileHandle::get_record(const Rid& rid, Context* context) const {
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    if (!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {
        buffer_pool_manager_->unpin_page(PageId{fd_, rid.page_no}, false);
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }

    auto record = std::make_unique<RmRecord>(file_hdr_.record_size);
    memcpy(record->data, page_handle.get_slot(rid.slot_no), file_hdr_.record_size);
    buffer_pool_manager_->unpin_page(PageId{fd_, rid.page_no}, false);
    return record;
}

std::vector<std::unique_ptr<RmRecord>> RmFileHandle::batch_get_records(int page_no, std::vector<Rid> &rids,
                                                                       Context *context) const {
    std::vector<std::unique_ptr<RmRecord>> records;
    if (rids.empty()) {
        return records;
    }

    RmPageHandle page_handle = fetch_page_handle(page_no);
    std::vector<Rid> valid_rids;
    valid_rids.reserve(rids.size());
    for (const auto &rid : rids) {
        if (rid.page_no != page_no) {
            continue;
        }
        if (!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {
            continue;
        }
        auto record = std::make_unique<RmRecord>(file_hdr_.record_size);
        memcpy(record->data, page_handle.get_slot(rid.slot_no), file_hdr_.record_size);
        records.push_back(std::move(record));
        valid_rids.push_back(rid);
    }
    buffer_pool_manager_->unpin_page(PageId{fd_, page_no}, false);
    rids = std::move(valid_rids);
    return records;
}

/**
 * @description: 在当前表中插入一条记录，不指定插入位置
 * @param {char*} buf 要插入的记录的数据
 * @param {Context*} context
 * @return {Rid} 插入的记录的记录号（位置）
 */
Rid RmFileHandle::insert_record(char* buf, Context* context) {
    return insert_record_internal(buf, context, nullptr);
}

Rid RmFileHandle::insert_record(char *buf, Context *context, const std::string &table_name) {
    return insert_record_internal(buf, context, &table_name);
}

Rid RmFileHandle::insert_record_internal(char *buf, Context *context, const std::string *table_name) {
    std::lock_guard<std::mutex> lock(insert_latch_);
    RmPageHandle page_handle = create_page_handle();
    int page_no = page_handle.page->get_page_id().page_no;
    int slot_no = Bitmap::first_bit(false, page_handle.bitmap, file_hdr_.num_records_per_page);
    Rid rid{page_no, slot_no};

    if (context != nullptr && context->txn_ != nullptr && context->log_mgr_ != nullptr &&
        table_name != nullptr) {
        RmRecord record(file_hdr_.record_size, buf);
        InsertLogRecord log_record(
            context->txn_->get_transaction_id(), record, rid, *table_name);
        log_record.prev_lsn_ = context->txn_->get_prev_lsn();
        lsn_t lsn = context->log_mgr_->add_log_to_buffer(&log_record);
        context->txn_->set_prev_lsn(lsn);
    }

    Bitmap::set(page_handle.bitmap, slot_no);
    memcpy(page_handle.get_slot(slot_no), buf, file_hdr_.record_size);
    page_handle.page_hdr->num_records++;

    if (page_handle.page_hdr->num_records == file_hdr_.num_records_per_page) {
        if (file_hdr_.first_free_page_no == page_no) {
            file_hdr_.first_free_page_no = page_handle.page_hdr->next_free_page_no;
        }
        page_handle.page_hdr->next_free_page_no = RM_NO_PAGE;
    }

    buffer_pool_manager_->unpin_page(PageId{fd_, page_no}, true);
    return rid;
}

/**
 * @description: 在当前表中的指定位置插入一条记录
 * @param {Rid&} rid 要插入记录的位置
 * @param {char*} buf 要插入记录的数据
 */
void RmFileHandle::insert_record(const Rid& rid, char* buf) {
    std::lock_guard<std::mutex> lock(insert_latch_);
    ensure_page_exists(rid.page_no);
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    bool existed = Bitmap::is_set(page_handle.bitmap, rid.slot_no);
    Bitmap::set(page_handle.bitmap, rid.slot_no);
    memcpy(page_handle.get_slot(rid.slot_no), buf, file_hdr_.record_size);
    if (!existed) {
        page_handle.page_hdr->num_records++;
    }
    bool became_full =
        !existed &&
        page_handle.page_hdr->num_records == file_hdr_.num_records_per_page;
    buffer_pool_manager_->unpin_page(PageId{fd_, rid.page_no}, true);
    if (became_full) {
        remove_page_from_free_list(rid.page_no);
    }
}

/**
 * @description: 删除记录文件中记录号为rid的记录
 * @param {Rid&} rid 要删除的记录的记录号（位置）
 * @param {Context*} context
 */
void RmFileHandle::delete_record(const Rid& rid, Context* context) {
    std::lock_guard<std::mutex> lock(insert_latch_);
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    if (!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {
        buffer_pool_manager_->unpin_page(PageId{fd_, rid.page_no}, false);
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }

    bool was_full = page_handle.page_hdr->num_records == file_hdr_.num_records_per_page;
    Bitmap::reset(page_handle.bitmap, rid.slot_no);
    page_handle.page_hdr->num_records--;

    if (was_full) {
        release_page_handle(page_handle);
    }

    buffer_pool_manager_->unpin_page(PageId{fd_, rid.page_no}, true);
}

/**
 * @description: 更新记录文件中记录号为rid的记录
 * @param {Rid&} rid 要更新的记录的记录号（位置）
 * @param {char*} buf 新记录的数据
 * @param {Context*} context
 */
void RmFileHandle::update_record(const Rid& rid, char* buf, Context* context) {
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    if (!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {
        buffer_pool_manager_->unpin_page(PageId{fd_, rid.page_no}, false);
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }

    memcpy(page_handle.get_slot(rid.slot_no), buf, file_hdr_.record_size);
    buffer_pool_manager_->unpin_page(PageId{fd_, rid.page_no}, true);
}

bool RmFileHandle::record_exists(const Rid &rid) const {
    if (rid.page_no < RM_FIRST_RECORD_PAGE || rid.page_no >= file_hdr_.num_pages ||
        rid.slot_no < 0 || rid.slot_no >= file_hdr_.num_records_per_page) {
        return false;
    }
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    bool exists = Bitmap::is_set(page_handle.bitmap, rid.slot_no);
    buffer_pool_manager_->unpin_page(PageId{fd_, rid.page_no}, false);
    return exists;
}

void RmFileHandle::upsert_record_for_recovery(const Rid &rid, const char *buf) {
    if (rid.page_no < RM_FIRST_RECORD_PAGE || rid.slot_no < 0 ||
        rid.slot_no >= file_hdr_.num_records_per_page) {
        throw InternalError("Invalid RID in recovery log");
    }

    ensure_page_exists(rid.page_no);
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    if (!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {
        Bitmap::set(page_handle.bitmap, rid.slot_no);
        page_handle.page_hdr->num_records++;
    }
    memcpy(page_handle.get_slot(rid.slot_no), buf, file_hdr_.record_size);
    buffer_pool_manager_->unpin_page(PageId{fd_, rid.page_no}, true);
}

void RmFileHandle::delete_record_for_recovery(const Rid &rid) {
    if (!record_exists(rid)) {
        return;
    }
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    Bitmap::reset(page_handle.bitmap, rid.slot_no);
    page_handle.page_hdr->num_records--;
    buffer_pool_manager_->unpin_page(PageId{fd_, rid.page_no}, true);
}

void RmFileHandle::rebuild_free_page_list() {
    int first_free_page_no = RM_NO_PAGE;
    for (int page_no = file_hdr_.num_pages - 1; page_no >= RM_FIRST_RECORD_PAGE; --page_no) {
        RmPageHandle page_handle = fetch_page_handle(page_no);
        if (page_handle.page_hdr->num_records < file_hdr_.num_records_per_page) {
            page_handle.page_hdr->next_free_page_no = first_free_page_no;
            first_free_page_no = page_no;
        } else {
            page_handle.page_hdr->next_free_page_no = RM_NO_PAGE;
        }
        buffer_pool_manager_->unpin_page(PageId{fd_, page_no}, true);
    }
    file_hdr_.first_free_page_no = first_free_page_no;
}

void RmFileHandle::flush_file_header() const {
    disk_manager_->write_page(
        fd_, RM_FILE_HDR_PAGE, reinterpret_cast<const char *>(&file_hdr_), sizeof(file_hdr_));
}

/**
 * 以下函数为辅助函数，仅提供参考，可以选择完成如下函数，也可以删除如下函数，在单元测试中不涉及如下函数接口的直接调用
*/
/**
 * @description: 获取指定页面的页面句柄
 * @param {int} page_no 页面号
 * @return {RmPageHandle} 指定页面的句柄
 */
RmPageHandle RmFileHandle::fetch_page_handle(int page_no) const {
    if (page_no < 0 || page_no >= file_hdr_.num_pages) {
        throw PageNotExistError(std::to_string(fd_), page_no);
    }

    Page *page = buffer_pool_manager_->fetch_page(PageId{fd_, page_no});
    return RmPageHandle(&file_hdr_, page);
}

/**
 * @description: 创建一个新的page handle
 * @return {RmPageHandle} 新的PageHandle
 */
RmPageHandle RmFileHandle::create_new_page_handle() {
    PageId page_id = {fd_, INVALID_PAGE_ID};
    Page *page = buffer_pool_manager_->new_page(&page_id);

    RmPageHandle page_handle(&file_hdr_, page);
    page_handle.page_hdr->next_free_page_no = file_hdr_.first_free_page_no;
    page_handle.page_hdr->num_records = 0;
    Bitmap::init(page_handle.bitmap, file_hdr_.bitmap_size);

    file_hdr_.first_free_page_no = page_id.page_no;
    file_hdr_.num_pages++;

    return page_handle;
}

/**
 * @brief 创建或获取一个空闲的page handle
 *
 * @return RmPageHandle 返回生成的空闲page handle
 * @note pin the page, remember to unpin it outside!
 */
RmPageHandle RmFileHandle::create_page_handle() {
    if (file_hdr_.first_free_page_no == RM_NO_PAGE) {
        return create_new_page_handle();
    }

    return fetch_page_handle(file_hdr_.first_free_page_no);
}

/**
 * @description: 当一个页面从没有空闲空间的状态变为有空闲空间状态时，更新文件头和页头中空闲页面相关的元数据
 */
void RmFileHandle::release_page_handle(RmPageHandle& page_handle) {
    int page_no = page_handle.page->get_page_id().page_no;
    page_handle.page_hdr->next_free_page_no = file_hdr_.first_free_page_no;
    file_hdr_.first_free_page_no = page_no;
}

void RmFileHandle::ensure_page_exists(int page_no) {
    while (file_hdr_.num_pages <= page_no) {
        RmPageHandle page_handle = create_new_page_handle();
        int new_page_no = page_handle.page->get_page_id().page_no;
        buffer_pool_manager_->unpin_page(PageId{fd_, new_page_no}, true);
    }
}

void RmFileHandle::remove_page_from_free_list(int page_no) {
    if (file_hdr_.first_free_page_no == RM_NO_PAGE) {
        return;
    }
    if (file_hdr_.first_free_page_no == page_no) {
        RmPageHandle page_handle = fetch_page_handle(page_no);
        file_hdr_.first_free_page_no = page_handle.page_hdr->next_free_page_no;
        page_handle.page_hdr->next_free_page_no = RM_NO_PAGE;
        buffer_pool_manager_->unpin_page(PageId{fd_, page_no}, true);
        return;
    }

    int current_page_no = file_hdr_.first_free_page_no;
    while (current_page_no != RM_NO_PAGE) {
        RmPageHandle current = fetch_page_handle(current_page_no);
        int next_page_no = current.page_hdr->next_free_page_no;
        if (next_page_no == page_no) {
            RmPageHandle removed = fetch_page_handle(page_no);
            current.page_hdr->next_free_page_no =
                removed.page_hdr->next_free_page_no;
            removed.page_hdr->next_free_page_no = RM_NO_PAGE;
            buffer_pool_manager_->unpin_page(PageId{fd_, page_no}, true);
            buffer_pool_manager_->unpin_page(
                PageId{fd_, current_page_no}, true);
            return;
        }
        buffer_pool_manager_->unpin_page(
            PageId{fd_, current_page_no}, false);
        current_page_no = next_page_no;
    }
}
