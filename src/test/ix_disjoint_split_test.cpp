#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "common/perf_counters.h"
#include "index/ix.h"
#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"

namespace {

constexpr int kKeyLength = 512;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::vector<char> make_key(int value) {
    std::vector<char> key(kKeyLength, 0);
    std::snprintf(key.data(), key.size(), "%010d", value);
    return key;
}

int key_value(const char *key) {
    char text[11]{};
    std::memcpy(text, key, 10);
    return std::atoi(text);
}

struct LeafSnapshot {
    page_id_t page_no;
    page_id_t parent;
    page_id_t prev;
    page_id_t next;
    std::vector<std::pair<int, Rid>> entries;
};

IxFileHdr read_header(DiskManager *disk, int fd) {
    std::vector<char> bytes(PAGE_SIZE, 0);
    disk->read_page(fd, IX_FILE_HDR_PAGE, bytes.data(), PAGE_SIZE);
    IxFileHdr header;
    header.deserialize(bytes.data());
    return header;
}

std::vector<LeafSnapshot> verify_tree(
    IxIndexHandle *index, BufferPoolManager *buffer_pool,
    DiskManager *disk, const std::map<int, Rid> &expected,
    size_t minimum_leaf_count = 4) {
    index->flush_file_header();
    IxFileHdr header = read_header(disk, index->GetFd());
    require(header.root_page_ >= IX_INIT_ROOT_PAGE,
            "tree header has an invalid root");

    std::set<page_id_t> visited;
    std::vector<std::pair<page_id_t, page_id_t>> pending{
        {header.root_page_, IX_NO_PAGE}};
    size_t cursor = 0;
    while (cursor < pending.size()) {
        const auto [page_no, expected_parent] = pending[cursor++];
        require(visited.insert(page_no).second,
                "tree contains a page cycle or duplicate child");
        ReadPageGuard guard =
            buffer_pool->fetch_page_read(PageId{index->GetFd(), page_no});
        require(guard.is_valid(), "tree page could not be fetched");
        IxNodeHandle node(&header, const_cast<Page *>(guard.get_page()));
        require(node.get_parent_page_no() == expected_parent,
                "child parent pointer is inconsistent");
        require(node.get_size() >= 0 &&
                    node.get_size() <= header.btree_order_,
                "page size is outside B+Tree capacity");
        for (int slot = 1; slot < node.get_size(); ++slot) {
            require(ix_compare(node.get_key(slot - 1), node.get_key(slot),
                               header.col_types_, header.col_lens_) < 0,
                    "page keys are not strictly ordered");
        }
        if (!node.is_leaf_page()) {
            require(node.get_size() > 0,
                    "non-leaf root/branch has no separator");
            for (int child = 0; child <= node.get_size(); ++child) {
                pending.push_back(
                    {node.value_at(child), node.get_page_no()});
            }
        }
    }

    std::vector<LeafSnapshot> leaves;
    std::map<int, Rid> actual;
    page_id_t page_no = header.first_leaf_;
    page_id_t previous = IX_LEAF_HEADER_PAGE;
    std::set<page_id_t> chain_pages;
    int last_key = -1;
    while (page_no != IX_LEAF_HEADER_PAGE) {
        require(page_no != IX_NO_PAGE && page_no != INVALID_PAGE_ID,
                "leaf chain ended at an invalid page");
        require(chain_pages.insert(page_no).second,
                "leaf chain contains a cycle");
        ReadPageGuard guard =
            buffer_pool->fetch_page_read(PageId{index->GetFd(), page_no});
        require(guard.is_valid(), "leaf page could not be fetched");
        IxNodeHandle node(&header, const_cast<Page *>(guard.get_page()));
        require(node.is_leaf_page(), "leaf chain references an internal page");
        require(node.get_prev_leaf() == previous,
                "leaf backward link is inconsistent");

        LeafSnapshot leaf{page_no, node.get_parent_page_no(),
                          node.get_prev_leaf(), node.get_next_leaf(), {}};
        for (int slot = 0; slot < node.get_size(); ++slot) {
            const int value = key_value(node.get_key(slot));
            require(actual.empty() || last_key < value,
                    "leaf chain keys are duplicated or unordered");
            last_key = value;
            const Rid rid = *node.get_rid(slot);
            require(actual.emplace(value, rid).second,
                    "duplicate key found in leaf chain");
            leaf.entries.push_back({value, rid});
        }
        leaves.push_back(std::move(leaf));
        previous = page_no;
        page_no = node.get_next_leaf();
    }
    require(previous == header.last_leaf_,
            "last_leaf metadata disagrees with the leaf chain");
    require(leaves.size() >= minimum_leaf_count,
            "test tree did not reach the required leaf count");
    require(actual == expected, "tree contents differ from std::map");
    for (const auto &[value, expected_rid] : expected) {
        auto key = make_key(value);
        std::vector<Rid> result;
        require(index->get_value(key.data(), &result, nullptr) &&
                    result.size() == 1 && result.front() == expected_rid,
                "root traversal cannot reach an expected key/RID");
    }
    return leaves;
}

struct TestIndex {
    std::string table_name;
    ColMeta column;
    DiskManager disk;
    BufferPoolManager buffer_pool;
    IxManager manager;
    std::string index_name;
    std::unique_ptr<IxIndexHandle> index;

