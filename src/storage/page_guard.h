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

#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include "common/config.h"
#include "page.h"

class BufferPoolManager;

/**
 * A move-only pin owner. ReadPageGuard and WritePageGuard add the appropriate
 * page-content latch while keeping pin release and dirty propagation automatic.
 */
class BasicPageGuard {
    friend class BufferPoolManager;
    friend class ReadPageGuard;
    friend class WritePageGuard;

   public:
    BasicPageGuard() = default;
    BasicPageGuard(const BasicPageGuard &) = delete;
    BasicPageGuard &operator=(const BasicPageGuard &) = delete;

    BasicPageGuard(BasicPageGuard &&other) noexcept;
    BasicPageGuard &operator=(BasicPageGuard &&other) noexcept;
    ~BasicPageGuard();

    const Page *get_page() const { return page_; }
    bool is_valid() const { return page_ != nullptr; }
    void mark_dirty() { dirty_ = true; }
    void set_page_lsn(lsn_t page_lsn) { page_lsn_ = page_lsn; }
    void drop() noexcept;

   private:
    BasicPageGuard(BufferPoolManager *bpm, Page *page, frame_id_t frame_id,
                   uint64_t generation)
        : bpm_(bpm), page_(page), frame_id_(frame_id),
          generation_(generation) {}

    BufferPoolManager *bpm_ = nullptr;
    Page *page_ = nullptr;
    frame_id_t frame_id_ = INVALID_FRAME_ID;
    uint64_t generation_ = 0;
    bool dirty_ = false;
    lsn_t page_lsn_ = INVALID_LSN;
};

class ReadPageGuard : private BasicPageGuard {
    friend class BufferPoolManager;

   public:
    ReadPageGuard() = default;
    ReadPageGuard(const ReadPageGuard &) = delete;
    ReadPageGuard &operator=(const ReadPageGuard &) = delete;

    ReadPageGuard(ReadPageGuard &&other) noexcept;
    ReadPageGuard &operator=(ReadPageGuard &&other) noexcept;
    ~ReadPageGuard();

    const Page *get_page() const { return BasicPageGuard::get_page(); }
    const char *data() const {
        return page_ == nullptr ? nullptr : page_->get_data();
    }
    bool is_valid() const { return BasicPageGuard::is_valid(); }
    uint64_t generation() const { return generation_; }
    void drop() noexcept;

   private:
    ReadPageGuard(BasicPageGuard &&basic, std::shared_mutex &content_latch);

    std::shared_lock<std::shared_mutex> content_lock_;
};

class WritePageGuard : private BasicPageGuard {
    friend class BufferPoolManager;

   public:
    WritePageGuard() = default;
    WritePageGuard(const WritePageGuard &) = delete;
    WritePageGuard &operator=(const WritePageGuard &) = delete;

    WritePageGuard(WritePageGuard &&other) noexcept;
    WritePageGuard &operator=(WritePageGuard &&other) noexcept;
    ~WritePageGuard();

    Page *get_page() { return page_; }
    const Page *get_page() const { return page_; }
    char *data() { return page_ == nullptr ? nullptr : page_->get_data(); }
    const char *data() const {
        return page_ == nullptr ? nullptr : page_->get_data();
    }
    bool is_valid() const { return BasicPageGuard::is_valid(); }
    uint64_t generation() const { return generation_; }
    void mark_dirty() { BasicPageGuard::mark_dirty(); }
    void set_page_lsn(lsn_t page_lsn, bool persist_in_page = false) {
        BasicPageGuard::set_page_lsn(page_lsn);
        if (persist_in_page && page_ != nullptr) {
            page_->set_page_lsn(page_lsn);
        }
    }
    void drop() noexcept;

   private:
    WritePageGuard(BasicPageGuard &&basic, std::shared_mutex &content_latch);

    std::unique_lock<std::shared_mutex> content_lock_;
};
