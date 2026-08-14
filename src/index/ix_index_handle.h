/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <vector>

#include "ix_defs.h"
#include "storage/page_guard.h"
#include "transaction/transaction.h"

enum class Operation { FIND = 0, INSERT, DELETE };

inline int ix_compare(const char *a, const char *b, ColType type,
                      int col_len) {
    switch (type) {
        case TYPE_INT: {
            int ia = 0;
            int ib = 0;
            std::memcpy(&ia, a, sizeof(ia));
            std::memcpy(&ib, b, sizeof(ib));
            return (ia < ib) ? -1 : ((ia > ib) ? 1 : 0);
        }
        case TYPE_FLOAT: {
            float fa = 0;
            float fb = 0;
            std::memcpy(&fa, a, sizeof(fa));
            std::memcpy(&fb, b, sizeof(fb));
            return (fa < fb) ? -1 : ((fa > fb) ? 1 : 0);
        }
        case TYPE_STRING:
            return std::memcmp(a, b, col_len);
        default:
            throw InternalError("Unexpected data type");
    }
}

inline int ix_compare(const char *a, const char *b,
                      const std::vector<ColType> &col_types,
                      const std::vector<int> &col_lens) {
    int offset = 0;
    for (size_t i = 0; i < col_types.size(); ++i) {
        int result =
            ix_compare(a + offset, b + offset, col_types[i], col_lens[i]);
        if (result != 0) {
            return result;
        }
        offset += col_lens[i];
    }
    return 0;
}

/** A non-owning parser for an index page. Ownership lives in IxRead/WriteNode. */
class IxNodeHandle {
    friend class IxIndexHandle;
    friend class IxScan;

   private:
    const IxFileHdr *file_hdr = nullptr;
    Page *page = nullptr;
    IxPageHdr *page_hdr = nullptr;
    char *keys = nullptr;
    Rid *rids = nullptr;

   public:
    IxNodeHandle() = default;
    IxNodeHandle(const IxFileHdr *file_hdr, Page *page)
        : file_hdr(file_hdr), page(page) {
        page_hdr = reinterpret_cast<IxPageHdr *>(page->get_data());
        keys = page->get_data() + sizeof(IxPageHdr);
        rids = reinterpret_cast<Rid *>(keys + file_hdr->keys_size_);
    }

    int get_size() const { return page_hdr->num_key; }
    void set_size(int size) { page_hdr->num_key = size; }
    int get_max_size() const { return file_hdr->btree_order_ + 1; }
    int get_min_size() const { return get_max_size() / 2; }
    bool is_safe(Operation operation) const {
        if (operation == Operation::INSERT) {
            return get_size() < get_max_size() - 1;
        }
        if (operation == Operation::DELETE) {
            return get_size() > get_min_size();
        }
        return true;
    }

    int key_at(int index) const {
        int value = 0;
        std::memcpy(&value, get_key(index), sizeof(value));
        return value;
    }
    page_id_t value_at(int index) const { return get_rid(index)->page_no; }
    page_id_t get_page_no() const { return page->get_page_id().page_no; }
    PageId get_page_id() const { return page->get_page_id(); }
    page_id_t get_next_leaf() const { return page_hdr->next_leaf; }
    page_id_t get_prev_leaf() const { return page_hdr->prev_leaf; }
    page_id_t get_parent_page_no() const { return page_hdr->parent; }
    bool is_leaf_page() const { return page_hdr->is_leaf; }
    bool is_root_page() const {
        return get_parent_page_no() == INVALID_PAGE_ID ||
               get_parent_page_no() == IX_NO_PAGE;
    }
    void set_next_leaf(page_id_t page_no) { page_hdr->next_leaf = page_no; }
    void set_prev_leaf(page_id_t page_no) { page_hdr->prev_leaf = page_no; }
    void set_parent_page_no(page_id_t parent) { page_hdr->parent = parent; }
    char *get_key(int index) const {
        return keys + index * file_hdr->col_tot_len_;
    }
    Rid *get_rid(int index) const { return &rids[index]; }
    void set_key(int index, const char *key) {
        std::memcpy(get_key(index), key, file_hdr->col_tot_len_);
    }
    void set_rid(int index, const Rid &rid) { rids[index] = rid; }

    int lower_bound(const char *target) const;
    int upper_bound(const char *target) const;
    void insert_pairs(int pos, const char *key, const Rid *rid, int count);
    page_id_t internal_lookup(const char *key);
    bool leaf_lookup(const char *key, Rid **value);
    int insert(const char *key, const Rid &value);
    void insert_pair(int pos, const char *key, const Rid &rid) {
        insert_pairs(pos, key, &rid, 1);
    }
    void erase_pair(int pos);
    int remove(const char *key);
    int find_child(const IxNodeHandle *child) const {
        for (int index = 0; index <= page_hdr->num_key; ++index) {
            if (get_rid(index)->page_no == child->get_page_no()) {
                return index;
            }
        }
        throw InternalError("B+Tree parent does not reference child");
    }
};

