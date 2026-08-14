#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "errors.h"
#include "transaction/transaction.h"
#include "transaction/transaction_write_batch.h"

namespace {

std::vector<char> bytes(const char *text) {
    return std::vector<char>(text, text + std::char_traits<char>::length(text));
}

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

template <typename Action>
void require_internal_error(Action action, const char *message) {
    try {
        action();
    } catch (const InternalError &) {
        return;
    }
    require(false, message);
}

void test_insert_temp_ids_and_temp_update() {
    TransactionWriteBatch batch;
    const TempRowId first = batch.stage_insert("t", 7, bytes("one"));
    const TempRowId second = batch.stage_insert("t", 7, bytes("two"));
    batch.stage_update(first, bytes("one updated"));

    require(first == 1 && second == 2, "inserts must receive monotonic temp ids");
    require(batch.writes().size() == 2, "temp update must coalesce into its insert");
    require(batch.writes()[0].kind == LogicalWriteKind::INSERT,
            "temp update changed insert kind");
    require(batch.writes()[0].temp_id == first,
            "temp update changed insert identity");
    require(batch.writes()[0].after == bytes("one updated"),
            "temp update did not replace insert value");
}

void test_insert_then_delete_cancels_insert() {
    TransactionWriteBatch batch;
    const TempRowId id = batch.stage_insert("t", 7, bytes("one"));
    batch.stage_delete(id);

    require(batch.writes().empty(), "insert then delete must cancel the staged write");
    require(batch.freeze().empty(), "cancelled insert must not be frozen");
    require_internal_error([&] { batch.stage_update(id, bytes("new")); },
                           "cancelled temp id must be rejected");
}

void test_update_coalesces_and_owns_visible_value() {
    TransactionWriteBatch batch;
    const Rid rid{3, 4};
    batch.stage_update("t", 7, rid, bytes("old"), bytes("first"));
    batch.stage_update("t", 7, rid, bytes("first"), bytes("new"));

    require(batch.writes().size() == 1, "updates for one record must coalesce");
    const StagedWrite &write = batch.writes().front();
    require(write.kind == LogicalWriteKind::UPDATE, "coalesced write must remain update");
    require(write.before == bytes("old"), "coalesced update must retain original value");
    require(write.after == bytes("new"), "coalesced update must expose final value");

    std::vector<char> visible;
    require(batch.lookup(7, rid, &visible) == OverlayKind::VALUE,
            "own update is not visible");
    require(visible == bytes("new"), "wrong overlay bytes");
}

void test_update_then_delete_coalesces_to_delete() {
    TransactionWriteBatch batch;
    const Rid rid{3, 4};
    batch.stage_update("t", 7, rid, bytes("old"), bytes("new"));
    batch.stage_delete("t", 7, rid, bytes("new"));

    require(batch.writes().size() == 1, "update then delete must coalesce");
    const StagedWrite &write = batch.writes().front();
    require(write.kind == LogicalWriteKind::DELETE, "coalesced write must become delete");
    require(write.before == bytes("old"), "delete must retain original before image");
    std::vector<char> visible;
    require(batch.lookup(7, rid, &visible) == OverlayKind::DELETED,
            "own delete must hide the record");
}

void test_freeze_orders_existing_records_then_inserts() {
    TransactionWriteBatch batch;
    const TempRowId first_insert = batch.stage_insert("t", 9, bytes("first"));
    batch.stage_update("b", 8, Rid{2, 1}, bytes("old"), bytes("b"));
    batch.stage_delete("a", 7, Rid{3, 0}, bytes("old"));
    batch.stage_update("a", 7, Rid{1, 2}, bytes("old"), bytes("a"));
    const TempRowId second_insert = batch.stage_insert("t", 7, bytes("second"));

    const std::vector<StagedWrite> frozen = batch.freeze();
    require(frozen.size() == 5, "freeze lost a surviving write");
    require(frozen[0].rid == Rid{1, 2} && frozen[1].rid == Rid{3, 0} &&
                frozen[2].rid == Rid{2, 1},
            "existing writes must be sorted by file, page, slot");
    require(frozen[3].kind == LogicalWriteKind::INSERT &&
                frozen[3].temp_id == first_insert &&
                frozen[4].kind == LogicalWriteKind::INSERT &&
                frozen[4].temp_id == second_insert,
            "inserts must follow existing writes in temp-id order");
    require_internal_error([&] { batch.stage_insert("t", 7, bytes("late")); },
                           "frozen batch must reject new writes");
}

void test_discard_clears_overlay_and_rejects_new_writes() {
    Transaction transaction(41);
    TransactionWriteBatch &batch = transaction.write_batch();
    const Rid rid{3, 4};
    batch.stage_update("t", 7, rid, bytes("old"), bytes("new"));
    batch.discard();

    std::vector<char> visible;
    require(batch.writes().empty(), "discard must clear staged writes");
    require(batch.lookup(7, rid, &visible) == OverlayKind::ABSENT,
            "discard must clear the own-write overlay");
    require_internal_error([&] { batch.stage_delete("t", 7, rid, bytes("old")); },
                           "discarded batch must reject new writes");
}

}  // namespace

int main() {
    test_insert_temp_ids_and_temp_update();
    test_insert_then_delete_cancels_insert();
    test_update_coalesces_and_owns_visible_value();
    test_update_then_delete_coalesces_to_delete();
    test_freeze_orders_existing_records_then_inserts();
    test_discard_clears_overlay_and_rejects_new_writes();
    std::cout << "transaction write batch tests passed\n";
    return 0;
}
