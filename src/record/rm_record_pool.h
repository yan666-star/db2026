#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "record/rm_defs.h"

class RmRecordPool {
   public:
    std::unique_ptr<RmRecord> acquire(int record_size) {
        while (!records_.empty()) {
            auto record = std::move(records_.back());
            records_.pop_back();
            if (record != nullptr && record->allocated_ &&
                record->data != nullptr && record->size == record_size) {
                return record;
            }
        }
        return std::make_unique<RmRecord>(record_size);
    }

    void release(std::unique_ptr<RmRecord> record) {
        if (record != nullptr) {
            records_.push_back(std::move(record));
        }
    }

    size_t available() const { return records_.size(); }

   private:
    std::vector<std::unique_ptr<RmRecord>> records_;
};
