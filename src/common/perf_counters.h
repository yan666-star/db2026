#pragma once

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <mutex>
#include <shared_mutex>
#include <string_view>

namespace rmdb_perf {

enum class Metric : size_t {
    TXN_STATE_WAIT = 0,
    COMMIT_APPLY_READ_WAIT,
    COMMIT_APPLY_WRITE_WAIT,
    HEAP_FILE_WAIT,
    INDEX_ROOT_READ_WAIT,
    INDEX_ROOT_WRITE_WAIT,
    WAL_DURABLE_WAIT,
    WAL_FSYNC,
    WAL_GROUP_SIZE,
    COUNT
};

struct MetricSnapshot {
    uint64_t count = 0;
    uint64_t total_us = 0;
    uint64_t max_us = 0;
};

struct MetricCounter {
    std::atomic<uint64_t> count{0};
    std::atomic<uint64_t> total{0};
    std::atomic<uint64_t> maximum{0};
};

constexpr size_t metric_count() {
    return static_cast<size_t>(Metric::COUNT);
}

inline std::array<MetricCounter, metric_count()> &metric_counters() {
    static std::array<MetricCounter, metric_count()> counters;
    return counters;
}

inline void update_max(std::atomic<uint64_t> &target, uint64_t value) {
    uint64_t observed = target.load(std::memory_order_relaxed);
    while (observed < value &&
           !target.compare_exchange_weak(
               observed, value, std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

struct LocalMetricCounter {
    uint64_t count = 0;
    uint64_t total = 0;
    uint64_t maximum = 0;
};

struct LocalCounters {
    std::array<LocalMetricCounter, metric_count()> values{};
    uint64_t pending = 0;
};

inline LocalCounters &local_counters() {
    thread_local LocalCounters counters;
    return counters;
}

inline void flush_thread_local() {
    auto &local = local_counters();
    auto &global = metric_counters();
    for (size_t index = 0; index < metric_count(); ++index) {
        auto &source = local.values[index];
        if (source.count == 0) {
            continue;
        }
        auto &target = global[index];
        target.count.fetch_add(source.count, std::memory_order_relaxed);
        target.total.fetch_add(source.total, std::memory_order_relaxed);
        update_max(target.maximum, source.maximum);
        source = {};
    }
    local.pending = 0;
}

inline void record(Metric metric, uint64_t value) {
    constexpr uint64_t kFlushThreshold = 256;
    auto &local = local_counters();
    auto &counter = local.values[static_cast<size_t>(metric)];
    ++counter.count;
    counter.total += value;
    if (value > counter.maximum) {
        counter.maximum = value;
    }
    if (++local.pending >= kFlushThreshold) {
        flush_thread_local();
    }
}

inline MetricSnapshot read(Metric metric) {
    const auto &counter = metric_counters()[static_cast<size_t>(metric)];
    return {counter.count.load(std::memory_order_relaxed),
            counter.total.load(std::memory_order_relaxed),
            counter.maximum.load(std::memory_order_relaxed)};
}

inline std::string_view metric_name(Metric metric) {
    constexpr std::array<std::string_view, metric_count()> names = {
        "txn_state_wait",
        "commit_apply_read_wait",
        "commit_apply_write_wait",
        "heap_file_wait",
        "index_root_read_wait",
        "index_root_write_wait",
        "wal_durable_wait",
        "wal_fsync",
        "wal_group_size"};
    return names[static_cast<size_t>(metric)];
}

inline bool metric_is_duration(Metric metric) {
    return metric != Metric::WAL_GROUP_SIZE;
}

inline void write_report(std::ostream &output) {
    flush_thread_local();
    output << "RMDB_PERF_DIAG_BEGIN\n";
    for (size_t index = 0; index < metric_count(); ++index) {
        const auto metric = static_cast<Metric>(index);
        const auto value = read(metric);
        output << metric_name(metric) << " count=" << value.count;
        if (metric_is_duration(metric)) {
            output << " total_us=" << value.total_us
                   << " max_us=" << value.max_us;
        } else {
            output << " total=" << value.total_us
                   << " max=" << value.max_us;
        }
        output << '\n';
    }
    output << "RMDB_PERF_DIAG_END\n";
}

inline void reset_for_test() {
    flush_thread_local();
    for (auto &counter : metric_counters()) {
        counter.count.store(0, std::memory_order_relaxed);
        counter.total.store(0, std::memory_order_relaxed);
        counter.maximum.store(0, std::memory_order_relaxed);
    }
}

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

inline std::unique_lock<std::mutex> lock_mutex(
    std::mutex &latch, Metric metric) {
    if (!enabled()) {
        return std::unique_lock<std::mutex>(latch);
    }
    auto start = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(latch);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    record(metric, static_cast<uint64_t>(elapsed));
    return lock;
}

inline std::shared_lock<std::shared_mutex> lock_shared_mutex(
    std::shared_mutex &latch, Metric metric) {
    if (!enabled()) {
        return std::shared_lock<std::shared_mutex>(latch);
    }
    auto start = std::chrono::steady_clock::now();
    std::shared_lock<std::shared_mutex> lock(latch);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    record(metric, static_cast<uint64_t>(elapsed));
    return lock;
}

inline std::unique_lock<std::shared_mutex> lock_unique_shared_mutex(
    std::shared_mutex &latch, Metric metric) {
    if (!enabled()) {
        return std::unique_lock<std::shared_mutex>(latch);
    }
    auto start = std::chrono::steady_clock::now();
    std::unique_lock<std::shared_mutex> lock(latch);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    record(metric, static_cast<uint64_t>(elapsed));
    return lock;
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
    record(Metric::COMMIT_APPLY_READ_WAIT,
           static_cast<uint64_t>(elapsed));
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
    record(Metric::COMMIT_APPLY_WRITE_WAIT,
           static_cast<uint64_t>(elapsed));
    return lock;
}

inline std::unique_lock<std::mutex> lock_mvcc(std::mutex &latch) {
    auto &c = shared_counters();
    return timed_lock(latch, c.mvcc_latch_acquires,
                      c.mvcc_latch_wait_us);
}

}  // namespace rmdb_perf