    explicit TestIndex(std::string name)
        : table_name(std::move(name)),
          column{table_name, "generic_key", TYPE_STRING, kKeyLength, 0,
                 true},
          buffer_pool(256, &disk),
          manager(&disk, &buffer_pool),
          index_name(manager.get_index_name(
              table_name, std::vector<ColMeta>{column})) {
        if (disk.is_file(index_name)) {
            disk.destroy_file(index_name);
        }
        manager.create_index(table_name, std::vector<ColMeta>{column});
        index = manager.open_index(table_name, std::vector<ColMeta>{column});
    }

    ~TestIndex() {
        if (index != nullptr) {
            manager.close_index(index.get());
            index.reset();
        }
        if (disk.is_file(index_name)) {
            disk.destroy_file(index_name);
        }
    }
};

void insert(TestIndex *fixture, std::map<int, Rid> *expected, int value) {
    const Rid rid{value + 17, value + 29};
    auto key = make_key(value);
    fixture->index->insert_entry(key.data(), rid, nullptr);
    require(expected->emplace(value, rid).second,
            "test attempted to insert a duplicate fixture key");
}

std::pair<LeafSnapshot, LeafSnapshot> choose_disjoint_leaves(
    const std::vector<LeafSnapshot> &leaves) {
    for (size_t left = 1; left + 2 < leaves.size(); ++left) {
        for (size_t right = left + 2; right + 1 < leaves.size(); ++right) {
            if (leaves[left].parent != leaves[right].parent &&
                !leaves[left].entries.empty() &&
                !leaves[right].entries.empty()) {
                return {leaves[left], leaves[right]};
            }
        }
    }
    require(false, "could not find leaves under disjoint parent pages");
    std::abort();
}

void fill_leaf_to_capacity(TestIndex *fixture, std::map<int, Rid> *expected,
                           const LeafSnapshot &target, int capacity) {
    require(!target.entries.empty(), "cannot fill an empty target leaf");
    int candidate = target.entries.front().first + 1;
    const int upper = target.entries.back().first + 9999;
    int needed = capacity - static_cast<int>(target.entries.size());
    while (needed > 0) {
        if (candidate < upper && expected->find(candidate) == expected->end()) {
            insert(fixture, expected, candidate);
            --needed;
        }
        ++candidate;
    }
}

void require_disjoint_splits_overlap_and_preserve_tree() {
    TestIndex fixture("ix_disjoint_split");
    std::map<int, Rid> expected;
    for (int value = 0; value < 240; ++value) {
        insert(&fixture, &expected, value * 10000);
    }

    auto leaves = verify_tree(fixture.index.get(), &fixture.buffer_pool,
                              &fixture.disk, expected);
    const auto [first, second] = choose_disjoint_leaves(leaves);
    IxFileHdr header = read_header(&fixture.disk, fixture.index->GetFd());
    fill_leaf_to_capacity(&fixture, &expected, first, header.btree_order_);
    fill_leaf_to_capacity(&fixture, &expected, second, header.btree_order_);

    leaves = verify_tree(fixture.index.get(), &fixture.buffer_pool,
                         &fixture.disk, expected);
    const auto find_page = [&](page_id_t page_no) -> const LeafSnapshot & {
        auto found = std::find_if(
            leaves.begin(), leaves.end(),
            [&](const LeafSnapshot &leaf) { return leaf.page_no == page_no; });
        require(found != leaves.end(), "filled target leaf moved unexpectedly");
        return *found;
    };
    require(static_cast<int>(find_page(first.page_no).entries.size()) ==
                header.btree_order_ &&
                static_cast<int>(find_page(second.page_no).entries.size()) ==
                    header.btree_order_,
            "target leaves were not full before concurrent inserts");

    constexpr int kConcurrentRowsPerRange = 64;
    std::vector<std::pair<int, Rid>> first_range;
    std::vector<std::pair<int, Rid>> second_range;
    const auto prepare_range = [&](const LeafSnapshot &leaf,
                                   std::vector<std::pair<int, Rid>> *rows) {
        const int base = leaf.entries.front().first;
        rows->reserve(kConcurrentRowsPerRange);
        for (int row = 0; row < kConcurrentRowsPerRange; ++row) {
            const int value = base + 1000 + row;
            const Rid rid{value + 17, value + 29};
            require(expected.emplace(value, rid).second,
                    "concurrent fixture key is not unique");
            rows->push_back({value, rid});
        }
    };
    prepare_range(first, &first_range);
    prepare_range(second, &second_range);

    auto &counters = rmdb_perf::shared_counters();
    counters.ix_structural_active.store(0, std::memory_order_relaxed);
    counters.ix_structural_max_active.store(0, std::memory_order_relaxed);
    counters.ix_structural_test_gate_arrived.store(
        0, std::memory_order_relaxed);
    counters.ix_structural_test_gate_open.store(false,
                                                std::memory_order_relaxed);
    counters.ix_structural_test_gate_target.store(2,
                                                  std::memory_order_release);
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    auto worker = [&](const std::vector<std::pair<int, Rid>> &rows) {
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (const auto &[value, rid] : rows) {
            auto key = make_key(value);
            fixture.index->insert_entry(key.data(), rid, nullptr);
        }
    };
    std::thread left(worker, std::cref(first_range));
    std::thread right(worker, std::cref(second_range));
    while (ready.load(std::memory_order_acquire) != 2) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    left.join();
    right.join();
    counters.ix_structural_test_gate_target.store(
        0, std::memory_order_release);

    verify_tree(fixture.index.get(), &fixture.buffer_pool, &fixture.disk,
                expected);
    const uint64_t structural_max =
        counters.ix_structural_max_active.load(std::memory_order_relaxed);
    require(structural_max >= 2,
            "disjoint structural inserts were serialized");
    std::cout << "ix_structural_max_active=" << structural_max << '\n';
}

void require_safe_insert_uses_only_one_leaf_write_guard() {
    TestIndex fixture("ix_leaf_fast_path");
    std::map<int, Rid> expected;
    for (int value = 0; value < 80; ++value) {
        insert(&fixture, &expected, value * 10000);
    }

    const auto leaves = verify_tree(fixture.index.get(), &fixture.buffer_pool,
                                    &fixture.disk, expected);
    IxFileHdr header = read_header(&fixture.disk, fixture.index->GetFd());
    const LeafSnapshot *target = nullptr;
    int value = -1;
    for (size_t index = leaves.size(); index-- > 0;) {
        const auto &leaf = leaves[index];
        if (leaf.entries.empty() ||
            static_cast<int>(leaf.entries.size()) >= header.btree_order_) {
            continue;
        }
        const int candidate = leaf.entries.back().first + 1;
        if (index + 1 == leaves.size() ||
            candidate < leaves[index + 1].entries.front().first) {
            target = &leaf;
            value = candidate;
            break;
        }
    }
    require(target != nullptr, "could not find a safe target leaf");

    auto &counters = rmdb_perf::shared_counters();
    const uint64_t leaf_before =
        counters.ix_leaf_write_guards.load(std::memory_order_relaxed);
    const uint64_t ancestor_before =
        counters.ix_ancestor_write_guards.load(std::memory_order_relaxed);
    const uint64_t fast_before =
        counters.ix_leaf_fast_insert.load(std::memory_order_relaxed);
    insert(&fixture, &expected, value);
    const uint64_t leaf_delta =
        counters.ix_leaf_write_guards.load(std::memory_order_relaxed) -
        leaf_before;
    const uint64_t ancestor_delta =
        counters.ix_ancestor_write_guards.load(std::memory_order_relaxed) -
        ancestor_before;
    const uint64_t fast_delta =
        counters.ix_leaf_fast_insert.load(std::memory_order_relaxed) -
        fast_before;
    require(leaf_delta == 1,
            "safe insert acquired more than one leaf write guard");
    require(ancestor_delta == 0,
            "safe insert acquired an ancestor write guard");
    require(fast_delta == 1,
            "safe insert did not complete on the optimistic leaf path");
    std::cout << "safe_leaf_write_guards=" << leaf_delta
              << " safe_ancestor_write_guards=" << ancestor_delta
              << " ix_leaf_fast_insert_delta=" << fast_delta << '\n';
    verify_tree(fixture.index.get(), &fixture.buffer_pool, &fixture.disk,
                expected);
}

void require_batch_restarts_at_first_non_fitting_key() {
    TestIndex fixture("ix_batch_structural_suffix");
    std::map<int, Rid> expected;
    IxFileHdr header = read_header(&fixture.disk, fixture.index->GetFd());
    require(header.btree_order_ == 6,
            "batch overflow fixture assumes the 512-byte generic key order");
    for (int value : {0, 10000, 20000, 30000}) {
        insert(&fixture, &expected, value);
    }

    std::vector<std::pair<std::vector<char>, Rid>> batch;
    for (int value : {1000, 1001, 1002, 1003}) {
        const Rid rid{value + 17, value + 29};
        require(expected.emplace(value, rid).second,
                "batch overflow fixture key is not unique");
        batch.emplace_back(make_key(value), rid);
    }

    auto &counters = rmdb_perf::shared_counters();
    const uint64_t groups_before =
        counters.ix_batch_leaf_groups.load(std::memory_order_relaxed);
    const uint64_t rows_before =
        counters.ix_batch_leaf_rows.load(std::memory_order_relaxed);
    const uint64_t restarts_before =
        counters.ix_structural_restart.load(std::memory_order_relaxed);
    const uint64_t leaf_guards_before =
        counters.ix_leaf_write_guards.load(std::memory_order_relaxed);

    fixture.index->insert_entries_batch(std::move(batch), nullptr);

    const uint64_t group_delta =
        counters.ix_batch_leaf_groups.load(std::memory_order_relaxed) -
        groups_before;
    const uint64_t row_delta =
        counters.ix_batch_leaf_rows.load(std::memory_order_relaxed) -
        rows_before;
    const uint64_t restart_delta =
        counters.ix_structural_restart.load(std::memory_order_relaxed) -
        restarts_before;
    const uint64_t leaf_guard_delta =
        counters.ix_leaf_write_guards.load(std::memory_order_relaxed) -
        leaf_guards_before;
    require(group_delta == 2,
            "batch did not group the safe prefix and relocated suffix");
    require(row_delta == 3,
            "batch fast groups inserted the wrong number of rows");
    require(restart_delta == 1,
            "batch structurally restarted more than the first overflow");
    require(leaf_guard_delta == 3,
            "batch used unexpected leaf write-guard path shape");
    verify_tree(fixture.index.get(), &fixture.buffer_pool, &fixture.disk,
                expected, 1);
    std::cout << "batch_leaf_groups=" << group_delta
              << " batch_fast_rows=" << row_delta
              << " batch_structural_restarts=" << restart_delta << '\n';
}

void require_mutation_batch_falls_back_on_non_monotonic_leaf() {
    TestIndex fixture("ix_batch_non_monotonic_leaf");
    std::map<int, Rid> expected;
    for (int value = 0; value < 80; ++value) {
        insert(&fixture, &expected, value * 10000);
    }

    auto leaves = verify_tree(fixture.index.get(), &fixture.buffer_pool,
                              &fixture.disk, expected);
    IxFileHdr header = read_header(&fixture.disk, fixture.index->GetFd());
    const LeafSnapshot *target = nullptr;
    for (size_t index = 1; index + 1 < leaves.size(); ++index) {
        if (!leaves[index].entries.empty()) {
            target = &leaves[index];
            break;
        }
    }
    require(target != nullptr, "could not find an interior leaf to split");
    const page_id_t split_page = target->page_no;
    fill_leaf_to_capacity(&fixture, &expected, *target, header.btree_order_);

    leaves = verify_tree(fixture.index.get(), &fixture.buffer_pool,
                         &fixture.disk, expected);
    auto full = std::find_if(
        leaves.begin(), leaves.end(), [&](const LeafSnapshot &leaf) {
            return leaf.page_no == split_page;
        });
    require(full != leaves.end() && !full->entries.empty(),
            "filled split leaf disappeared");
    int split_key = full->entries.front().first + 1;
    while (expected.count(split_key) != 0) {
        ++split_key;
    }
    insert(&fixture, &expected, split_key);

    leaves = verify_tree(fixture.index.get(), &fixture.buffer_pool,
                         &fixture.disk, expected);
    const LeafSnapshot *non_monotonic = nullptr;
    for (const LeafSnapshot &leaf : leaves) {
        if (leaf.next != IX_LEAF_HEADER_PAGE && leaf.page_no > leaf.next &&
            !leaf.entries.empty()) {
            non_monotonic = &leaf;
            break;
        }
    }
    require(non_monotonic != nullptr,
            "fixture did not create a non-monotonic leaf link");
    auto successor = std::find_if(
        leaves.begin(), leaves.end(), [&](const LeafSnapshot &leaf) {
            return leaf.page_no == non_monotonic->next;
        });
    require(successor != leaves.end() && !successor->entries.empty(),
            "non-monotonic successor is unavailable");

    std::vector<int> values;
    for (int candidate = non_monotonic->entries.front().first + 1;
         candidate < successor->entries.front().first && values.size() < 2;
         ++candidate) {
        if (expected.count(candidate) == 0) {
            values.push_back(candidate);
        }
    }
    require(values.size() == 2,
            "non-monotonic leaf has insufficient room for a batch");

    std::vector<IndexMutation> mutations;
    for (int value : values) {
        const Rid rid{value + 17, value + 29};
        require(expected.emplace(value, rid).second,
                "non-monotonic batch key is not unique");
        mutations.push_back(
            {IndexMutationKind::INSERT, make_key(value), rid});
    }

    auto &counters = rmdb_perf::shared_counters();
    const uint64_t groups_before =
        counters.ix_batch_leaf_groups.load(std::memory_order_relaxed);
    const uint64_t rows_before =
        counters.ix_batch_leaf_rows.load(std::memory_order_relaxed);
    const uint64_t leaf_guards_before =
        counters.ix_leaf_write_guards.load(std::memory_order_relaxed);
    fixture.index->apply_sorted_batch(std::move(mutations), nullptr);

    require(counters.ix_batch_leaf_groups.load(std::memory_order_relaxed) -
                groups_before == 0,
            "non-monotonic leaf batch held two leaves instead of falling back");
    require(counters.ix_batch_leaf_rows.load(std::memory_order_relaxed) -
                rows_before == 0,
            "non-monotonic leaf batch unexpectedly used the grouped path");
    require(counters.ix_leaf_write_guards.load(std::memory_order_relaxed) -
                leaf_guards_before == 2,
            "non-monotonic leaf fallback did not make per-row progress");
    verify_tree(fixture.index.get(), &fixture.buffer_pool, &fixture.disk,
                expected);
}

}  // namespace

int main(int argc, char **argv) {
    setenv("RMDB_PERF_DIAG", "1", 1);
    if (argc == 2 && std::string(argv[1]) == "--fast-only") {
        require_safe_insert_uses_only_one_leaf_write_guard();
    } else {
        require_disjoint_splits_overlap_and_preserve_tree();
        require_safe_insert_uses_only_one_leaf_write_guard();
        require_batch_restarts_at_first_non_fitting_key();
        require_mutation_batch_falls_back_on_non_monotonic_leaf();
    }
    std::cout << "disjoint B+Tree split tests passed\n";
    return 0;
}
