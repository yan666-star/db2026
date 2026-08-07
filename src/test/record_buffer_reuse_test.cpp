#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "record/rm.h"
#include "record/rm_record_pool.h"
#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void require_same_size_record_is_reused() {
    RmRecordPool pool;
    auto first = pool.acquire(sizeof(int));
    RmRecord *object_address = first.get();
    char *data_address = first->data;
    int old_value = 17;
    std::memcpy(first->data, &old_value, sizeof(old_value));

    pool.release(std::move(first));
    require(pool.available() == 1, "released record must enter the pool");

    auto reused = pool.acquire(sizeof(int));
    require(reused.get() == object_address,
            "same-size acquire must reuse the record object");
    require(reused->data == data_address,
            "same-size acquire must reuse the record data buffer");
    require(pool.available() == 0,
            "acquire must remove the record from the pool");
}

void require_wrong_size_record_is_not_returned() {
    RmRecordPool pool;
    pool.release(std::make_unique<RmRecord>(sizeof(int) * 2));
    auto record = pool.acquire(sizeof(int));
    require(record->size == static_cast<int>(sizeof(int)),
            "acquire must return the requested record size");
}

void require_batch_read_reuses_buffers_and_overwrites_contents() {
    const std::string file_name = "record_buffer_reuse_test.db";
    DiskManager disk_manager;
    BufferPoolManager buffer_pool_manager(16, &disk_manager);
    RmManager rm_manager(&disk_manager, &buffer_pool_manager);
    if (disk_manager.is_file(file_name)) {
        disk_manager.destroy_file(file_name);
    }
    rm_manager.create_file(file_name, sizeof(int));
    auto file = rm_manager.open_file(file_name);

    std::vector<Rid> rids;
    for (int value : {1, 2, 3}) {
        rids.push_back(file->insert_record(
            reinterpret_cast<char *>(&value), nullptr));
    }
    std::vector<Rid> first_rids = rids;
    auto first = file->batch_get_records(rids.front().page_no,
                                         first_rids, nullptr);
    std::set<char *> buffers;
    RmRecordPool pool;
    for (auto &record : first) {
        buffers.insert(record->data);
        pool.release(std::move(record));
    }

    for (size_t i = 0; i < rids.size(); ++i) {
        int value = static_cast<int>(101 + i);
        file->update_record(rids[i], reinterpret_cast<char *>(&value),
                            nullptr);
    }

    std::vector<Rid> second_rids = rids;
    auto second = file->batch_get_records(rids.front().page_no,
                                          second_rids, nullptr, &pool);
    require(second.size() == rids.size(),
            "batch read must retain every RID");
    require(pool.available() == 0,
            "batch read must consume reusable buffers");
    for (size_t i = 0; i < second.size(); ++i) {
        int value = 0;
        std::memcpy(&value, second[i]->data, sizeof(value));
        require(value == static_cast<int>(101 + i),
                "reused buffer must contain the latest page contents");
        require(buffers.count(second[i]->data) == 1,
                "batch read must reuse an available same-size buffer");
    }

    rm_manager.close_file(file.get());
    rm_manager.destroy_file(file_name);
}

}  // namespace

int main() {
    require_same_size_record_is_reused();
    require_wrong_size_record_is_not_returned();
    require_batch_read_reuses_buffers_and_overwrites_contents();
    std::cout << "record buffer reuse tests passed\n";
    return 0;
}
