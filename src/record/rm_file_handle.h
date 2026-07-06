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

#include <assert.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "bitmap.h"
#include "common/context.h"
#include "rm_defs.h"

class RmManager;

/* 对表数据文件中的页面进行封装 */
struct RmPageHandle {
    const RmFileHdr *file_hdr;  // 当前页面所在文件的文件头指针
    Page *page;                 // 页面的实际数据，包括页面存储的数据、元信息等
    RmPageHdr *page_hdr;        // page->data的第一部分，存储页面元信息，指针指向首地址，长度为sizeof(RmPageHdr)
    char *bitmap;               // page->data的第二部分，存储页面的bitmap，指针指向首地址，长度为file_hdr->bitmap_size
    char *slots;                // page->data的第三部分，存储表的记录，指针指向首地址，每个slot的长度为file_hdr->record_size

    RmPageHandle(const RmFileHdr *fhdr_, Page *page_) : file_hdr(fhdr_), page(page_) {
        page_hdr = reinterpret_cast<RmPageHdr *>(page->get_data() + page->OFFSET_PAGE_HDR);
        bitmap = page->get_data() + sizeof(RmPageHdr) + page->OFFSET_PAGE_HDR;
        slots = bitmap + file_hdr->bitmap_size;
    }

    // 返回指定slot_no的slot存储收地址
    char* get_slot(int slot_no) const {
        return slots + slot_no * file_hdr->record_size;  // slots的首地址 + slot个数 * 每个slot的大小(每个record的大小)
    }
};

/* 每个RmFileHandle对应一个表的数据文件，里面有多个page，每个page的数据封装在RmPageHandle中 */
class RmFileHandle {      
    friend class RmScan;    
    friend class RmManager;

   private:
    struct IntEqualityCache {
        int offset = 0;
        std::unordered_multimap<int, Rid> values;
    };

    DiskManager *disk_manager_;
    BufferPoolManager *buffer_pool_manager_;
    int fd_;        // 打开文件后产生的文件句柄
    inline static std::atomic<uint64_t> next_mvcc_file_id_{0};
    uint64_t mvcc_file_id_;
    RmFileHdr file_hdr_;    // 文件头，维护当前表文件的元数据
    std::mutex insert_latch_;
    std::mutex logical_update_latch_;
    std::unordered_map<int, IntEqualityCache> int_equality_caches_;

   public:
    RmFileHandle(DiskManager *disk_manager, BufferPoolManager *buffer_pool_manager, int fd)
        : disk_manager_(disk_manager),
          buffer_pool_manager_(buffer_pool_manager),
          fd_(fd),
          mvcc_file_id_(next_mvcc_file_id_.fetch_add(1)) {
        // 注意：这里从磁盘中读出文件描述符为fd的文件的file_hdr，读到内存中
        // 这里实际就是初始化file_hdr，只不过是从磁盘中读出进行初始化
        // init file_hdr_
        disk_manager_->read_page(fd, RM_FILE_HDR_PAGE, (char *)&file_hdr_, sizeof(file_hdr_));
        // disk_manager管理的fd对应的文件中，设置从file_hdr_.num_pages开始分配page_no
        disk_manager_->set_fd2pageno(fd, file_hdr_.num_pages);
    }

    RmFileHdr get_file_hdr() { return file_hdr_; }
    int GetFd() { return fd_; }
    uint64_t GetMvccFileId() const { return mvcc_file_id_; }
    std::unique_lock<std::mutex> acquire_logical_update_latch() {
        return std::unique_lock<std::mutex>(logical_update_latch_);
    }

    /* 判断指定位置上是否已经存在一条记录，通过Bitmap来判断 */
    bool is_record(const Rid &rid) const {
        RmPageHandle page_handle = fetch_page_handle(rid.page_no);
        bool exists = Bitmap::is_set(page_handle.bitmap, rid.slot_no);
        buffer_pool_manager_->unpin_page(PageId{fd_, rid.page_no}, false);
        return exists;
    }

    std::unique_ptr<RmRecord> get_record(const Rid &rid, Context *context) const;

    std::vector<std::unique_ptr<RmRecord>> batch_get_records(int page_no, std::vector<Rid> &rids, Context *context) const;

    std::vector<Rid> lookup_int_equal_records(int offset, int value);

    std::vector<Rid> all_record_slots();

    Rid insert_record(char *buf, Context *context);

    Rid insert_record(char *buf, Context *context, const std::string &table_name);

    void insert_record(const Rid &rid, char *buf);

    void delete_record(const Rid &rid, Context *context);

    void update_record(const Rid &rid, char *buf, Context *context);

    bool record_exists(const Rid &rid) const;

    void upsert_record_for_recovery(const Rid &rid, const char *buf);

    void delete_record_for_recovery(const Rid &rid);

    void rebuild_free_page_list();

    void flush_file_header() const;

    RmPageHandle create_new_page_handle();

    RmPageHandle fetch_page_handle(int page_no) const;

   private:
    Rid insert_record_internal(char *buf, Context *context, const std::string *table_name);

    RmPageHandle create_page_handle();

    void release_page_handle(RmPageHandle &page_handle);

    void ensure_page_exists(int page_no);

    void remove_page_from_free_list(int page_no);

    void add_to_int_equality_caches(const Rid &rid, const char *record);

    void remove_from_int_equality_caches(const Rid &rid, const char *record);

    void update_int_equality_caches(const Rid &rid, const char *old_record, const char *new_record);
};
