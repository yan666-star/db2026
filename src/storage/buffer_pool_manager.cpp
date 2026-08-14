/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "buffer_pool_manager.h"

#include <array>
#include <cstring>
#include <utility>
#include <vector>

#include "common/perf_counters.h"
#include "recovery/log_manager.h"

void BufferPoolManager::flush_wal_before_page_write(lsn_t page_lsn) {
    if (log_manager_ != nullptr && page_lsn != INVALID_LSN) {
        log_manager_->force_flush_up_to(page_lsn);
    }
}

size_t BufferPoolManager::page_table_shard_index(PageId page_id) const {
    return PageIdHash{}(page_id) % kPageTableShardCount;
}

BufferPoolManager::PageTableShard &BufferPoolManager::page_table_shard(
    PageId page_id) {
    return page_table_shards_[page_table_shard_index(page_id)];
}

bool BufferPoolManager::find_victim_page(frame_id_t *frame_id,
                                         bool *from_free_list) {
    if (!free_list_.empty()) {
        *frame_id = free_list_.front();
        free_list_.pop_front();
        *from_free_list = true;
        return true;
    }
    *from_free_list = false;
    return replacer_->victim(frame_id);
}

bool BufferPoolManager::select_victim_frame(frame_id_t *frame_id,
                                            PageId *old_page_id,
                                            bool *from_free_list) {
    while (find_victim_page(frame_id, from_free_list)) {
        FrameControl &control = frame_controls_[*frame_id];
        auto frame_lock = rmdb_perf::lock_buffer_frame(control.meta_latch);
        Page *page = &pages_[*frame_id];

        if (*from_free_list) {
            if (control.state != FrameState::FREE || page->pin_count_ != 0) {
                continue;
            }
        } else if (control.state != FrameState::VALID ||
                   page->pin_count_ != 0) {
            // A hit can race with victim selection after the replacer has
            // returned a frame. In that case the hit owns the frame now.
            continue;
        }

        *old_page_id = page->id_;
        control.state = FrameState::EVICTING;
        control.io_cv.notify_all();
        return true;
    }
    return false;
}

void BufferPoolManager::release_reserved_frame(frame_id_t frame_id,
                                                PageId old_page_id,
                                                bool from_free_list) {
    FrameControl &control = frame_controls_[frame_id];
    auto frame_lock = rmdb_perf::lock_buffer_frame(control.meta_latch);
    Page *page = &pages_[frame_id];
    if (from_free_list || old_page_id.page_no == INVALID_PAGE_ID) {
        page->id_ = PageId{old_page_id.fd, INVALID_PAGE_ID};
        control.state = FrameState::FREE;
        free_list_.push_front(frame_id);
    } else {
        control.state = FrameState::VALID;
        replacer_->unpin(frame_id);
    }
    control.io_cv.notify_all();
}

Page *BufferPoolManager::pin_cached_page(PageId page_id, bool flush_pin) {
    for (;;) {
        PageTableShard &shard = page_table_shard(page_id);
        std::unique_lock<std::mutex> shard_lock(shard.latch);
        auto it = shard.pages.find(page_id);
        if (it == shard.pages.end()) {
            return nullptr;
        }

        frame_id_t frame_id = it->second;
        FrameControl &control = frame_controls_[frame_id];
        std::unique_lock<std::mutex> frame_lock(control.meta_latch);
        Page *page = &pages_[frame_id];
        if (page->id_ != page_id) {
            shard.pages.erase(it);
            continue;
        }

        if (control.state == FrameState::VALID) {
            bool was_unpinned = page->pin_count_ == 0;
            page->pin_count_++;
            if (flush_pin) {
                control.flush_pin_count++;
            }
            if (was_unpinned) {
                replacer_->pin(frame_id);
            }
            return page;
        }

        uint64_t generation = control.generation;
        shard_lock.unlock();
        control.io_cv.wait(frame_lock, [&] {
            return control.generation != generation ||
                   control.state == FrameState::VALID ||
                   control.state == FrameState::FREE;
        });
        // Recheck both the mapping and generation from the shard. The frame
        // may have completed this load, reverted an eviction, or been reused.
    }
}

