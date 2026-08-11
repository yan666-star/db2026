#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <shared_mutex>

namespace rmdb_perf {

struct SharedCounters {
    std::atomic<uint64_t> buffer_fetches{0};
    std::atomic<uint64_t> buffer_hits{0};
    std::atomic<uint64_t> buffer_misses{0};
    std::atomic<uint64_t> buffer_latch_acquires{0};
    std::atomic<uint64_t> buffer_latch_wait_us{0};
    std::atomic<uint64_t> buffer_frame_latch_acquires{0};
    std::atomic<uint64_t> buffer_frame_latch_wait_us{0};
    std::atomic<uint64_t> commit_apply_read_acquires{0};
    std::atomic<uint64_t> commit_apply_read_wait_us{0};
    std::atomic<uint64_t> commit_apply_write_acquires{0};
    std::atomic<uint64_t> commit_apply_write_wait_us{0};
    std::atomic<uint64_t> mvcc_latch_acquires{0};
    std::atomic<uint64_t> mvcc_latch_wait_us{0};
};

inline SharedCounters &shared_counters() {
    static SharedCounters counters;
    return counters;
}

inline bool enabled() {
    static bool value = [] {
        const char *env = std::getenv("RMDB_PERF_DIAG");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    return value;
}

inline std::unique_lock<std::mutex> timed_lock(
    std::mutex &latch, std::atomic<uint64_t> &acquires,
    std::atomic<uint64_t> &wait_us) {
    if (!enabled()) {
        return std::unique_lock<std::mutex>(latch);
    }
    acquires.fetch_add(1, std::memory_order_relaxed);
    auto start = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(latch);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    wait_us.fetch_add(static_cast<uint64_t>(elapsed),
                      std::memory_order_relaxed);
    return lock;
}

inline std::shared_lock<std::shared_mutex> lock_buffer_shared(
    std::shared_mutex &latch) {
    auto &c = shared_counters();
    if (!enabled()) {
        return std::shared_lock<std::shared_mutex>(latch);
    }
    c.buffer_latch_acquires.fetch_add(1, std::memory_order_relaxed);
    auto start = std::chrono::steady_clock::now();
    std::shared_lock<std::shared_mutex> lock(latch);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    c.buffer_latch_wait_us.fetch_add(static_cast<uint64_t>(elapsed),
                                     std::memory_order_relaxed);
    return lock;
}

inline std::unique_lock<std::shared_mutex> lock_buffer_exclusive(
    std::shared_mutex &latch) {
    auto &c = shared_counters();
    if (!enabled()) {
        return std::unique_lock<std::shared_mutex>(latch);
    }
    c.buffer_latch_acquires.fetch_add(1, std::memory_order_relaxed);
    auto start = std::chrono::steady_clock::now();
    std::unique_lock<std::shared_mutex> lock(latch);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    c.buffer_latch_wait_us.fetch_add(static_cast<uint64_t>(elapsed),
                                     std::memory_order_relaxed);
    return lock;
}

inline std::unique_lock<std::mutex> lock_buffer_frame(std::mutex &latch) {
    auto &c = shared_counters();
    return timed_lock(latch, c.buffer_frame_latch_acquires,
                      c.buffer_frame_latch_wait_us);
}

inline std::shared_lock<std::shared_mutex> lock_commit_apply_read(
    std::shared_mutex &latch) {
    auto &c = shared_counters();
    if (!enabled()) {
        return std::shared_lock<std::shared_mutex>(latch);
    }
    c.commit_apply_read_acquires.fetch_add(1, std::memory_order_relaxed);
    auto start = std::chrono::steady_clock::now();
    std::shared_lock<std::shared_mutex> lock(latch);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    c.commit_apply_read_wait_us.fetch_add(static_cast<uint64_t>(elapsed),
                                          std::memory_order_relaxed);
    return lock;
}

inline std::unique_lock<std::shared_mutex> lock_commit_apply_write(
    std::shared_mutex &latch) {
    auto &c = shared_counters();
    if (!enabled()) {
        return std::unique_lock<std::shared_mutex>(latch);
    }
    c.commit_apply_write_acquires.fetch_add(1, std::memory_order_relaxed);
    auto start = std::chrono::steady_clock::now();
    std::unique_lock<std::shared_mutex> lock(latch);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    c.commit_apply_write_wait_us.fetch_add(static_cast<uint64_t>(elapsed),
                                           std::memory_order_relaxed);
    return lock;
}

inline std::unique_lock<std::mutex> lock_mvcc(std::mutex &latch) {
    auto &c = shared_counters();
    return timed_lock(latch, c.mvcc_latch_acquires,
                      c.mvcc_latch_wait_us);
}

}  // namespace rmdb_perf
