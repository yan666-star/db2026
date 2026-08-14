/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#include "ix_index_handle.h"

#include <algorithm>
#include <cstring>
#include <thread>
#include <tuple>

#include "errors.h"
#include "common/perf_counters.h"

namespace {

class StructuralActivityGuard {
   public:
    StructuralActivityGuard() {
        if (!rmdb_perf::enabled()) {
            return;
        }
        active_ = true;
        auto &counters = rmdb_perf::shared_counters();
        const uint64_t active =
            counters.ix_structural_active.fetch_add(
                1, std::memory_order_relaxed) +
            1;
        rmdb_perf::update_max(counters.ix_structural_max_active, active);
        const uint64_t gate_target =
            counters.ix_structural_test_gate_target.load(
                std::memory_order_acquire);
        if (gate_target != 0) {
            const uint64_t arrived =
                counters.ix_structural_test_gate_arrived.fetch_add(
                    1, std::memory_order_acq_rel) +
                1;
            if (arrived >= gate_target) {
                counters.ix_structural_test_gate_open.store(
                    true, std::memory_order_release);
            } else {
                while (!counters.ix_structural_test_gate_open.load(
                    std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            }
        }
    }

    ~StructuralActivityGuard() {
        if (active_) {
            rmdb_perf::shared_counters().ix_structural_active.fetch_sub(
                1, std::memory_order_relaxed);
        }
    }

   private:
    bool active_ = false;
};

void record_insert_write_guard(const IxNodeHandle &node) {
    if (!rmdb_perf::enabled()) {
        return;
    }
    auto &counters = rmdb_perf::shared_counters();
    if (node.is_leaf_page()) {
        counters.ix_leaf_write_guards.fetch_add(1,
                                                std::memory_order_relaxed);
    } else {
        counters.ix_ancestor_write_guards.fetch_add(
            1, std::memory_order_relaxed);
    }
}

}  // namespace

int IxNodeHandle::lower_bound(const char *target) const {
    int left = 0;
    int right = page_hdr->num_key;
    while (left < right) {
        int mid = left + ((right - left) >> 1);
        if (ix_compare(get_key(mid), target, file_hdr->col_types_,
                       file_hdr->col_lens_) < 0) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    return left;
}

int IxNodeHandle::upper_bound(const char *target) const {
    int left = 0;
    int right = page_hdr->num_key;
    while (left < right) {
        int mid = left + ((right - left) >> 1);
        if (ix_compare(get_key(mid), target, file_hdr->col_types_,
                       file_hdr->col_lens_) <= 0) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    return left;
}

bool IxNodeHandle::leaf_lookup(const char *key, Rid **value) {
    int index = lower_bound(key);
    if (index < page_hdr->num_key &&
        ix_compare(get_key(index), key, file_hdr->col_types_,
                   file_hdr->col_lens_) == 0) {
        *value = get_rid(index);
        return true;
    }
    return false;
}

page_id_t IxNodeHandle::internal_lookup(const char *key) {
    return value_at(upper_bound(key));
}

void IxNodeHandle::insert_pairs(int pos, const char *key, const Rid *rid,
                                int count) {
    assert(pos >= 0 && pos <= page_hdr->num_key && count >= 0);
    const int keys_to_move = page_hdr->num_key - pos;
    if (keys_to_move > 0) {
        char *key_start = keys + pos * file_hdr->col_tot_len_;
        std::memmove(key_start + count * file_hdr->col_tot_len_, key_start,
                     keys_to_move * file_hdr->col_tot_len_);
        if (is_leaf_page()) {
            auto *rid_start = reinterpret_cast<char *>(rids + pos);
            std::memmove(rid_start + count * sizeof(Rid), rid_start,
                         keys_to_move * sizeof(Rid));
        } else {
            auto *rid_start = reinterpret_cast<char *>(rids + pos + 1);
            std::memmove(rid_start + count * sizeof(Rid), rid_start,
                         keys_to_move * sizeof(Rid));
        }
    }
    std::memcpy(keys + pos * file_hdr->col_tot_len_, key,
                count * file_hdr->col_tot_len_);
    if (is_leaf_page()) {
        std::memcpy(rids + pos, rid, count * sizeof(Rid));
    } else {
        std::memcpy(rids + pos + 1, rid, count * sizeof(Rid));
    }
    page_hdr->num_key += count;
}

int IxNodeHandle::insert(const char *key, const Rid &value) {
    int pos = lower_bound(key);
    if (pos < page_hdr->num_key &&
        ix_compare(get_key(pos), key, file_hdr->col_types_,
                   file_hdr->col_lens_) == 0) {
        return page_hdr->num_key;
    }
    insert_pairs(pos, key, &value, 1);
    return page_hdr->num_key;
}

void IxNodeHandle::erase_pair(int pos) {
    if (pos < 0 || pos >= page_hdr->num_key) {
        throw IndexEntryNotFoundError();
    }
    const int keys_to_move = page_hdr->num_key - pos - 1;
    if (keys_to_move > 0) {
        std::memmove(get_key(pos), get_key(pos + 1),
                     keys_to_move * file_hdr->col_tot_len_);
    }
    if (is_leaf_page()) {
        if (keys_to_move > 0) {
            std::memmove(get_rid(pos), get_rid(pos + 1),
                         keys_to_move * sizeof(Rid));
        }
    } else {
        const int children_to_move = page_hdr->num_key - pos - 1;
        if (children_to_move > 0) {
            std::memmove(get_rid(pos + 1), get_rid(pos + 2),
                         children_to_move * sizeof(Rid));
        }
    }
    page_hdr->num_key--;
}

int IxNodeHandle::remove(const char *key) {
    int pos = lower_bound(key);
    if (pos < page_hdr->num_key &&
        ix_compare(get_key(pos), key, file_hdr->col_types_,
                   file_hdr->col_lens_) == 0) {
        erase_pair(pos);
    }
    return page_hdr->num_key;
}

IxIndexHandle::IxIndexHandle(DiskManager *disk_manager,
                             BufferPoolManager *buffer_pool_manager, int fd)
    : disk_manager_(disk_manager),
      buffer_pool_manager_(buffer_pool_manager),
      fd_(fd) {
    std::vector<char> buffer(PAGE_SIZE, 0);
    disk_manager_->read_page(fd_, IX_FILE_HDR_PAGE, buffer.data(), PAGE_SIZE);
    file_hdr_ = new IxFileHdr();
    file_hdr_->deserialize(buffer.data());
    disk_manager_->set_fd2pageno(fd_, file_hdr_->num_pages_);
}

void IxIndexHandle::flush_file_header() const {
    std::shared_lock<std::shared_mutex> root_lock(root_latch_);
    std::scoped_lock metadata(header_latch_, allocation_latch_);
    std::vector<char> data(file_hdr_->tot_len_);
    file_hdr_->serialize(data.data());
    disk_manager_->write_page(fd_, IX_FILE_HDR_PAGE, data.data(),
                              file_hdr_->tot_len_);
}

page_id_t IxIndexHandle::root_page_snapshot() const {
    std::shared_lock<std::shared_mutex> lock(root_latch_);
    return file_hdr_->root_page_;
}

void IxIndexHandle::replace_root(page_id_t expected_old_root,
                                 page_id_t new_root) {
    std::unique_lock<std::shared_mutex> lock(root_latch_);
    if (file_hdr_->root_page_ != expected_old_root) {
        throw InternalError("B+Tree root changed during a latched split");
    }
    file_hdr_->root_page_ = new_root;
}

IxReadNode IxIndexHandle::fetch_node_read(page_id_t page_no) const {
    ReadPageGuard guard =
        buffer_pool_manager_->fetch_page_read(PageId{fd_, page_no});
    if (!guard.is_valid()) {
        throw InternalError("Failed to fetch B+Tree page for read");
    }
    return IxReadNode(file_hdr_, std::move(guard));
}

IxWriteNode IxIndexHandle::fetch_node_write(page_id_t page_no) const {
    WritePageGuard guard =
        buffer_pool_manager_->fetch_page_write(PageId{fd_, page_no});
    if (!guard.is_valid()) {
        throw InternalError("Failed to fetch B+Tree page for write");
    }
    return IxWriteNode(file_hdr_, std::move(guard));
}

IxWriteNode IxIndexHandle::create_node_write() {
    std::lock_guard<std::mutex> allocation(allocation_latch_);
    PageId page_id{fd_, INVALID_PAGE_ID};
    WritePageGuard guard = buffer_pool_manager_->new_page_guarded(&page_id);
    if (!guard.is_valid()) {
        throw InternalError("Failed to allocate B+Tree page");
    }
    file_hdr_->num_pages_ =
        std::max(file_hdr_->num_pages_, page_id.page_no + 1);
    return IxWriteNode(file_hdr_, std::move(guard));
}

std::optional<IxReadNode> IxIndexHandle::find_leaf_read(
    const char *key) const {
    for (;;) {
        const page_id_t root_page = root_page_snapshot();
        if (root_page == IX_NO_PAGE || root_page == INVALID_PAGE_ID) {
            return std::nullopt;
        }
        IxReadNode current = fetch_node_read(root_page);
        if (root_page_snapshot() != root_page) {
            continue;
        }
        while (!current.node.is_leaf_page()) {
            const page_id_t child_page = current.node.internal_lookup(key);
            IxReadNode child = fetch_node_read(child_page);
            current = std::move(child);
        }
        return std::optional<IxReadNode>(std::move(current));
    }
}

bool IxIndexHandle::get_value(const char *key, std::vector<Rid> *result,
                              Transaction *transaction) {
    static_cast<void>(transaction);
    auto leaf = find_leaf_read(key);
    if (!leaf.has_value()) {
        return false;
    }
    Rid *rid = nullptr;
    const bool found = leaf->node.leaf_lookup(key, &rid);
    if (found) {
        result->push_back(*rid);
    }
    return found;
}

void IxIndexHandle::set_children_parent(IxWriteNode &node,
                                        lsn_t page_lsn) {
    if (node.node.is_leaf_page()) {
        return;
    }
    for (int index = 0; index <= node.node.get_size(); ++index) {
        IxWriteNode child = fetch_node_write(node.node.value_at(index));
        child.node.set_parent_page_no(node.node.get_page_no());
        child.mark_dirty(page_lsn);
    }
}

IxWriteNode IxIndexHandle::split_leaf(IxWriteNode &leaf,
                                      IxWriteNode *next_sibling,
                                      lsn_t page_lsn) {
    if (rmdb_perf::enabled()) {
        rmdb_perf::shared_counters().ix_split.fetch_add(
            1, std::memory_order_relaxed);
    }
    IxWriteNode right = create_node_write();
    right.node.page_hdr->is_leaf = true;
    right.node.page_hdr->parent = leaf.node.get_parent_page_no();

    const int old_size = leaf.node.get_size();
    const int mid = old_size / 2;
    const int move_count = old_size - mid;
    right.node.insert_pairs(0, leaf.node.get_key(mid),
                            leaf.node.get_rid(mid), move_count);
    leaf.node.set_size(mid);

    const page_id_t old_next_page = leaf.node.get_next_leaf();
    right.node.set_prev_leaf(leaf.node.get_page_no());
    right.node.set_next_leaf(old_next_page);
    leaf.node.set_next_leaf(right.node.get_page_no());

    if (old_next_page != IX_LEAF_HEADER_PAGE &&
        old_next_page != INVALID_PAGE_ID && old_next_page != IX_NO_PAGE) {
        if (next_sibling == nullptr ||
            next_sibling->node.get_page_no() != old_next_page) {
            throw InternalError("B+Tree split missing its locked sibling");
        }
        next_sibling->node.set_prev_leaf(right.node.get_page_no());
        next_sibling->mark_dirty(page_lsn);
    } else {
        std::lock_guard<std::mutex> header(header_latch_);
        file_hdr_->last_leaf_ = right.node.get_page_no();
    }
    leaf.mark_dirty(page_lsn);
    right.mark_dirty(page_lsn);
    return right;
}

IxWriteNode IxIndexHandle::split_internal(
    IxWriteNode &node, std::vector<char> *promote_key,
    lsn_t page_lsn) {
    if (rmdb_perf::enabled()) {
        rmdb_perf::shared_counters().ix_split.fetch_add(
            1, std::memory_order_relaxed);
    }
    IxWriteNode right = create_node_write();
    right.node.page_hdr->is_leaf = false;
    right.node.page_hdr->parent = node.node.get_parent_page_no();
    right.node.page_hdr->prev_leaf = IX_NO_PAGE;
    right.node.page_hdr->next_leaf = IX_NO_PAGE;

    const int old_size = node.node.get_size();
    const int mid = old_size / 2;
    promote_key->resize(file_hdr_->col_tot_len_);
    std::memcpy(promote_key->data(), node.node.get_key(mid),
                file_hdr_->col_tot_len_);

    const int right_size = old_size - mid - 1;
    if (right_size > 0) {
        std::memcpy(right.node.get_key(0), node.node.get_key(mid + 1),
                    right_size * file_hdr_->col_tot_len_);
    }
    std::memcpy(right.node.get_rid(0), node.node.get_rid(mid + 1),
                (right_size + 1) * sizeof(Rid));
    right.node.set_size(right_size);
    node.node.set_size(mid);
    node.mark_dirty(page_lsn);
    right.mark_dirty(page_lsn);
    set_children_parent(right, page_lsn);
    return right;
}

void IxIndexHandle::initialize_new_root(IxWriteNode &root, page_id_t left,
                                        const char *key,
                                        page_id_t right,
                                        lsn_t page_lsn) {
    root.node.page_hdr->next_free_page_no = IX_NO_PAGE;
    root.node.page_hdr->parent = INVALID_PAGE_ID;
    root.node.page_hdr->num_key = 1;
    root.node.page_hdr->is_leaf = false;
    root.node.page_hdr->prev_leaf = IX_NO_PAGE;
    root.node.page_hdr->next_leaf = IX_NO_PAGE;
    root.node.set_key(0, key);
    root.node.set_rid(0, Rid{left, -1});
    root.node.set_rid(1, Rid{right, -1});
    root.mark_dirty(page_lsn);
}

page_id_t IxIndexHandle::insert_entry(const char *key, const Rid &value,
                                      Transaction *transaction) {
    if (auto inserted =
            try_insert_leaf_optimistic(key, value, transaction);
        inserted.has_value()) {
        return *inserted;
    }
    if (rmdb_perf::enabled()) {
        rmdb_perf::shared_counters().ix_structural_restart.fetch_add(
            1, std::memory_order_relaxed);
    }
    return insert_with_structural_path(key, value, transaction);
}

bool IxIndexHandle::leaf_still_owns_key(const IxNodeHandle &leaf,
                                        const char *key) const {
    if (!leaf.is_leaf_page()) {
        return false;
    }
    if (leaf.get_size() > 0 &&
        leaf.get_prev_leaf() != IX_LEAF_HEADER_PAGE &&
        ix_compare(key, leaf.get_key(0), file_hdr_->col_types_,
                   file_hdr_->col_lens_) < 0) {
        return false;
    }

    page_id_t next_page = leaf.get_next_leaf();
    while (next_page != IX_LEAF_HEADER_PAGE &&
           next_page != INVALID_PAGE_ID && next_page != IX_NO_PAGE) {
        // The caller owns the target leaf exclusively. Never wait for a
        // sibling with a smaller page number while retaining that guard.
        if (next_page <= leaf.get_page_no()) {
            return false;
        }
        IxReadNode next = fetch_node_read(next_page);
        if (!next.node.is_leaf_page()) {
            return false;
        }
        if (next.node.get_size() > 0) {
            return ix_compare(key, next.node.get_key(0),
                              file_hdr_->col_types_,
                              file_hdr_->col_lens_) < 0;
        }
        next_page = next.node.get_next_leaf();
    }
    return true;
}

std::optional<page_id_t> IxIndexHandle::try_insert_leaf_optimistic(
    const char *key, const Rid &value, Transaction *transaction) {
    auto located = find_leaf_read(key);
    if (!located.has_value()) {
        return std::nullopt;
    }
    const page_id_t page_no = located->node.get_page_no();
    const uint64_t generation = located->guard.generation();
    located.reset();

    IxWriteNode leaf = fetch_node_write(page_no);
    record_insert_write_guard(leaf.node);
    if (leaf.guard.generation() != generation ||
        !leaf_still_owns_key(leaf.node, key)) {
        return std::nullopt;
    }

    const int pos = leaf.node.lower_bound(key);
    if (pos < leaf.node.get_size() &&
        ix_compare(leaf.node.get_key(pos), key, file_hdr_->col_types_,
                   file_hdr_->col_lens_) == 0) {
        return page_no;
    }
    if (!leaf.node.is_safe(Operation::INSERT)) {
        return std::nullopt;
    }

    leaf.node.insert(key, value);
    const lsn_t change_lsn =
        transaction == nullptr ? INVALID_LSN : transaction->get_prev_lsn();
    leaf.mark_dirty(change_lsn);
    if (rmdb_perf::enabled()) {
        rmdb_perf::shared_counters().ix_leaf_fast_insert.fetch_add(
            1, std::memory_order_relaxed);
    }
    return page_no;
}

page_id_t IxIndexHandle::insert_with_structural_path(
    const char *key, const Rid &value, Transaction *transaction,
    bool *inserted) {
    const lsn_t change_lsn =
        transaction == nullptr ? INVALID_LSN : transaction->get_prev_lsn();
    StructuralActivityGuard activity;
    for (;;) {
        page_id_t root_page = root_page_snapshot();
        if (root_page == IX_NO_PAGE || root_page == INVALID_PAGE_ID) {
            std::unique_lock<std::shared_mutex> root_guard(root_latch_);
            if (file_hdr_->root_page_ == IX_NO_PAGE ||
                file_hdr_->root_page_ == INVALID_PAGE_ID) {
                IxWriteNode root = create_node_write();
                root.node.page_hdr->next_free_page_no = IX_NO_PAGE;
                root.node.page_hdr->parent = IX_NO_PAGE;
                root.node.page_hdr->num_key = 0;
                root.node.page_hdr->is_leaf = true;
                root.node.page_hdr->prev_leaf = IX_LEAF_HEADER_PAGE;
                root.node.page_hdr->next_leaf = IX_LEAF_HEADER_PAGE;
                root.mark_dirty(change_lsn);
                file_hdr_->root_page_ = root.node.get_page_no();
                std::lock_guard<std::mutex> header(header_latch_);
                file_hdr_->first_leaf_ = root.node.get_page_no();
                file_hdr_->last_leaf_ = root.node.get_page_no();
            }
            continue;
        }

        std::vector<IxWriteNode> path;
        path.reserve(8);
        path.emplace_back(fetch_node_write(root_page));
        record_insert_write_guard(path.back().node);
        if (root_page_snapshot() != root_page) {
            continue;
        }

        while (!path.back().node.is_leaf_page()) {
            const page_id_t child_page =
                path.back().node.internal_lookup(key);
            IxWriteNode child = fetch_node_write(child_page);
            record_insert_write_guard(child.node);
            if (child.node.is_safe(Operation::INSERT)) {
                path.clear();
            }
            path.emplace_back(std::move(child));
        }

        IxWriteNode &leaf = path.back();
        const int pos = leaf.node.lower_bound(key);
        if (pos < leaf.node.get_size() &&
            ix_compare(leaf.node.get_key(pos), key, file_hdr_->col_types_,
                       file_hdr_->col_lens_) == 0) {
            if (inserted != nullptr) {
                *inserted = false;
            }
            return leaf.node.get_page_no();
        }

        const page_id_t inserted_page = leaf.node.get_page_no();
        if (leaf.node.is_safe(Operation::INSERT)) {
            leaf.node.insert(key, value);
            if (inserted != nullptr) {
                *inserted = true;
            }
            leaf.mark_dirty(change_lsn);
            return inserted_page;
        }

        // Lock the existing sibling before publishing any leaf-chain change.
        // Page numbers along the logical leaf chain are not monotonic, so if
        // the sibling sorts before the target, release/reacquire the target
        // while retaining its already-latched unsafe ancestors.
        const page_id_t leaf_page = leaf.node.get_page_no();
        const page_id_t old_next_page = leaf.node.get_next_leaf();
        const bool has_next =
            old_next_page != IX_LEAF_HEADER_PAGE &&
            old_next_page != INVALID_PAGE_ID && old_next_page != IX_NO_PAGE;
        std::optional<IxWriteNode> next_sibling;
        if (has_next && old_next_page < leaf_page) {
            path.pop_back();
            next_sibling.emplace(fetch_node_write(old_next_page));
            path.emplace_back(fetch_node_write(leaf_page));
            record_insert_write_guard(path.back().node);
        } else if (has_next) {
            next_sibling.emplace(fetch_node_write(old_next_page));
        }

        IxWriteNode &validated_leaf = path.back();
        bool valid = validated_leaf.node.is_leaf_page() &&
                     validated_leaf.node.get_page_no() == leaf_page &&
                     validated_leaf.node.get_next_leaf() == old_next_page;
        if (valid && path.size() > 1) {
            IxWriteNode &parent = path[path.size() - 2];
            valid = !parent.node.is_leaf_page() &&
                    parent.node.internal_lookup(key) == leaf_page;
        }
        if (valid && next_sibling.has_value()) {
            valid = next_sibling->node.is_leaf_page() &&
                    next_sibling->node.get_prev_leaf() == leaf_page &&
                    (next_sibling->node.get_size() == 0 ||
                     ix_compare(key, next_sibling->node.get_key(0),
                                file_hdr_->col_types_,
                                file_hdr_->col_lens_) < 0);
        }
        if (!valid) {
            continue;
        }

        const int validated_pos = validated_leaf.node.lower_bound(key);
        if (validated_pos < validated_leaf.node.get_size() &&
            ix_compare(validated_leaf.node.get_key(validated_pos), key,
                       file_hdr_->col_types_, file_hdr_->col_lens_) == 0) {
            if (inserted != nullptr) {
                *inserted = false;
            }
            return leaf_page;
        }
        if (validated_leaf.node.is_safe(Operation::INSERT)) {
            validated_leaf.node.insert(key, value);
            validated_leaf.mark_dirty(change_lsn);
            if (inserted != nullptr) {
                *inserted = true;
            }
            return leaf_page;
        }
        if (validated_leaf.node.get_size() !=
            validated_leaf.node.get_max_size() - 1) {
            // A concurrent lazy delete can make the pre-split state safe.
            // Restart so insertion happens through the ordinary safe path.
            continue;
        }

        validated_leaf.node.insert(key, value);
        validated_leaf.mark_dirty(change_lsn);
        if (inserted != nullptr) {
            *inserted = true;
        }

        IxWriteNode right = split_leaf(
            validated_leaf,
            next_sibling.has_value() ? &*next_sibling : nullptr,
            change_lsn);
        next_sibling.reset();
        std::vector<char> separator(file_hdr_->col_tot_len_);
        std::memcpy(separator.data(), right.node.get_key(0),
                    file_hdr_->col_tot_len_);
        size_t level = path.size() - 1;

        for (;;) {
            IxWriteNode &left = path[level];
            if (level == 0) {
                const page_id_t old_root = left.node.get_page_no();
                IxWriteNode new_root = create_node_write();
                initialize_new_root(new_root, left.node.get_page_no(),
                                    separator.data(),
                                    right.node.get_page_no(), change_lsn);
                left.node.set_parent_page_no(new_root.node.get_page_no());
                right.node.set_parent_page_no(new_root.node.get_page_no());
                left.mark_dirty(change_lsn);
                right.mark_dirty(change_lsn);
                replace_root(old_root, new_root.node.get_page_no());
                return inserted_page;
            }

            IxWriteNode &parent = path[level - 1];
            const Rid right_rid{right.node.get_page_no(), -1};
            const int insert_pos = parent.node.upper_bound(separator.data());
            parent.node.insert_pairs(insert_pos, separator.data(),
                                     &right_rid, 1);
            right.node.set_parent_page_no(parent.node.get_page_no());
            parent.mark_dirty(change_lsn);
            right.mark_dirty(change_lsn);
            if (parent.node.get_size() < parent.node.get_max_size()) {
                return inserted_page;
            }

            // The child split is fully linked into parent. Release all child
            // guards before split_internal() rewrites the parent pointers of
            // moved children; otherwise the current thread can try to take a
            // second exclusive latch on the same child page.
            right.guard.drop();
            path.erase(path.begin() + static_cast<std::ptrdiff_t>(level),
                       path.end());
            std::vector<char> next_separator;
            IxWriteNode parent_right =
                split_internal(parent, &next_separator, change_lsn);
            right = std::move(parent_right);
            separator = std::move(next_separator);
            level--;
        }
    }
}

void IxIndexHandle::insert_entries_batch(
    std::vector<std::pair<std::vector<char>, Rid>> entries,
    Transaction *transaction) {
    for (const auto &entry : entries) {
        if (entry.first.size() !=
            static_cast<size_t>(file_hdr_->col_tot_len_)) {
            throw InvalidColLengthError(static_cast<int>(entry.first.size()));
        }
    }
    std::sort(entries.begin(), entries.end(), [&](const auto &left,
                                                   const auto &right) {
        return ix_compare(left.first.data(), right.first.data(),
                          file_hdr_->col_types_, file_hdr_->col_lens_) < 0;
    });
    for (size_t i = 1; i < entries.size(); ++i) {
        if (ix_compare(entries[i - 1].first.data(), entries[i].first.data(),
                       file_hdr_->col_types_, file_hdr_->col_lens_) == 0) {
            throw InternalError("Duplicate key in B+Tree insert batch");
        }
    }

    size_t begin = 0;
    while (begin < entries.size()) {
        bool first_overflow = false;
        const size_t inserted = try_insert_leaf_batch(
            entries, begin, transaction, &first_overflow);
        begin += inserted;
        if (begin == entries.size()) {
            break;
        }
        if (inserted == 0 || first_overflow) {
            // A full target leaf needs the normal crabbing/split path. After
            // that one structural insertion, retry the remaining sorted keys;
            // they will normally fit in one of the two resulting leaves.
            bool inserted_one = false;
            if (rmdb_perf::enabled()) {
                rmdb_perf::shared_counters().ix_structural_restart.fetch_add(
                    1, std::memory_order_relaxed);
            }
            insert_with_structural_path(entries[begin].first.data(),
                                        entries[begin].second, transaction,
                                        &inserted_one);
            if (!inserted_one) {
                throw InternalError("Duplicate key in B+Tree insert batch");
            }
            ++begin;
        }
    }
}

void IxIndexHandle::apply_sorted_batch(
    std::vector<IndexMutation> mutations, Transaction *transaction) {
    for (const IndexMutation &mutation : mutations) {
        if (mutation.key.size() !=
            static_cast<size_t>(file_hdr_->col_tot_len_)) {
            throw InvalidColLengthError(
                static_cast<int>(mutation.key.size()));
        }
    }
    std::sort(mutations.begin(), mutations.end(), [&](const auto &left,
                                                       const auto &right) {
        const int compared = ix_compare(
            left.key.data(), right.key.data(), file_hdr_->col_types_,
            file_hdr_->col_lens_);
        if (compared != 0) {
            return compared < 0;
        }
        return left.kind == IndexMutationKind::DELETE &&
               right.kind == IndexMutationKind::INSERT;
    });

    size_t begin = 0;
    while (begin < mutations.size()) {
        const size_t group_begin = begin;
        bool structural_insert = false;
        bool retry = false;
        bool single_fallback = false;
        size_t end = begin;
        auto apply_group = [&](IxWriteNode &leaf,
                               const std::vector<char> &upper_boundary) {
            bool changed = false;
            while (!retry && end < mutations.size()) {
                const IndexMutation &mutation = mutations[end];
                if (!upper_boundary.empty() &&
                    ix_compare(mutation.key.data(), upper_boundary.data(),
                               file_hdr_->col_types_,
                               file_hdr_->col_lens_) >= 0) {
                    break;
                }
                const int pos = leaf.node.lower_bound(mutation.key.data());
                const bool exists =
                    pos < leaf.node.get_size() &&
                    ix_compare(leaf.node.get_key(pos), mutation.key.data(),
                               file_hdr_->col_types_,
                               file_hdr_->col_lens_) == 0;
                if (mutation.kind == IndexMutationKind::DELETE) {
                    if (exists && *leaf.node.get_rid(pos) == mutation.rid) {
                        leaf.node.erase_pair(pos);
                        changed = true;
                    }
                    ++end;
                    continue;
                }
                if (exists) {
                    throw InternalError(
                        "Duplicate key in B+Tree mutation batch");
                }
                if (!leaf.node.is_safe(Operation::INSERT)) {
                    structural_insert = true;
                    break;
                }
                leaf.node.insert(mutation.key.data(), mutation.rid);
                changed = true;
                ++end;
            }
            if (!changed) {
                return;
            }
            const lsn_t change_lsn =
                transaction == nullptr ? INVALID_LSN
                                       : transaction->get_prev_lsn();
            leaf.mark_dirty(change_lsn);
            if (rmdb_perf::enabled()) {
                auto &counters = rmdb_perf::shared_counters();
                counters.ix_batch_leaf_groups.fetch_add(
                    1, std::memory_order_relaxed);
                counters.ix_batch_leaf_rows.fetch_add(
                    end - begin, std::memory_order_relaxed);
            }
        };
        {
            auto located = find_leaf_read(mutations[begin].key.data());
            if (!located.has_value()) {
                structural_insert =
                    mutations[begin].kind == IndexMutationKind::INSERT;
            } else {
                const page_id_t page_no = located->node.get_page_no();
                const uint64_t generation = located->guard.generation();
                const page_id_t observed_next =
                    located->node.get_next_leaf();
                located.reset();

                const bool has_successor =
                    observed_next != IX_LEAF_HEADER_PAGE &&
                    observed_next != INVALID_PAGE_ID &&
                    observed_next != IX_NO_PAGE;
                std::vector<char> upper_boundary;
                if (has_successor && observed_next < page_no) {
                    // Acquire the two adjacent leaves by physical page id,
                    // while still applying mutations in logical key order.
                    // Non-rightmost splits commonly create page N -> page M
                    // with M < N; taking M first avoids both latch inversion
                    // and the former per-key root-path fallback.
                    IxReadNode next = fetch_node_read(observed_next);
                    IxWriteNode leaf = fetch_node_write(page_no);
                    record_insert_write_guard(leaf.node);
                    if (leaf.guard.generation() != generation ||
                        !leaf.node.is_leaf_page() ||
                        !next.node.is_leaf_page() ||
                        leaf.node.get_next_leaf() != observed_next ||
                        next.node.get_prev_leaf() != page_no ||
                        next.node.get_size() == 0 ||
                        (leaf.node.get_size() > 0 &&
                         leaf.node.get_prev_leaf() != IX_LEAF_HEADER_PAGE &&
                         ix_compare(mutations[begin].key.data(),
                                    leaf.node.get_key(0),
                                    file_hdr_->col_types_,
                                    file_hdr_->col_lens_) < 0)) {
                        retry = true;
                    } else {
                        upper_boundary.resize(file_hdr_->col_tot_len_);
                        std::memcpy(upper_boundary.data(),
                                    next.node.get_key(0),
                                    file_hdr_->col_tot_len_);
                        apply_group(leaf, upper_boundary);
                    }
                } else {
                    IxWriteNode leaf = fetch_node_write(page_no);
                    record_insert_write_guard(leaf.node);
                    if (leaf.guard.generation() != generation ||
                        !leaf.node.is_leaf_page() ||
                        leaf.node.get_next_leaf() != observed_next ||
                        (leaf.node.get_size() > 0 &&
                         leaf.node.get_prev_leaf() != IX_LEAF_HEADER_PAGE &&
                         ix_compare(mutations[begin].key.data(),
                                    leaf.node.get_key(0),
                                    file_hdr_->col_types_,
                                    file_hdr_->col_lens_) < 0)) {
                        retry = true;
                    } else if (has_successor) {
                        IxReadNode next = fetch_node_read(observed_next);
                        if (!next.node.is_leaf_page() ||
                            next.node.get_prev_leaf() != page_no) {
                            retry = true;
                        } else if (next.node.get_size() == 0) {
                            single_fallback = true;
                        } else {
                            upper_boundary.resize(file_hdr_->col_tot_len_);
                            std::memcpy(upper_boundary.data(),
                                        next.node.get_key(0),
                                        file_hdr_->col_tot_len_);
                            apply_group(leaf, upper_boundary);
                        }
                    } else {
                        apply_group(leaf, upper_boundary);
                    }
                }
            }
        }
        if (retry) {
            continue;
        }
        if (single_fallback) {
            const IndexMutation &mutation = mutations[begin];
            if (mutation.kind == IndexMutationKind::INSERT) {
                bool inserted = false;
                insert_with_structural_path(mutation.key.data(),
                                            mutation.rid, transaction,
                                            &inserted);
                if (!inserted) {
                    throw InternalError(
                        "Duplicate key in B+Tree mutation batch");
                }
            } else {
                delete_entry(mutation.key.data(), transaction);
            }
            ++begin;
            continue;
        }
        begin = end;
        if (structural_insert) {
            bool inserted = false;
            insert_with_structural_path(mutations[begin].key.data(),
                                        mutations[begin].rid, transaction,
                                        &inserted);
            if (!inserted) {
                throw InternalError(
                    "Duplicate key in B+Tree mutation batch");
            }
            ++begin;
        } else if (end == group_begin && begin < mutations.size()) {
            // An empty tree can only make progress through structural insert;
            // deleting from it is a successful no-op.
            ++begin;
        }
    }
}

size_t IxIndexHandle::try_insert_leaf_batch(
    const std::vector<std::pair<std::vector<char>, Rid>> &entries,
    size_t begin, Transaction *transaction, bool *first_overflow) {
    *first_overflow = false;
    if (begin >= entries.size()) {
        return 0;
    }

    auto located = find_leaf_read(entries[begin].first.data());
    if (!located.has_value()) {
        return 0;
    }
    const page_id_t page_no = located->node.get_page_no();
    const uint64_t generation = located->guard.generation();
    located.reset();

    IxWriteNode leaf = fetch_node_write(page_no);
    record_insert_write_guard(leaf.node);
    if (leaf.guard.generation() != generation ||
        !leaf.node.is_leaf_page() ||
        (leaf.node.get_size() > 0 &&
         leaf.node.get_prev_leaf() != IX_LEAF_HEADER_PAGE &&
         ix_compare(entries[begin].first.data(), leaf.node.get_key(0),
                    file_hdr_->col_types_, file_hdr_->col_lens_) < 0)) {
        return 0;
    }

    // A split may have happened between the read traversal and the write
    // guard. Validate the leaf's current exclusive upper boundary by reading
    // the first key of the next non-empty leaf. Holding the left leaf while
    // reading right follows the tree's left-to-right latch order.
    std::vector<char> upper_boundary;
    page_id_t next_page = leaf.node.get_next_leaf();
    while (next_page != IX_LEAF_HEADER_PAGE &&
           next_page != INVALID_PAGE_ID && next_page != IX_NO_PAGE) {
        if (next_page <= leaf.node.get_page_no()) {
            return 0;
        }
        IxReadNode next = fetch_node_read(next_page);
        if (next.node.get_size() > 0) {
            upper_boundary.resize(file_hdr_->col_tot_len_);
            std::memcpy(upper_boundary.data(), next.node.get_key(0),
                        file_hdr_->col_tot_len_);
            break;
        }
        next_page = next.node.get_next_leaf();
    }
    if (!upper_boundary.empty() &&
        ix_compare(entries[begin].first.data(), upper_boundary.data(),
                   file_hdr_->col_types_, file_hdr_->col_lens_) >= 0) {
        return 0;
    }

    bool changed = false;
    size_t end = begin;
    while (end < entries.size()) {
        const auto &entry = entries[end];
        if (!upper_boundary.empty() &&
            ix_compare(entry.first.data(), upper_boundary.data(),
                       file_hdr_->col_types_, file_hdr_->col_lens_) >= 0) {
            break;
        }
        const int pos = leaf.node.lower_bound(entry.first.data());
        if (pos < leaf.node.get_size() &&
            ix_compare(leaf.node.get_key(pos), entry.first.data(),
                       file_hdr_->col_types_, file_hdr_->col_lens_) == 0) {
            throw InternalError("Duplicate key in B+Tree insert batch");
        }
        // Keep the overflow slot unused. Return the prefix that fits so the
        // caller structurally restarts only at the first overflowing key.
        if (!leaf.node.is_safe(Operation::INSERT)) {
            *first_overflow = true;
            break;
        }
        leaf.node.insert(entry.first.data(), entry.second);
        changed = true;
        ++end;
    }
    if (changed) {
        const lsn_t change_lsn =
            transaction == nullptr ? INVALID_LSN
                                   : transaction->get_prev_lsn();
        leaf.mark_dirty(change_lsn);
        if (rmdb_perf::enabled()) {
            auto &counters = rmdb_perf::shared_counters();
            counters.ix_leaf_fast_insert.fetch_add(
                end - begin, std::memory_order_relaxed);
            counters.ix_batch_leaf_groups.fetch_add(
                1, std::memory_order_relaxed);
            counters.ix_batch_leaf_rows.fetch_add(
                end - begin, std::memory_order_relaxed);
        }
    }
    return end - begin;
}

bool IxIndexHandle::contains_any_entries_batch(
    std::vector<std::vector<char>> keys) const {
    for (const auto &key : keys) {
        if (key.size() != static_cast<size_t>(file_hdr_->col_tot_len_)) {
            throw InvalidColLengthError(static_cast<int>(key.size()));
        }
    }
    std::sort(keys.begin(), keys.end(), [&](const auto &left,
                                             const auto &right) {
        return ix_compare(left.data(), right.data(), file_hdr_->col_types_,
                          file_hdr_->col_lens_) < 0;
    });

    size_t begin = 0;
    while (begin < keys.size()) {
        auto leaf = find_leaf_read(keys[begin].data());
        if (!leaf.has_value()) {
            return false;
        }

        std::vector<char> upper_boundary;
        page_id_t next_page = leaf->node.get_next_leaf();
        while (next_page != IX_LEAF_HEADER_PAGE &&
               next_page != INVALID_PAGE_ID && next_page != IX_NO_PAGE) {
            IxReadNode next = fetch_node_read(next_page);
            if (next.node.get_size() > 0) {
                upper_boundary.resize(file_hdr_->col_tot_len_);
                std::memcpy(upper_boundary.data(), next.node.get_key(0),
                            file_hdr_->col_tot_len_);
                break;
            }
            next_page = next.node.get_next_leaf();
        }

        size_t end = begin;
        while (end < keys.size() &&
               (upper_boundary.empty() ||
                ix_compare(keys[end].data(), upper_boundary.data(),
                           file_hdr_->col_types_, file_hdr_->col_lens_) < 0)) {
            Rid *rid = nullptr;
            if (leaf->node.leaf_lookup(keys[end].data(), &rid)) {
                return true;
            }
            ++end;
        }
        if (end == begin) {
            // The leaf split after traversal would be impossible while its
            // read guard is held, but retrying is safer than skipping a key if
            // a stale separator routed to an adjacent leaf.
            ++begin;
        } else {
            begin = end;
        }
    }
    return false;
}

bool IxIndexHandle::delete_entry(const char *key,
                                 Transaction *transaction) {
    const lsn_t change_lsn =
        transaction == nullptr ? INVALID_LSN : transaction->get_prev_lsn();
    // Lazy deletion performs no structural change. If a split moved the key
    // between the read traversal and leaf write acquisition, re-read the
    // current tree and retry only when the key still exists.
    for (;;) {
        auto leaf = find_leaf_read(key);
        if (!leaf.has_value()) {
            return false;
        }
        const page_id_t page_no = leaf->node.get_page_no();
        const uint64_t generation = leaf->guard.generation();
        leaf.reset();

        IxWriteNode write_leaf = fetch_node_write(page_no);
        if (write_leaf.guard.generation() != generation) {
            continue;
        }
        const int pos = write_leaf.node.lower_bound(key);
        if (pos >= write_leaf.node.get_size() ||
            ix_compare(write_leaf.node.get_key(pos), key,
                       file_hdr_->col_types_, file_hdr_->col_lens_) != 0) {
            write_leaf.guard.drop();
            std::vector<Rid> current;
            if (get_value(key, &current, transaction)) {
                continue;
            }
            return false;
        }
        write_leaf.node.erase_pair(pos);
        write_leaf.mark_dirty(change_lsn);
        return true;
    }
}

Iid IxIndexHandle::lower_bound(const char *key) {
    auto leaf = find_leaf_read(key);
    if (!leaf.has_value()) {
        return {IX_NO_PAGE, 0};
    }
    int index = leaf->node.lower_bound(key);
    if (index < leaf->node.get_size()) {
        return {leaf->node.get_page_no(), index};
    }
    page_id_t next_page = leaf->node.get_next_leaf();
    leaf.reset();
    while (next_page != IX_LEAF_HEADER_PAGE &&
           next_page != INVALID_PAGE_ID && next_page != IX_NO_PAGE) {
        IxReadNode next = fetch_node_read(next_page);
        if (next.node.get_size() > 0) {
            return {next_page, 0};
        }
        next_page = next.node.get_next_leaf();
    }
    return leaf_end();
}

Iid IxIndexHandle::upper_bound(const char *key) {
    auto leaf = find_leaf_read(key);
    if (!leaf.has_value()) {
        return {IX_NO_PAGE, 0};
    }
    int index = leaf->node.upper_bound(key);
    if (index < leaf->node.get_size()) {
        return {leaf->node.get_page_no(), index};
    }
    page_id_t next_page = leaf->node.get_next_leaf();
    leaf.reset();
    while (next_page != IX_LEAF_HEADER_PAGE &&
           next_page != INVALID_PAGE_ID && next_page != IX_NO_PAGE) {
        IxReadNode next = fetch_node_read(next_page);
        if (next.node.get_size() > 0) {
            return {next_page, 0};
        }
        next_page = next.node.get_next_leaf();
    }
    return leaf_end();
}

Iid IxIndexHandle::leaf_end() const {
    for (;;) {
        page_id_t last_leaf;
        {
            std::lock_guard<std::mutex> header(header_latch_);
            last_leaf = file_hdr_->last_leaf_;
        }
        if (last_leaf == IX_NO_PAGE || last_leaf == INVALID_PAGE_ID) {
            return {IX_NO_PAGE, 0};
        }
        IxReadNode leaf = fetch_node_read(last_leaf);
        {
            std::lock_guard<std::mutex> header(header_latch_);
            if (file_hdr_->last_leaf_ != last_leaf) {
                continue;
            }
        }
        return {last_leaf, leaf.node.get_size()};
    }
}

Iid IxIndexHandle::leaf_begin() const {
    page_id_t page_no;
    {
        std::lock_guard<std::mutex> header(header_latch_);
        page_no = file_hdr_->first_leaf_;
    }
    while (page_no != IX_LEAF_HEADER_PAGE &&
           page_no != INVALID_PAGE_ID && page_no != IX_NO_PAGE) {
        IxReadNode leaf = fetch_node_read(page_no);
        if (leaf.node.get_size() > 0) {
            return {page_no, 0};
        }
        page_no = leaf.node.get_next_leaf();
    }
    return leaf_end();
}

Rid IxIndexHandle::get_rid(const Iid &iid) const {
    IxReadNode node = fetch_node_read(iid.page_no);
    if (iid.slot_no < 0 || iid.slot_no >= node.node.get_size()) {
        throw IndexEntryNotFoundError();
    }
    return *node.node.get_rid(iid.slot_no);
}

std::vector<Rid> IxIndexHandle::get_rids(const Iid &iid) const {
    IxReadNode node = fetch_node_read(iid.page_no);
    if (iid.slot_no < 0 || iid.slot_no >= node.node.get_size()) {
        throw IndexEntryNotFoundError();
    }
    std::vector<Rid> rids;
    rids.reserve(node.node.get_size());
    for (int index = 0; index < node.node.get_size(); ++index) {
        rids.push_back(*node.node.get_rid(index));
    }
    return rids;
}