Page *BufferPoolManager::fetch_page(PageId page_id) {
    const bool diagnose = rmdb_perf::enabled();
    if (diagnose) {
        rmdb_perf::shared_counters().buffer_fetches.fetch_add(
            1, std::memory_order_relaxed);
    }

    bool counted_miss = false;
    for (;;) {
        if (Page *cached = pin_cached_page(page_id); cached != nullptr) {
            if (diagnose && !counted_miss) {
                rmdb_perf::shared_counters().buffer_hits.fetch_add(
                    1, std::memory_order_relaxed);
            }
            return cached;
        }

        std::unique_lock<std::mutex> victim_lock(victim_latch_);
        {
            PageTableShard &target_shard = page_table_shard(page_id);
            std::lock_guard<std::mutex> target_lock(target_shard.latch);
            if (target_shard.pages.find(page_id) !=
                target_shard.pages.end()) {
                continue;
            }
        }

        if (!counted_miss) {
            counted_miss = true;
            if (diagnose) {
                rmdb_perf::shared_counters().buffer_misses.fetch_add(
                    1, std::memory_order_relaxed);
            }
        }

        frame_id_t frame_id = INVALID_FRAME_ID;
        PageId old_page_id{-1, INVALID_PAGE_ID};
        bool from_free_list = false;
        if (!select_victim_frame(&frame_id, &old_page_id,
                                 &from_free_list)) {
            return nullptr;
        }

        const size_t target_index = page_table_shard_index(page_id);
        const bool has_old_mapping =
            old_page_id.page_no != INVALID_PAGE_ID;
        const size_t old_index = has_old_mapping
                                     ? page_table_shard_index(old_page_id)
                                     : target_index;
        const size_t first_index = std::min(target_index, old_index);
        const size_t second_index = std::max(target_index, old_index);
        std::unique_lock<std::mutex> first_lock(
            page_table_shards_[first_index].latch);
        std::unique_lock<std::mutex> second_lock;
        if (second_index != first_index) {
            second_lock = std::unique_lock<std::mutex>(
                page_table_shards_[second_index].latch);
        }

        PageTableShard &target_shard = page_table_shards_[target_index];
        if (target_shard.pages.find(page_id) != target_shard.pages.end()) {
            if (second_lock.owns_lock()) {
                second_lock.unlock();
            }
            first_lock.unlock();
            release_reserved_frame(frame_id, old_page_id, from_free_list);
            continue;
        }

        FrameControl &control = frame_controls_[frame_id];
        std::unique_lock<std::mutex> frame_lock(control.meta_latch);
        Page *page = &pages_[frame_id];
        if (control.state != FrameState::EVICTING ||
            page->pin_count_ != 0 || page->id_ != old_page_id) {
            frame_lock.unlock();
            if (second_lock.owns_lock()) {
                second_lock.unlock();
            }
            first_lock.unlock();
            release_reserved_frame(frame_id, old_page_id, from_free_list);
            continue;
        }

        if (has_old_mapping) {
            PageTableShard &old_shard = page_table_shards_[old_index];
            auto old_it = old_shard.pages.find(old_page_id);
            if (old_it != old_shard.pages.end() &&
                old_it->second == frame_id) {
                old_shard.pages.erase(old_it);
            }
        }

        const bool old_dirty = page->is_dirty_;
        const lsn_t old_page_lsn = control.page_lsn;
        control.generation++;
        const uint64_t generation = control.generation;
        page->id_ = page_id;
        page->pin_count_ = 1;
        control.state = FrameState::LOADING;
        target_shard.pages.emplace(page_id, frame_id);
        frame_lock.unlock();
        if (second_lock.owns_lock()) {
            second_lock.unlock();
        }
        first_lock.unlock();
        victim_lock.unlock();
        control.io_cv.notify_all();

        try {
            std::unique_lock<std::shared_mutex> content_lock(
                control.content_latch);
            if (old_dirty && has_old_mapping) {
                flush_wal_before_page_write(old_page_lsn);
                disk_manager_->write_page(old_page_id.fd,
                                          old_page_id.page_no,
                                          page->get_data(), PAGE_SIZE);
            }
            disk_manager_->read_page(page_id.fd, page_id.page_no,
                                     page->get_data(), PAGE_SIZE);

            std::lock_guard<std::mutex> loaded_lock(control.meta_latch);
            if (control.generation == generation && page->id_ == page_id &&
                control.state == FrameState::LOADING) {
                page->is_dirty_ = false;
                control.page_lsn = INVALID_LSN;
                control.dirty_epoch++;
                control.state = FrameState::VALID;
            }
        } catch (...) {
            rollback_loading_frame(page_id, frame_id, generation);
            throw;
        }
        control.io_cv.notify_all();
        return page;
    }
}

