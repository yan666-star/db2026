#include <cstdlib>
#include <iostream>

#include "common/server_memory_budget.h"
#include "storage/page.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    constexpr uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
    const size_t large_host_pages =
        rmdb::memory::select_buffer_pool_pages(64 * kGiB, nullptr);
    require(large_host_pages == rmdb::memory::kMaximumBufferPoolPages,
            "large hosts must use the bounded buffer-pool maximum");
    require(large_host_pages * sizeof(Page) < 3 * kGiB,
            "Page array leaves insufficient headroom below 8 GiB RSS");

    const size_t four_gib_pages =
        rmdb::memory::select_buffer_pool_pages(4 * kGiB, nullptr);
    require(four_gib_pages == (kGiB / PAGE_SIZE),
            "default pool must remain one quarter of physical memory");

    const size_t explicit_safe = rmdb::memory::select_buffer_pool_pages(
        4 * kGiB, "524288");
    require(explicit_safe == rmdb::memory::kMaximumBufferPoolPages,
            "safe explicit buffer-pool override was rejected");

    const size_t explicit_unsafe = rmdb::memory::select_buffer_pool_pages(
        4 * kGiB, "2097152");
    require(explicit_unsafe == four_gib_pages,
            "unsafe explicit override bypassed the hard RSS budget");

    std::cout << "Server memory budget tests passed\n";
    return 0;
}
