#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>

#include "common/writer_priority_shared_mutex.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

template <typename Predicate>
bool wait_until(Predicate predicate,
                std::chrono::milliseconds timeout =
                    std::chrono::milliseconds(500)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

void require_waiting_writer_blocks_late_reader() {
    WriterPrioritySharedMutex latch;
    std::shared_lock<WriterPrioritySharedMutex> initial_reader(latch);

    std::atomic<int> acquisition_order{0};
    std::atomic<int> writer_order{0};
    std::atomic<int> late_reader_order{0};
    std::atomic<bool> release_writer{false};

    std::thread writer([&] {
        std::unique_lock<WriterPrioritySharedMutex> lock(latch);
        writer_order.store(acquisition_order.fetch_add(1) + 1,
                           std::memory_order_release);
        while (!release_writer.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    });

    require(wait_until([&] { return latch.waiting_writers() == 1; }),
            "writer must become visible to the reader gate");

    std::thread late_reader([&] {
        std::shared_lock<WriterPrioritySharedMutex> lock(latch);
        late_reader_order.store(acquisition_order.fetch_add(1) + 1,
                                std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    require(late_reader_order.load(std::memory_order_acquire) == 0,
            "a reader arriving after a queued writer must not barge");

    initial_reader.unlock();
    require(wait_until([&] {
                return writer_order.load(std::memory_order_acquire) != 0;
            }),
            "queued writer must acquire after existing readers leave");
    require(late_reader_order.load(std::memory_order_acquire) == 0,
            "late reader must remain blocked while the writer owns the latch");

    release_writer.store(true, std::memory_order_release);
    writer.join();
    late_reader.join();

    require(writer_order.load(std::memory_order_acquire) == 1,
            "queued writer must acquire before a late reader");
    require(late_reader_order.load(std::memory_order_acquire) == 2,
            "late reader must proceed after the writer releases");
}

}  // namespace

int main() {
    require_waiting_writer_blocks_late_reader();
    std::cout << "writer priority shared mutex tests passed\n";
    return 0;
}