ReadPageGuard BufferPoolManager::fetch_page_read(PageId page_id) {
    Page *page = fetch_page(page_id);
    if (page == nullptr) {
        return {};
    }
    BasicPageGuard basic = make_guard(page);
    if (!basic.is_valid()) {
        unpin_page(page_id, false);
        return {};
    }
    frame_id_t frame_id = basic.frame_id_;
    return ReadPageGuard(std::move(basic),
                         frame_controls_[frame_id].content_latch);
}

WritePageGuard BufferPoolManager::fetch_page_write(PageId page_id) {
    Page *page = fetch_page(page_id);
    if (page == nullptr) {
        return {};
    }
    BasicPageGuard basic = make_guard(page);
    if (!basic.is_valid()) {
        unpin_page(page_id, false);
        return {};
    }
    frame_id_t frame_id = basic.frame_id_;
    return WritePageGuard(std::move(basic),
                          frame_controls_[frame_id].content_latch);
}

bool BufferPoolManager::unpin_page(PageId page_id, bool is_dirty) {
    PageTableShard &shard = page_table_shard(page_id);
    std::lock_guard<std::mutex> shard_lock(shard.latch);
    auto it = shard.pages.find(page_id);
    if (it == shard.pages.end()) {
        return false;
    }

    frame_id_t frame_id = it->second;
    FrameControl &control = frame_controls_[frame_id];
    auto frame_lock = rmdb_perf::lock_buffer_frame(control.meta_latch);
    Page *page = &pages_[frame_id];
    if (page->id_ != page_id || control.state != FrameState::VALID ||
        page->pin_count_ <= 0) {
        return false;
    }

    page->pin_count_--;
    if (is_dirty) {
        page->is_dirty_ = true;
        control.dirty_epoch++;
    }
    if (page->pin_count_ == 0) {
        replacer_->unpin(frame_id);
    }
    return true;
}

bool BufferPoolManager::flush_page(PageId page_id) {
    Page *page = pin_cached_page(page_id, true);
    if (page == nullptr) {
        return false;
    }

    frame_id_t frame_id = static_cast<frame_id_t>(page - pages_);
    FrameControl &control = frame_controls_[frame_id];
    std::array<char, PAGE_SIZE> snapshot{};
    uint64_t generation = 0;
    uint64_t dirty_epoch = 0;
    lsn_t page_lsn = INVALID_LSN;
    try {
        bool valid = false;
        bool dirty = false;
        {
            std::shared_lock<std::shared_mutex> content_lock(
                control.content_latch);
            std::lock_guard<std::mutex> frame_lock(control.meta_latch);
            generation = control.generation;
            valid = control.state == FrameState::VALID &&
                    page->id_ == page_id;
            dirty = valid && page->is_dirty_;
            if (dirty) {
                dirty_epoch = control.dirty_epoch;
                page_lsn = control.page_lsn;
                std::memcpy(snapshot.data(), page->get_data(), PAGE_SIZE);
            }
        }
        if (!valid || !dirty) {
            unpin_flush(frame_id, generation);
            return valid;
        }

        flush_wal_before_page_write(page_lsn);
        disk_manager_->write_page(page_id.fd, page_id.page_no,
                                  snapshot.data(), PAGE_SIZE);

        {
            std::shared_lock<std::shared_mutex> content_lock(
                control.content_latch);
            std::lock_guard<std::mutex> frame_lock(control.meta_latch);
            if (control.state == FrameState::VALID &&
                control.generation == generation &&
                page->id_ == page_id &&
                control.dirty_epoch == dirty_epoch) {
                page->is_dirty_ = false;
            }
        }
    } catch (...) {
        unpin_flush(frame_id, generation);
        throw;
    }
    unpin_flush(frame_id, generation);
    return true;
}

