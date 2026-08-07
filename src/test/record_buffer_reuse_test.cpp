#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

#include "record/rm_record_pool.h"

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

}  // namespace

int main() {
    require_same_size_record_is_reused();
    require_wrong_size_record_is_not_returned();
    std::cout << "record buffer reuse tests passed\n";
    return 0;
}
