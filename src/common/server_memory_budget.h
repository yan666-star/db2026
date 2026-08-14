#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "common/config.h"

namespace rmdb::memory {

// The evaluator applies an 8 GiB hard RSS limit to the whole process tree.
// Buffer pages are only one resident component: every frame also owns latches
// and queue metadata, while MVCC, indexes, WAL and client threads need runtime
// headroom. Keep the page-data portion at or below 2 GiB.
inline constexpr size_t kMaximumBufferPoolBytes =
    2ULL * 1024ULL * 1024ULL * 1024ULL;
inline constexpr size_t kMaximumBufferPoolPages =
    kMaximumBufferPoolBytes / PAGE_SIZE;
inline constexpr size_t kMinimumBufferPoolPages = BUFFER_POOL_SIZE;

inline size_t select_buffer_pool_pages(uint64_t physical_bytes,
                                       const char *override_value) {
    if (override_value != nullptr && override_value[0] != '\0') {
        char *end = nullptr;
        const unsigned long long parsed =
            std::strtoull(override_value, &end, 10);
        if (end != override_value && *end == '\0' && parsed >= 1024 &&
            parsed <= kMaximumBufferPoolPages) {
            return static_cast<size_t>(parsed);
        }
    }

    if (physical_bytes == 0) {
        return kMinimumBufferPoolPages;
    }
    const uint64_t budget_bytes = physical_bytes / 4;
    const size_t pages = static_cast<size_t>(budget_bytes / PAGE_SIZE);
    return std::max(kMinimumBufferPoolPages,
                    std::min(kMaximumBufferPoolPages, pages));
}

}  // namespace rmdb::memory