Page *BufferPoolManager::new_page(PageId *page_id) {
    std::unique_lock<std::mutex> victim_lock(victim_latch_);
    frame_id_t frame_id = INVALID_FRAME_ID;
    PageId old_page_id{-1, INVALID_PAGE_ID};
    bool from_free_list = false;
    if (!select_victim_frame(&frame_id, &old_page_id, &from_free_list)) {
        return nullptr;
    }

    PageId new_page_id = *page_id;
    try {
        new_page_id.page_no = disk_manager_->allocate_page(new_page_id.fd);
    } catch (...) {
        release_reserved_frame(frame_id, old_page_id, from_free_list);
        throw;
    }

    const bool has_old_mapping = old_page_id.page_no != INVALID_PAGE_ID;
    const size_t new_index = page_table_shard_index(new_page_id);
    const size_t old_index = has_old_mapping
                                 ? page_table_shard_index(old_page_id)
                                 : new_index;
    const size_t first_index = std::min(new_index, old_index);
    const size_t second_index = std::max(new_index, old_index);
    std::unique_lock<std::mutex> first_lock(
        page_table_shards_[first_index].latch);
    std::unique_lock<std::mutex> second_lock;
    if (second_index != first_index) {
        second_lock = std::unique_lock<std::mutex>(
            page_table_shards_[second_index].latch);
    }

    FrameControl &control = frame_controls_[frame_id];
    std::unique_lock<std::mutex> frame_lock(control.meta_latch);
    Page *page = &pages_[frame_id];
    if (has_old_mapping) {
        PageTableShard &old_shard = page_table_shards_[old_index];
        auto old_it = old_shard.pages.find(old_page_id);
        if (old_it != old_shard.pages.end() && old_it->second == frame_id) {
            old_shard.pages.erase(old_it);
        }
    }

    const bool old_dirty = page->is_dirty_;
    const lsn_t old_page_lsn = control.page_lsn;
    control.generation++;
    const uint64_t generation = control.generation;
    page->id_ = new_page_id;
    page->pin_count_ = 1;
    control.state = FrameState::LOADING;
    page_table_shards_[new_index].pages.emplace(new_page_id, frame_id);
    frame_lock.unlock();
    if (second_lock.owns_lock()) {
        second_lock.unlock();
    }
    first_lock.unlock();
    victim_lock.unlock();
    control.io_cv.notify_all();

    try {
        std::unique_lock<std::shared_mutex> content_lock(
            control.content_latch);
        if (old_dirty && has_old_mapping) {
            flush_wal_before_page_write(old_page_lsn);
            disk_manager_->write_page(old_page_id.fd, old_page_id.page_no,
                                      page->get_data(), PAGE_SIZE);
        }
        std::memset(page->data_, 0, PAGE_SIZE);
        std::lock_guard<std::mutex> loaded_lock(control.meta_latch);
        if (control.generation == generation && page->id_ == new_page_id &&
            control.state == FrameState::LOADING) {
            page->is_dirty_ = false;
            control.page_lsn = INVALID_LSN;
            control.dirty_epoch++;
            control.state = FrameState::VALID;
        }
    } catch (...) {
        rollback_loading_frame(new_page_id, frame_id, generation);
        throw;
    }
    control.io_cv.notify_all();
    *page_id = new_page_id;
    return page;
}

WritePageGuard BufferPoolManager::new_page_guarded(PageId *page_id) {
    Page *page = new_page(page_id);
    if (page == nullptr) {
        return {};
    }
    BasicPageGuard basic = make_guard(page);
    if (!basic.is_valid()) {
        unpin_page(*page_id, false);
        return {};
    }
    frame_id_t frame_id = basic.frame_id_;
    return WritePageGuard(std::move(basic),
                          frame_controls_[frame_id].content_latch);
}

