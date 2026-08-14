/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once
#include <fcntl.h>
#include <unistd.h>

#include <cassert>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "disk_manager.h"
#include "errors.h"
#include "page.h"
#include "page_guard.h"
#include "replacer/lru_replacer.h"
#include "replacer/replacer.h"

class LogManager;

class BufferPoolManager {
    friend class BasicPageGuard;

   private:
    enum class FrameState { FREE, LOADING, VALID, EVICTING };

    struct FrameControl {
        std::mutex meta_latch;
        std::shared_mutex content_latch;
        std::condition_variable io_cv;
        FrameState state = FrameState::FREE;
        uint64_t generation = 0;
        uint64_t dirty_epoch = 0;
        uint32_t flush_pin_count = 0;
        lsn_t page_lsn = INVALID_LSN;
    };

    static constexpr size_t kPageTableShardCount = 64;

    struct PageTableShard {
        std::mutex latch;
        std::unordered_map<PageId, frame_id_t, PageIdHash> pages;
    };

    size_t pool_size_;      // buffer_pool中可容纳页面的个数，即帧的个数
    Page *pages_;           // buffer_pool中的Page对象数组，在构造空间中申请内存空间，在析构函数中释放，大小为BUFFER_POOL_SIZE
    std::array<PageTableShard, kPageTableShardCount> page_table_shards_;
    std::list<frame_id_t> free_list_;   // 空闲帧编号的链表
    DiskManager *disk_manager_;
    LogManager *log_manager_ = nullptr;
    Replacer *replacer_;    // buffer_pool的置换策略，当前赛题中为LRU置换策略
    std::mutex victim_latch_;  // serializes free-list/victim reservation only
    std::unique_ptr<FrameControl[]> frame_controls_;

   public:
    BufferPoolManager(size_t pool_size, DiskManager *disk_manager)
        : pool_size_(pool_size), disk_manager_(disk_manager) {
        for (auto &shard : page_table_shards_) {
            shard.pages.reserve(pool_size_ / kPageTableShardCount + 1);
        }
        // 为buffer pool分配一块连续的内存空间
        pages_ = new Page[pool_size_];
        frame_controls_ = std::make_unique<FrameControl[]>(pool_size_);
        // 可以被Replacer改变
        if (REPLACER_TYPE.compare("LRU"))
            replacer_ = new LRUReplacer(pool_size_);
        else if (REPLACER_TYPE.compare("CLOCK"))
            replacer_ = new LRUReplacer(pool_size_);
        else {
            replacer_ = new LRUReplacer(pool_size_);
        }
        // 初始化时，所有的page都在free_list_中
        for (size_t i = 0; i < pool_size_; ++i) {
            free_list_.emplace_back(static_cast<frame_id_t>(i));  // static_cast转换数据类型
        }
    }

    ~BufferPoolManager() {
        delete[] pages_;
        delete replacer_;
    }

    /**
     * @description: 将目标页面标记为脏页
     * @param {Page*} page 脏页
     */
    static void mark_dirty(Page* page) { page->is_dirty_ = true; }

    void set_log_manager(LogManager *log_manager) { log_manager_ = log_manager; }

   public: 
    // Legacy raw-pointer interface. New storage code should prefer guards.
    Page* fetch_page(PageId page_id);

    ReadPageGuard fetch_page_read(PageId page_id);

    WritePageGuard fetch_page_write(PageId page_id);

    // Legacy companion to fetch_page().
    bool unpin_page(PageId page_id, bool is_dirty);

    bool flush_page(PageId page_id);

    Page* new_page(PageId* page_id);

    WritePageGuard new_page_guarded(PageId* page_id);

    bool delete_page(PageId page_id);

    void flush_all_pages(int fd);

    void flush_all_pages();

    void discard_all_pages(int fd);

   private:
    PageTableShard &page_table_shard(PageId page_id);

    size_t page_table_shard_index(PageId page_id) const;

    bool find_victim_page(frame_id_t *frame_id, bool *from_free_list);

    bool select_victim_frame(frame_id_t *frame_id, PageId *old_page_id,
                             bool *from_free_list);

    void release_reserved_frame(frame_id_t frame_id, PageId old_page_id,
                                bool from_free_list);

    Page *pin_cached_page(PageId page_id, bool flush_pin = false);

    void unpin_flush(frame_id_t frame_id, uint64_t generation) noexcept;

    void rollback_loading_frame(PageId page_id, frame_id_t frame_id,
                                uint64_t generation);

    void flush_wal_before_page_write(lsn_t page_lsn);

    BasicPageGuard make_guard(Page *page);

    void unpin_guard(frame_id_t frame_id, uint64_t generation,
                     bool is_dirty, lsn_t page_lsn) noexcept;
};
