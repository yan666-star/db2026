#include <cstdlib>
#include <iostream>
#include <vector>

#include "transaction/index_version_store.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_index_history_lifecycle() {
    IndexVersionStore store;
    const Rid rid{4, 7};
    store.retain(23, std::vector<char>{'o', 'l', 'd'}, rid, 91);
    auto provisional = store.snapshot(23);
    require(provisional.size() == 1 && provisional[0].rid == rid &&
                provisional[0].valid_until == INVALID_TS,
            "provisional old index mapping was not retained");
    require(store.conflicts_with_snapshot(
                23, std::vector<char>{'o', 'l', 'd'}, Rid{-1, -1}, 90, 0),
            "provisional old key did not block another snapshot writer");
    require(!store.conflicts_with_snapshot(
                23, std::vector<char>{'o', 'l', 'd'}, rid, 90, 0),
            "an update conflicted with its own target RID history");

    store.finalize(91, 12);
    auto committed = store.snapshot(23);
    require(committed.size() == 1 && committed[0].valid_until == 12,
            "old index mapping did not acquire commit visibility bound");
    require(store.conflicts_with_snapshot(
                23, std::vector<char>{'o', 'l', 'd'}, Rid{-1, -1}, 90, 11),
            "snapshot predating key removal missed unique-key conflict");
    require(!store.conflicts_with_snapshot(
                23, std::vector<char>{'o', 'l', 'd'}, Rid{-1, -1}, 90, 12),
            "snapshot at key-removal timestamp retained stale conflict");
    store.garbage_collect(11);
    require(store.snapshot(23).size() == 1,
            "index history was reclaimed before the snapshot watermark");
    store.garbage_collect(12);
    require(store.snapshot(23).empty(),
            "index history survived after every old snapshot expired");

    store.retain(23, std::vector<char>{'x'}, Rid{5, 8}, 92);
    store.discard(92);
    require(store.snapshot(23).empty(),
            "aborted provisional index history was not discarded");
}

}  // namespace

int main() {
    test_index_history_lifecycle();
    std::cout << "MVCC index visibility tests passed\n";
    return 0;
}
