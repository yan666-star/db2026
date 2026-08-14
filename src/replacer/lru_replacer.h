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

#include <mutex>
#include <vector>

#include "common/config.h"
#include "replacer/replacer.h"

/*
LRUReplacer实现了LRU替换策略
*/
class LRUReplacer : public Replacer {
   public:
    /**
     * @description: 创建一个新的LRUReplacer
     * @param {size_t} num_pages LRUReplacer最多需要存储的page数量
     */
    explicit LRUReplacer(size_t num_pages);

    ~LRUReplacer();

    bool victim(frame_id_t *frame_id);

    void pin(frame_id_t frame_id);

    void unpin(frame_id_t frame_id);

    size_t Size();

   private:
    struct Node {
        frame_id_t prev = INVALID_FRAME_ID;
        frame_id_t next = INVALID_FRAME_ID;
        bool linked = false;
    };

    void remove(frame_id_t frame_id);

    std::mutex latch_;                  // 互斥锁
    std::vector<Node> nodes_;            // 预分配 intrusive 双向链表节点
    frame_id_t head_ = INVALID_FRAME_ID; // 最久未使用、下一个 victim
    frame_id_t tail_ = INVALID_FRAME_ID; // 最近变为可淘汰的 frame
    size_t size_ = 0;
    size_t max_size_;   // 最大容量（与缓冲池的容量相同）
};
