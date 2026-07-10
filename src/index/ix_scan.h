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

    void load_leaf(page_id_t page_no) {
        auto node = ih_->fetch_node(page_no);
        node_size_ = node->get_size();
        if (iid_.slot_no < 0 || iid_.slot_no >= node_size_) {
            bpm_->unpin_page(node->get_page_id(), false);
            delete node;
            throw IndexEntryNotFoundError();
        }
        batch_rids_.clear();
        batch_rids_.reserve(node_size_);
        for (int i = 0; i < node_size_; ++i) {
            batch_rids_.push_back(*node->get_rid(i));
        }
        bpm_->unpin_page(node->get_page_id(), false);
        delete node;
    }

   public:
    IxScan(const IxIndexHandle *ih, const Iid &lower, const Iid &upper, BufferPoolManager *bpm)
        : ih_(ih), iid_(lower), end_(upper), bpm_(bpm) {
        if (is_end()) {
            return;
        }
        load_leaf(iid_.page_no);
    }

    void next() override;

    bool is_end() const override { return iid_ == end_; }

    Rid rid() const override;

    const Iid &iid() const { return iid_; }
};
