/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "page_guard.h"

#include "buffer_pool_manager.h"
#include "common/perf_counters.h"

namespace {

template <typename Lock>
void lock_page_content(Lock *lock) {
    if (!rmdb_perf::enabled()) {
        lock->lock();
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    lock->lock();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    rmdb_perf::shared_counters().page_content_latch_wait_us.fetch_add(
        static_cast<uint64_t>(elapsed), std::memory_order_relaxed);
}

}  // namespace

BasicPageGuard::BasicPageGuard(BasicPageGuard &&other) noexcept
    : bpm_(std::exchange(other.bpm_, nullptr)),
      page_(std::exchange(other.page_, nullptr)),
      frame_id_(std::exchange(other.frame_id_, INVALID_FRAME_ID)),
      generation_(std::exchange(other.generation_, 0)),
      dirty_(std::exchange(other.dirty_, false)),
      page_lsn_(std::exchange(other.page_lsn_, INVALID_LSN)) {}

BasicPageGuard &BasicPageGuard::operator=(BasicPageGuard &&other) noexcept {
    if (this != &other) {
        drop();
        bpm_ = std::exchange(other.bpm_, nullptr);
        page_ = std::exchange(other.page_, nullptr);
        frame_id_ = std::exchange(other.frame_id_, INVALID_FRAME_ID);
        generation_ = std::exchange(other.generation_, 0);
        dirty_ = std::exchange(other.dirty_, false);
        page_lsn_ = std::exchange(other.page_lsn_, INVALID_LSN);
    }
    return *this;
}

BasicPageGuard::~BasicPageGuard() { drop(); }

void BasicPageGuard::drop() noexcept {
    if (bpm_ != nullptr && page_ != nullptr) {
        bpm_->unpin_guard(frame_id_, generation_, dirty_, page_lsn_);
    }
    bpm_ = nullptr;
    page_ = nullptr;
    frame_id_ = INVALID_FRAME_ID;
    generation_ = 0;
    dirty_ = false;
    page_lsn_ = INVALID_LSN;
}

ReadPageGuard::ReadPageGuard(BasicPageGuard &&basic,
                             std::shared_mutex &content_latch)
    : BasicPageGuard(std::move(basic)),
      content_lock_(content_latch, std::defer_lock) {
    lock_page_content(&content_lock_);
}

ReadPageGuard::ReadPageGuard(ReadPageGuard &&other) noexcept
    : BasicPageGuard(std::move(other)),
      content_lock_(std::move(other.content_lock_)) {}

ReadPageGuard &ReadPageGuard::operator=(ReadPageGuard &&other) noexcept {
    if (this != &other) {
        drop();
        BasicPageGuard::operator=(std::move(other));
        content_lock_ = std::move(other.content_lock_);
    }
    return *this;
}

ReadPageGuard::~ReadPageGuard() { drop(); }

void ReadPageGuard::drop() noexcept {
    if (content_lock_.owns_lock()) {
        content_lock_.unlock();
    }
    BasicPageGuard::drop();
}

WritePageGuard::WritePageGuard(BasicPageGuard &&basic,
                               std::shared_mutex &content_latch)
    : BasicPageGuard(std::move(basic)),
      content_lock_(content_latch, std::defer_lock) {
    lock_page_content(&content_lock_);
}

WritePageGuard::WritePageGuard(WritePageGuard &&other) noexcept
    : BasicPageGuard(std::move(other)),
      content_lock_(std::move(other.content_lock_)) {}

WritePageGuard &WritePageGuard::operator=(WritePageGuard &&other) noexcept {
    if (this != &other) {
        drop();
        BasicPageGuard::operator=(std::move(other));
        content_lock_ = std::move(other.content_lock_);
    }
    return *this;
}

WritePageGuard::~WritePageGuard() { drop(); }

void WritePageGuard::drop() noexcept {
    if (content_lock_.owns_lock()) {
        content_lock_.unlock();
    }
    BasicPageGuard::drop();
}
