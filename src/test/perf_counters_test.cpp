#include <cassert>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "common/perf_counters.h"

int main() {
    rmdb_perf::reset_for_test();

    constexpr int kThreads = 4;
    constexpr int kIterations = 1000;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int thread = 0; thread < kThreads; ++thread) {
        workers.emplace_back([] {
            for (int i = 0; i < kIterations; ++i) {
                rmdb_perf::record(
                    rmdb_perf::Metric::TXN_STATE_WAIT, 3);
                rmdb_perf::record(
                    rmdb_perf::Metric::HEAP_FILE_WAIT, 2);
            }
            rmdb_perf::flush_thread_local();
        });
    }
    for (auto &worker : workers) {
        worker.join();
    }

    const auto txn_state =
        rmdb_perf::read(rmdb_perf::Metric::TXN_STATE_WAIT);
    assert(txn_state.count == kThreads * kIterations);
    assert(txn_state.total_us ==
           static_cast<uint64_t>(kThreads * kIterations * 3));
    assert(txn_state.max_us == 3);

    const auto heap =
        rmdb_perf::read(rmdb_perf::Metric::HEAP_FILE_WAIT);
    assert(heap.count == kThreads * kIterations);
    assert(heap.total_us ==
           static_cast<uint64_t>(kThreads * kIterations * 2));
    assert(heap.max_us == 2);

    rmdb_perf::record(rmdb_perf::Metric::WAL_GROUP_SIZE, 7);
    rmdb_perf::record(rmdb_perf::Metric::WAL_GROUP_SIZE, 2);
    rmdb_perf::flush_thread_local();
    const auto wal_group =
        rmdb_perf::read(rmdb_perf::Metric::WAL_GROUP_SIZE);
    assert(wal_group.count == 2);
    assert(wal_group.total_us == 9);
    assert(wal_group.max_us == 7);

    std::ostringstream report;
    rmdb_perf::write_report(report);
    const std::string text = report.str();
    assert(text.find("txn_state_wait count=4000 total_us=12000 max_us=3") !=
           std::string::npos);
    assert(text.find("wal_group_size count=2 total=9 max=7") !=
           std::string::npos);
    assert(text.find("sql=") == std::string::npos);

    return 0;
}
