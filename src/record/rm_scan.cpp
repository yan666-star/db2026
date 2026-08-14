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

void RmScan::load_page_rids(int page_no) {
    rids_.clear();
    current_index_ = 0;
    rid_ = {RM_NO_PAGE, -1};

    if (page_no >= file_handle_->page_count()) {
        return;
    }

    RmPageReadHandle page_handle = file_handle_->fetch_page_read(page_no);
    int slot_no = -1;
    while ((slot_no = Bitmap::next_bit(
                true, page_handle.bitmap,
                file_handle_->file_hdr_.num_records_per_page, slot_no)) <
           file_handle_->file_hdr_.num_records_per_page) {
        rids_.push_back(Rid{page_no, slot_no});
    }
    if (!rids_.empty()) {
        rid_ = rids_[0];
    }
}

/**
 * @brief 初始化file_handle和rid
 * @param file_handle
 */
RmScan::RmScan(const RmFileHandle *file_handle) : file_handle_(file_handle) {
    rid_.page_no = RM_NO_PAGE;
    rid_.slot_no = -1;

    if (file_handle_->page_count() <= RM_FIRST_RECORD_PAGE) {
        return;
    }

    for (int page_no = RM_FIRST_RECORD_PAGE;
         page_no < file_handle_->page_count(); ++page_no) {
        load_page_rids(page_no);
        if (!rids_.empty()) {
            return;
        }
    }
}

/**
 * @brief 找到文件中下一个存放了记录的位置
 */
void RmScan::next() {
    if (is_end()) {
        return;
    }

    if (current_index_ + 1 < static_cast<int>(rids_.size())) {
        current_index_++;
        rid_ = rids_[current_index_];
        return;
    }

    int next_page = rid_.page_no + 1;
    while (next_page < file_handle_->page_count()) {
        load_page_rids(next_page);
        if (!rids_.empty()) {
            return;
        }
        next_page++;
    }

    rid_.page_no = RM_NO_PAGE;
    rid_.slot_no = -1;
    rids_.clear();
    current_index_ = 0;
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
