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

#include "ix_defs.h"
#include "ix_index_handle.h"

class IxScan : public RecScan {
    const IxIndexHandle *ih_;
    Iid iid_;
    Iid end_;
    BufferPoolManager *bpm_;

    int node_size_ = 0;
    std::vector<Rid> batch_rids_;

   public:
    IxScan(const IxIndexHandle *ih, const Iid &lower, const Iid &upper, BufferPoolManager *bpm)
        : ih_(ih), iid_(lower), end_(upper), bpm_(bpm) {
        if (is_end()) {
            return;
        }
        auto node = ih_->fetch_node(iid_.page_no);
        node_size_ = node->get_size();
        batch_rids_ = ih_->get_rids(iid_);
        bpm_->unpin_page(node->get_page_id(), false);
        delete node;
    }

    void next() override;

    bool is_end() const override { return iid_ == end_; }

    Rid rid() const override;

    const Iid &iid() const { return iid_; }
};
