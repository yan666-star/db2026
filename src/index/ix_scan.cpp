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
    if (!is_end() && iid_.page_no != ih_->file_hdr_->last_leaf_) {
        auto node = ih_->fetch_node(iid_.page_no);
        iid_.slot_no = 0;
        iid_.page_no = node->get_next_leaf();
        bpm_->unpin_page(node->get_page_id(), false);
        delete node;
        if (!is_end()) {
            load_leaf(iid_.page_no);
        }
    } else {
        iid_ = end_;
    }
}

Rid IxScan::rid() const {
    return batch_rids_.empty() ? Rid{-1, -1} : batch_rids_[iid_.slot_no];
}
