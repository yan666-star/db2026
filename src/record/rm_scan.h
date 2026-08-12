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

#include <vector>

#include "rm_defs.h"

class RmFileHandle;

class RmScan : public RecScan {
    const RmFileHandle *file_handle_;
    Rid rid_;
    std::vector<Rid> rids_;
    int current_index_ = 0;

    void load_page_rids(int page_no);

   public:
    RmScan(const RmFileHandle *file_handle);

    void next() override;

    bool is_end() const override;

    Rid rid() const override;

    int get_batch_num() const override {
        return static_cast<int>(rids_.size()) - current_index_;
    }
};
