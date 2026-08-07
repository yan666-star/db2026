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

#include "execution_defs.h"
#include "common/common.h"
#include "index/ix.h"
#include "system/sm.h"

class AbstractExecutor {
   public:
    Rid _abstract_rid;

    Context *context_;

    virtual ~AbstractExecutor() = default;

    virtual size_t tupleLen() const { return 0; };

    virtual const std::vector<ColMeta> &cols() const {
        std::vector<ColMeta> *_cols = nullptr;
        return *_cols;
    };

    virtual std::string getType() { return "AbstractExecutor"; };
    
    //增加一个函数用来获取当前执行器的行数，方便explain analyze显示
    virtual size_t rows() const { return 0; }
    virtual void beginTuple(){};

    virtual void nextTuple(){};

    virtual bool is_end() const { return true; };

    virtual Rid &rid() = 0;

    virtual std::unique_ptr<RmRecord> Next() = 0;

    // Executors that already own the current tuple may expose a borrowed
    // view. Consumers must not retain it after nextTuple(). The default keeps
    // compatibility with materializing executors and lets callers fall back
    // to Next().
    virtual const RmRecord *current_record() const { return nullptr; }

    // A blocking consumer such as aggregation can request a coarse read path.
    // Scan executors may use this hint only when it preserves transaction
    // semantics; other executors can ignore it.
    virtual void enable_bulk_read() {}

    virtual ColMeta get_col_offset(const TabCol &target) { return ColMeta();};

    virtual bool set_index_lookup(const TabCol &target, const char *data, ColType type, int len) {
        return false;
    }

    std::vector<ColMeta>::const_iterator get_col(const std::vector<ColMeta> &rec_cols, const TabCol &target) {
        auto pos = std::find_if(rec_cols.begin(), rec_cols.end(), [&](const ColMeta &col) {
            return col.tab_name == target.tab_name && col.name == target.col_name;
        });
        if (pos == rec_cols.end()) {
            pos = std::find_if(rec_cols.begin(), rec_cols.end(), [&](const ColMeta &col) {
                return col.name == target.col_name;
            });
        }
        if (pos == rec_cols.end()) {
            throw ColumnNotFoundError(target.tab_name + '.' + target.col_name);
        }
        return pos;
    }
};
