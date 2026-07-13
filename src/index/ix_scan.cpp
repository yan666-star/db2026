/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "ix_scan.h"

void IxScan::next() {
    if (iid_.slot_no < node_size_ - 1) {
        iid_.slot_no++;
        return;
    }
    if (is_end() || iid_.page_no == ih_->file_hdr_->last_leaf_) {
        iid_ = end_;
        return;
    }

    page_id_t page_no = iid_.page_no;
    while (true) {
        auto node = ih_->fetch_node(page_no);
        page_id_t next_page = node->get_next_leaf();
        bpm_->unpin_page(node->get_page_id(), false);
        delete node;

        if (next_page == IX_LEAF_HEADER_PAGE ||
            next_page == INVALID_PAGE_ID || next_page == IX_NO_PAGE) {
            iid_ = end_;
            return;
        }
        iid_ = {next_page, 0};
        if (is_end()) {
            return;
        }
        if (load_leaf(next_page)) {
            return;
        }
        page_no = next_page;
    }
}

Rid IxScan::rid() const {
    return batch_rids_.empty() ? Rid{-1, -1} : batch_rids_[iid_.slot_no];
}
