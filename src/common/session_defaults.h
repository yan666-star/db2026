/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include <atomic>

#include "transaction/txn_defs.h"

namespace session_defaults {

inline std::atomic<int> &default_isolation_storage() {
    static std::atomic<int> level{
        static_cast<int>(IsolationLevel::READ_COMMITTED)};
    return level;
}

inline IsolationLevel get() {
    return static_cast<IsolationLevel>(default_isolation_storage().load());
}

inline void set(IsolationLevel level) {
    default_isolation_storage().store(static_cast<int>(level));
}

}  // namespace session_defaults
