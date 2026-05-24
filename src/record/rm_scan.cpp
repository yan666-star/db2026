/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_scan.h"

#include "bitmap.h"
#include "rm_file_handle.h"

/**
 * @brief 初始化file_handle和rid
 * @param file_handle
 */
RmScan::RmScan(const RmFileHandle *file_handle) : file_handle_(file_handle) {
    rid_.page_no = RM_NO_PAGE;
    rid_.slot_no = -1;

    if (file_handle_->file_hdr_.num_pages <= RM_FIRST_RECORD_PAGE) {
        return;
    }

    rid_.page_no = RM_FIRST_RECORD_PAGE;
    rid_.slot_no = -1;
    next();
}

/**
 * @brief 找到文件中下一个存放了记录的位置
 */
void RmScan::next() {
    if (is_end()) {
        return;
    }

    int page_no = rid_.page_no;
    int slot_no = rid_.slot_no;

    while (page_no < file_handle_->file_hdr_.num_pages) {
        RmPageHandle page_handle = file_handle_->fetch_page_handle(page_no);
        slot_no = Bitmap::next_bit(true, page_handle.bitmap, file_handle_->file_hdr_.num_records_per_page, slot_no);
        if (slot_no < file_handle_->file_hdr_.num_records_per_page) {
            rid_.page_no = page_no;
            rid_.slot_no = slot_no;
            file_handle_->buffer_pool_manager_->unpin_page(PageId{file_handle_->fd_, page_no}, false);
            return;
        }
        file_handle_->buffer_pool_manager_->unpin_page(PageId{file_handle_->fd_, page_no}, false);
        page_no++;
        slot_no = -1;
    }

    rid_.page_no = RM_NO_PAGE;
    rid_.slot_no = -1;
}

/**
 * @brief ​ 判断是否到达文件末尾
 */
bool RmScan::is_end() const {
    return rid_.page_no == RM_NO_PAGE;
}

/**
 * @brief RmScan内部存放的rid
 */
Rid RmScan::rid() const {
    return rid_;
}
