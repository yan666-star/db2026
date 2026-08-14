/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "lru_replacer.h"

LRUReplacer::LRUReplacer(size_t num_pages)
    : nodes_(num_pages), max_size_(num_pages) {}

LRUReplacer::~LRUReplacer() = default;  

/**
 * @description: 使用LRU策略删除一个victim frame，并返回该frame的id
 * @param {frame_id_t*} frame_id 被移除的frame的id，如果没有frame被移除返回nullptr
 * @return {bool} 如果成功淘汰了一个页面则返回true，否则返回false
 */
bool LRUReplacer::victim(frame_id_t* frame_id) {
    std::scoped_lock lock{latch_};

    if (head_ == INVALID_FRAME_ID) {
        return false;
    }
    *frame_id = head_;
    remove(*frame_id);
    return true;
}

/**
 * @description: 固定指定的frame，即该页面无法被淘汰
 * @param {frame_id_t} 需要固定的frame的id
 */
void LRUReplacer::pin(frame_id_t frame_id) {
    std::scoped_lock lock{latch_};
    if (frame_id < 0 || static_cast<size_t>(frame_id) >= nodes_.size()) {
        return;
    }
    if (nodes_[frame_id].linked) {
        remove(frame_id);
    }
}

/**
 * @description: 取消固定一个frame，代表该页面可以被淘汰
 * @param {frame_id_t} frame_id 取消固定的frame的id
 */
void LRUReplacer::unpin(frame_id_t frame_id) {
    std::scoped_lock lock{latch_};
    if (frame_id < 0 || static_cast<size_t>(frame_id) >= nodes_.size() ||
        nodes_[frame_id].linked) {
        return;
    }

    Node &node = nodes_[frame_id];
    node.prev = tail_;
    node.next = INVALID_FRAME_ID;
    node.linked = true;
    if (tail_ != INVALID_FRAME_ID) {
        nodes_[tail_].next = frame_id;
    } else {
        head_ = frame_id;
    }
    tail_ = frame_id;
    size_++;
}

/**
 * @description: 获取当前replacer中可以被淘汰的页面数量
 */
size_t LRUReplacer::Size() {
    std::scoped_lock lock{latch_};
    return size_;
}

void LRUReplacer::remove(frame_id_t frame_id) {
    Node &node = nodes_[frame_id];
    if (!node.linked) {
        return;
    }
    if (node.prev != INVALID_FRAME_ID) {
        nodes_[node.prev].next = node.next;
    } else {
        head_ = node.next;
    }
    if (node.next != INVALID_FRAME_ID) {
        nodes_[node.next].prev = node.prev;
    } else {
        tail_ = node.prev;
    }
    node.prev = INVALID_FRAME_ID;
    node.next = INVALID_FRAME_ID;
    node.linked = false;
    size_--;
}
