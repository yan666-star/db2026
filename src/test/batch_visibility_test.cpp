#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "transaction/transaction_manager.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::unique_ptr<RmRecord> make_int_record(int value) {
    auto record = std::make_unique<RmRecord>(sizeof(value));
    std::memcpy(record->data, &value, sizeof(value));
    return record;
}

int record_value(const RmRecord &record) {
    int value = 0;
    std::memcpy(&value, record.data, sizeof(value));
    return value;
}

void require_unversioned_records_keep_ownership() {
    TransactionManager manager(nullptr, nullptr);
    std::vector<Rid> rids{{1, 0}, {1, 1}};
    std::vector<std::unique_ptr<RmRecord>> records;
    records.push_back(make_int_record(7));
    records.push_back(make_int_record(9));
    RmRecord *first_record = records.front().get();

    manager.filter_visible_records(nullptr, 41, rids, records);

    require(records.size() == 2 && rids.size() == 2,
            "unversioned committed records must remain visible");
    require(records.front().get() == first_record,
            "batch visibility must move an unversioned physical record "
            "instead of copying it");
}

void require_pending_insert_visibility_matches_scalar_reads() {
    TransactionManager manager(nullptr, nullptr);
    Transaction writer(1, IsolationLevel::SNAPSHOT_ISOLATION);
    Transaction reader(2, IsolationLevel::READ_COMMITTED);
    Rid rid{2, 3};
    constexpr uint64_t file_id = 73;
    auto inserted = make_int_record(1234);
    manager.prepare_insert(&writer, file_id, rid, *inserted);

    std::vector<Rid> reader_rids{rid};
    std::vector<std::unique_ptr<RmRecord>> reader_records;
    reader_records.push_back(make_int_record(1234));
    manager.filter_visible_records(
        &reader, file_id, reader_rids, reader_records);
    require(reader_records.empty() && reader_rids.empty(),
            "READ COMMITTED batch reads must hide another transaction's "
            "pending insert");

    std::vector<Rid> writer_rids{rid};
    std::vector<std::unique_ptr<RmRecord>> writer_records;
    writer_records.push_back(make_int_record(1234));
    manager.filter_visible_records(
        &writer, file_id, writer_rids, writer_records);
    require(writer_records.size() == 1 && writer_rids.size() == 1 &&
                record_value(*writer_records.front()) == 1234,
            "snapshot batch reads must expose the transaction's own pending "
            "insert");
}

}  // namespace

int main() {
    require_unversioned_records_keep_ownership();
    require_pending_insert_visibility_matches_scalar_reads();
    std::cout << "batch visibility tests passed\n";
    return 0;
}