bool BufferPoolManager::delete_page(PageId page_id) {
    for (;;) {
        std::unique_lock<std::mutex> victim_lock(victim_latch_);
        PageTableShard &shard = page_table_shard(page_id);
        std::unique_lock<std::mutex> shard_lock(shard.latch);
        auto it = shard.pages.find(page_id);
        if (it == shard.pages.end()) {
            return true;
        }

        frame_id_t frame_id = it->second;
        FrameControl &control = frame_controls_[frame_id];
        std::unique_lock<std::mutex> frame_lock(control.meta_latch);
        Page *page = &pages_[frame_id];
        if (page->id_ != page_id) {
            shard.pages.erase(it);
            continue;
        }
        if (control.state != FrameState::VALID) {
            uint64_t generation = control.generation;
            shard_lock.unlock();
            victim_lock.unlock();
            control.io_cv.wait(frame_lock, [&] {
                return control.generation != generation ||
                       control.state == FrameState::VALID ||
                       control.state == FrameState::FREE;
            });
            continue;
        }
        if (page->pin_count_ > 0) {
            if (page->pin_count_ !=
                static_cast<int>(control.flush_pin_count)) {
                return false;
            }
            uint64_t generation = control.generation;
            shard_lock.unlock();
            victim_lock.unlock();
            control.io_cv.wait(frame_lock, [&] {
                return control.generation != generation ||
                       control.flush_pin_count == 0;
            });
            continue;
        }

        replacer_->pin(frame_id);
        const bool dirty = page->is_dirty_;
        const lsn_t page_lsn = control.page_lsn;
        const uint64_t generation = control.generation;
        control.state = FrameState::EVICTING;
        frame_lock.unlock();
        shard_lock.unlock();
        victim_lock.unlock();
        control.io_cv.notify_all();

        try {
            std::unique_lock<std::shared_mutex> content_lock(
                control.content_latch);
            if (dirty) {
                flush_wal_before_page_write(page_lsn);
                disk_manager_->write_page(page_id.fd, page_id.page_no,
                                          page->get_data(), PAGE_SIZE);
            }
            std::memset(page->data_, 0, PAGE_SIZE);
        } catch (...) {
            std::lock_guard<std::mutex> restore_victim(victim_latch_);
            std::lock_guard<std::mutex> restore_shard(shard.latch);
            std::lock_guard<std::mutex> restore_frame(control.meta_latch);
            if (control.generation == generation && page->id_ == page_id &&
                control.state == FrameState::EVICTING) {
                control.state = FrameState::VALID;
                replacer_->unpin(frame_id);
            }
            control.io_cv.notify_all();
            throw;
        }

        victim_lock.lock();
        shard_lock.lock();
        frame_lock.lock();
        auto current = shard.pages.find(page_id);
        if (current != shard.pages.end() && current->second == frame_id &&
            control.generation == generation && page->id_ == page_id &&
            control.state == FrameState::EVICTING) {
            shard.pages.erase(current);
            page->id_ = PageId{page_id.fd, INVALID_PAGE_ID};
            page->is_dirty_ = false;
            page->pin_count_ = 0;
            control.generation++;
            control.dirty_epoch++;
            control.page_lsn = INVALID_LSN;
            control.state = FrameState::FREE;
            free_list_.push_back(frame_id);
        }
        frame_lock.unlock();
        shard_lock.unlock();
        victim_lock.unlock();
        control.io_cv.notify_all();
        return true;
    }
}

void BufferPoolManager::flush_all_pages(int fd) {
    std::vector<PageId> page_ids;
    for (auto &shard : page_table_shards_) {
        std::lock_guard<std::mutex> shard_lock(shard.latch);
        for (const auto &entry : shard.pages) {
            if (entry.first.fd == fd) {
                page_ids.push_back(entry.first);
            }
        }
    }
    for (PageId page_id : page_ids) {
        flush_page(page_id);
    }
}

void BufferPoolManager::flush_all_pages() {
    std::vector<PageId> page_ids;
    for (auto &shard : page_table_shards_) {
        std::lock_guard<std::mutex> shard_lock(shard.latch);
        for (const auto &entry : shard.pages) {
            page_ids.push_back(entry.first);
        }
    }
    for (PageId page_id : page_ids) {
        flush_page(page_id);
    }
}

