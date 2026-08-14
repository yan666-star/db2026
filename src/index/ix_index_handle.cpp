/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#include "ix_index_handle.h"

#include <algorithm>
#include <cstring>
#include <tuple>

#include "errors.h"

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
                                      lsn_t page_lsn) {
    IxWriteNode right = create_node_write();
    right.node.page_hdr->is_leaf = true;
    right.node.page_hdr->parent = leaf.node.get_parent_page_no();

    const int old_size = leaf.node.get_size();
    const int mid = old_size / 2;
    const int move_count = old_size - mid;
    right.node.insert_pairs(0, leaf.node.get_key(mid),
                            leaf.node.get_rid(mid), move_count);
    leaf.node.set_size(mid);

    const page_id_t old_next = leaf.node.get_next_leaf();
    right.node.set_prev_leaf(leaf.node.get_page_no());
    right.node.set_next_leaf(old_next);
    leaf.node.set_next_leaf(right.node.get_page_no());

    if (old_next != IX_LEAF_HEADER_PAGE &&
        old_next != INVALID_PAGE_ID && old_next != IX_NO_PAGE) {
        IxWriteNode next = fetch_node_write(old_next);
        next.node.set_prev_leaf(right.node.get_page_no());
        next.mark_dirty(page_lsn);
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
    return insert_entry_impl(key, value, transaction, nullptr, nullptr);
}

page_id_t IxIndexHandle::insert_entry_impl(
    const char *key, const Rid &value, Transaction *transaction,
    std::unique_lock<std::mutex> *split_lock, bool *inserted) {
    static_cast<void>(transaction);
    const lsn_t change_lsn =
        transaction == nullptr ? INVALID_LSN : transaction->get_prev_lsn();
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
        if (root_page_snapshot() != root_page) {
            continue;
        }

        while (!path.back().node.is_leaf_page()) {
            const page_id_t child_page =
                path.back().node.internal_lookup(key);
            IxWriteNode child = fetch_node_write(child_page);
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

        if (leaf.node.get_size() >= leaf.node.get_max_size() - 1 &&
            split_lock == nullptr) {
            path.clear();
            std::unique_lock<std::mutex> structural(split_latch_);
            return insert_entry_impl(key, value, transaction, &structural,
                                     inserted);
        }

        const page_id_t inserted_page = leaf.node.get_page_no();
        leaf.node.insert(key, value);
        if (inserted != nullptr) {
            *inserted = true;
        }
        leaf.mark_dirty(change_lsn);
        if (leaf.node.get_size() < leaf.node.get_max_size()) {
            return inserted_page;
        }

        if (split_lock == nullptr || !split_lock->owns_lock()) {
            throw InternalError("B+Tree split requires the split latch");
        }

        IxWriteNode right = split_leaf(leaf, change_lsn);
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
        const size_t inserted =
            try_insert_leaf_batch(entries, begin, transaction);
        if (inserted == 0) {
            // A full target leaf needs the normal crabbing/split path. After
            // that one structural insertion, retry the remaining sorted keys;
            // they will normally fit in one of the two resulting leaves.
            bool inserted_one = false;
            insert_entry_impl(entries[begin].first.data(),
                              entries[begin].second, transaction, nullptr,
                              &inserted_one);
            if (!inserted_one) {
                throw InternalError("Duplicate key in B+Tree insert batch");
            }
            ++begin;
        } else {
            begin += inserted;
        }
    }
}

size_t IxIndexHandle::try_insert_leaf_batch(
    const std::vector<std::pair<std::vector<char>, Rid>> &entries,
    size_t begin, Transaction *transaction) {
    if (begin >= entries.size()) {
        return 0;
    }

    auto located = find_leaf_read(entries[begin].first.data());
    if (!located.has_value()) {
        return 0;
    }
    const page_id_t page_no = located->node.get_page_no();
    located.reset();

    IxWriteNode leaf = fetch_node_write(page_no);
    if (!leaf.node.is_leaf_page()) {
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

    size_t end = begin;
    size_t new_keys = 0;
    while (end < entries.size()) {
        const auto &entry = entries[end];
        if (!upper_boundary.empty() &&
            ix_compare(entry.first.data(), upper_boundary.data(),
                       file_hdr_->col_types_, file_hdr_->col_lens_) >= 0) {
            break;
        }
        const int pos = leaf.node.lower_bound(entry.first.data());
        const bool exists =
            pos < leaf.node.get_size() &&
            ix_compare(leaf.node.get_key(pos), entry.first.data(),
                       file_hdr_->col_types_, file_hdr_->col_lens_) == 0;
        if (!exists) {
            ++new_keys;
        }
        ++end;
    }
    if (end == begin) {
        return 0;
    }

    // Keep one overflow slot unused. The ordinary path owns all structural
    // changes and will split a full leaf before retrying the remaining batch.
    if (leaf.node.get_size() + static_cast<int>(new_keys) >=
        leaf.node.get_max_size()) {
        return 0;
    }

    bool changed = false;
    for (size_t index = begin; index < end; ++index) {
        const auto &entry = entries[index];
        const int pos = leaf.node.lower_bound(entry.first.data());
        if (pos < leaf.node.get_size() &&
            ix_compare(leaf.node.get_key(pos), entry.first.data(),
                       file_hdr_->col_types_, file_hdr_->col_lens_) == 0) {
            throw InternalError("Duplicate key in B+Tree insert batch");
        }
        leaf.node.insert(entry.first.data(), entry.second);
        changed = true;
    }
    if (changed) {
        const lsn_t change_lsn =
            transaction == nullptr ? INVALID_LSN
                                   : transaction->get_prev_lsn();
        leaf.mark_dirty(change_lsn);
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
    // Lazy deletion never merges pages. The split latch only prevents a leaf
    // from moving the target key between the read traversal and write latch.
    std::unique_lock<std::mutex> structural(split_latch_);
    auto leaf = find_leaf_read(key);
    if (!leaf.has_value()) {
        return false;
    }
    const page_id_t page_no = leaf->node.get_page_no();
    leaf.reset();

    IxWriteNode write_leaf = fetch_node_write(page_no);
    const int pos = write_leaf.node.lower_bound(key);
    if (pos >= write_leaf.node.get_size() ||
        ix_compare(write_leaf.node.get_key(pos), key,
                   file_hdr_->col_types_, file_hdr_->col_lens_) != 0) {
        return false;
    }
    write_leaf.node.erase_pair(pos);
    write_leaf.mark_dirty(change_lsn);
    return true;
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
