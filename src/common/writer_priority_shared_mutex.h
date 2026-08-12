#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <shared_mutex>

// A shared mutex that prevents newly arriving readers from overtaking a
// queued writer. Existing readers are allowed to finish; once a writer
// announces its intent, late readers retry until every queued/active writer
// has drained.
class WriterPrioritySharedMutex {
   public:
    WriterPrioritySharedMutex() = default;
    WriterPrioritySharedMutex(const WriterPrioritySharedMutex &) = delete;
    WriterPrioritySharedMutex &operator=(
        const WriterPrioritySharedMutex &) = delete;

    void lock() {
        writer_intents_.fetch_add(1, std::memory_order_acq_rel);
        try {
            latch_.lock();
        } catch (...) {
            release_writer_intent();
            throw;
        }
    }

    bool try_lock() {
        writer_intents_.fetch_add(1, std::memory_order_acq_rel);
        try {
            if (latch_.try_lock()) {
                return true;
            }
            release_writer_intent();
            return false;
        } catch (...) {
            release_writer_intent();
            throw;
        }
    }

    void unlock() {
        latch_.unlock();
        release_writer_intent();
    }

    void lock_shared() {
        while (true) {
            if (writer_intents_.load(std::memory_order_acquire) != 0) {
                std::unique_lock<std::mutex> gate_lock(gate_latch_);
                gate_cv_.wait(gate_lock, [&] {
                    return writer_intents_.load(
                               std::memory_order_acquire) == 0;
                });
                continue;
            }

            latch_.lock_shared();
            if (writer_intents_.load(std::memory_order_acquire) == 0) {
                return;
            }

            latch_.unlock_shared();
        }
    }

    bool try_lock_shared() {
        if (writer_intents_.load(std::memory_order_acquire) != 0 ||
            !latch_.try_lock_shared()) {
            return false;
        }
        if (writer_intents_.load(std::memory_order_acquire) == 0) {
            return true;
        }
        latch_.unlock_shared();
        return false;
    }

    void unlock_shared() { latch_.unlock_shared(); }

    uint32_t waiting_writers() const {
        return writer_intents_.load(std::memory_order_acquire);
    }

   private:
    void release_writer_intent() {
        std::lock_guard<std::mutex> gate_guard(gate_latch_);
        if (writer_intents_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            gate_cv_.notify_all();
        }
    }

    std::shared_mutex latch_;
    // Includes both queued writers and the writer currently owning latch_.
    // Keeping the active writer represented closes the hand-off window in
    // which late readers could otherwise enter the underlying shared_mutex
    // and overtake the next queued writer.
    std::atomic<uint32_t> writer_intents_{0};
    std::mutex gate_latch_;
    std::condition_variable gate_cv_;
};