void BufferPoolManager::discard_all_pages(int fd) {
    std::lock_guard<std::mutex> victim_lock(victim_latch_);
    for (auto &shard : page_table_shards_) {
        std::lock_guard<std::mutex> shard_lock(shard.latch);
        for (auto it = shard.pages.begin(); it != shard.pages.end();) {
            if (it->first.fd != fd) {
                ++it;
                continue;
            }

            frame_id_t frame_id = it->second;
            FrameControl &control = frame_controls_[frame_id];
            std::lock_guard<std::mutex> frame_lock(control.meta_latch);
            Page *page = &pages_[frame_id];
            if (page->pin_count_ != 0 ||
                control.state != FrameState::VALID) {
                throw InternalError(
                    "Cannot close a file with pinned or loading buffer pages");
            }
            replacer_->pin(frame_id);
            page->id_ = PageId{fd, INVALID_PAGE_ID};
            page->is_dirty_ = false;
            page->pin_count_ = 0;
            std::memset(page->data_, 0, PAGE_SIZE);
            control.generation++;
            control.dirty_epoch++;
            control.page_lsn = INVALID_LSN;
            control.state = FrameState::FREE;
            free_list_.push_back(frame_id);
            it = shard.pages.erase(it);
            control.io_cv.notify_all();
        }
    }
}

BasicPageGuard BufferPoolManager::make_guard(Page *page) {
    if (page == nullptr || page < pages_ || page >= pages_ + pool_size_) {
        return {};
    }

    frame_id_t frame_id = static_cast<frame_id_t>(page - pages_);
    FrameControl &control = frame_controls_[frame_id];
    auto frame_lock = rmdb_perf::lock_buffer_frame(control.meta_latch);
    if (page->pin_count_ <= 0 || control.state != FrameState::VALID) {
        return {};
    }
    return BasicPageGuard(this, page, frame_id, control.generation);
}

void BufferPoolManager::unpin_guard(frame_id_t frame_id, uint64_t generation,
                                    bool is_dirty,
                                    lsn_t page_lsn) noexcept {
    if (frame_id < 0 || static_cast<size_t>(frame_id) >= pool_size_) {
        return;
    }

    FrameControl &control = frame_controls_[frame_id];
    auto frame_lock = rmdb_perf::lock_buffer_frame(control.meta_latch);
    Page *page = &pages_[frame_id];
    if (control.generation != generation ||
        control.state != FrameState::VALID || page->pin_count_ <= 0) {
        return;
    }

    page->pin_count_--;
    if (is_dirty) {
        page->is_dirty_ = true;
        control.dirty_epoch++;
        if (page_lsn != INVALID_LSN &&
            (control.page_lsn == INVALID_LSN ||
             page_lsn > control.page_lsn)) {
            control.page_lsn = page_lsn;
        }
    }
    if (page->pin_count_ == 0) {
        replacer_->unpin(frame_id);
    }
}

void BufferPoolManager::unpin_flush(frame_id_t frame_id,
                                    uint64_t generation) noexcept {
    if (frame_id < 0 || static_cast<size_t>(frame_id) >= pool_size_) {
        return;
    }
    FrameControl &control = frame_controls_[frame_id];
    auto frame_lock = rmdb_perf::lock_buffer_frame(control.meta_latch);
    Page *page = &pages_[frame_id];
    if (control.generation != generation || page->pin_count_ <= 0 ||
        control.flush_pin_count == 0) {
        return;
    }
    control.flush_pin_count--;
    page->pin_count_--;
    if (page->pin_count_ == 0) {
        replacer_->unpin(frame_id);
    }
    control.io_cv.notify_all();
}

void BufferPoolManager::rollback_loading_frame(PageId page_id,
                                               frame_id_t frame_id,
                                               uint64_t generation) {
    std::lock_guard<std::mutex> victim_lock(victim_latch_);
    PageTableShard &shard = page_table_shard(page_id);
    std::lock_guard<std::mutex> shard_lock(shard.latch);
    FrameControl &control = frame_controls_[frame_id];
    std::lock_guard<std::mutex> frame_lock(control.meta_latch);
    Page *page = &pages_[frame_id];
    if (control.generation != generation || page->id_ != page_id ||
        control.state != FrameState::LOADING) {
        return;
    }

    auto it = shard.pages.find(page_id);
    if (it != shard.pages.end() && it->second == frame_id) {
        shard.pages.erase(it);
    }
    page->id_ = PageId{page_id.fd, INVALID_PAGE_ID};
    page->pin_count_ = 0;
    page->is_dirty_ = false;
    std::memset(page->data_, 0, PAGE_SIZE);
    control.generation++;
    control.dirty_epoch++;
    control.page_lsn = INVALID_LSN;
    control.state = FrameState::FREE;
    free_list_.push_back(frame_id);
    control.io_cv.notify_all();
}