struct IxReadNode {
    ReadPageGuard guard;
    IxNodeHandle node;

    IxReadNode(const IxFileHdr *file_hdr, ReadPageGuard page_guard)
        : guard(std::move(page_guard)),
          node(file_hdr, const_cast<Page *>(guard.get_page())) {}
    IxReadNode(const IxReadNode &) = delete;
    IxReadNode &operator=(const IxReadNode &) = delete;
    IxReadNode(IxReadNode &&other) noexcept
        : guard(std::move(other.guard)), node(other.node) {}
    IxReadNode &operator=(IxReadNode &&other) noexcept {
        if (this != &other) {
            guard = std::move(other.guard);
            node = other.node;
        }
        return *this;
    }
};

struct IxWriteNode {
    WritePageGuard guard;
    IxNodeHandle node;

    IxWriteNode(const IxFileHdr *file_hdr, WritePageGuard page_guard)
        : guard(std::move(page_guard)), node(file_hdr, guard.get_page()) {}
    IxWriteNode(const IxWriteNode &) = delete;
    IxWriteNode &operator=(const IxWriteNode &) = delete;
    IxWriteNode(IxWriteNode &&other) noexcept
        : guard(std::move(other.guard)), node(other.node) {}
    IxWriteNode &operator=(IxWriteNode &&other) noexcept {
        if (this != &other) {
            guard = std::move(other.guard);
            node = other.node;
        }
        return *this;
    }
    void mark_dirty(lsn_t page_lsn = INVALID_LSN) {
        if (page_lsn != INVALID_LSN) {
            guard.set_page_lsn(page_lsn);
        }
        guard.mark_dirty();
    }
};

class IxIndexHandle {
    friend class IxScan;
    friend class IxManager;

   private:
    DiskManager *disk_manager_;
    BufferPoolManager *buffer_pool_manager_;
    int fd_;
    IxFileHdr *file_hdr_;

    // Protects only reading/replacing root_page_. Page-content latches protect
    // traversal and mutation; unrelated leaves can therefore progress in
    // parallel.
    mutable std::shared_mutex root_latch_;
    mutable std::mutex header_latch_;
    mutable std::mutex allocation_latch_;

   public:
    IxIndexHandle(DiskManager *disk_manager,
                  BufferPoolManager *buffer_pool_manager, int fd);
    ~IxIndexHandle() { delete file_hdr_; }

    bool get_value(const char *key, std::vector<Rid> *result,
                   Transaction *transaction);
    page_id_t insert_entry(const char *key, const Rid &value,
                           Transaction *transaction);
    void insert_entries_batch(
        std::vector<std::pair<std::vector<char>, Rid>> entries,
        Transaction *transaction);
    bool contains_any_entries_batch(
        std::vector<std::vector<char>> keys) const;
    bool delete_entry(const char *key, Transaction *transaction);

    Iid lower_bound(const char *key);
    Iid upper_bound(const char *key);
    Iid leaf_end() const;
    Iid leaf_begin() const;
    void flush_file_header() const;
    int GetFd() const { return fd_; }

   private:
    page_id_t root_page_snapshot() const;
    void replace_root(page_id_t expected_old_root,
                      page_id_t new_root);
    IxReadNode fetch_node_read(page_id_t page_no) const;
    IxWriteNode fetch_node_write(page_id_t page_no) const;
    IxWriteNode create_node_write();
    std::optional<IxReadNode> find_leaf_read(const char *key) const;

    std::optional<page_id_t> try_insert_leaf_optimistic(
        const char *key, const Rid &rid, Transaction *txn);
    page_id_t insert_with_structural_path(const char *key, const Rid &rid,
                                          Transaction *txn,
                                          bool *inserted = nullptr);
    bool leaf_still_owns_key(const IxNodeHandle &leaf,
                             const char *key) const;
    size_t try_insert_leaf_batch(
        const std::vector<std::pair<std::vector<char>, Rid>> &entries,
        size_t begin, Transaction *transaction, bool *first_overflow);
    IxWriteNode split_leaf(IxWriteNode &leaf, IxWriteNode *next_sibling,
                           lsn_t page_lsn);
    IxWriteNode split_internal(IxWriteNode &node,
                               std::vector<char> *promote_key,
                               lsn_t page_lsn);
    void set_children_parent(IxWriteNode &node, lsn_t page_lsn);
    void initialize_new_root(IxWriteNode &root, page_id_t left,
                             const char *key, page_id_t right,
                             lsn_t page_lsn);

    Rid get_rid(const Iid &iid) const;
    std::vector<Rid> get_rids(const Iid &iid) const;
};
