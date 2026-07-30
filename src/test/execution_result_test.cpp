#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "execution/execution_result.h"

namespace {

int failures = 0;

void expect_true(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        failures++;
    }
}

void test_float_preserves_binary32_bits() {
    const uint32_t expected_bits = 0x3f800001U;
    float value = 0.0F;
    std::memcpy(&value, &expected_bits, sizeof(value));

    const auto typed = rmdb::execution::TypedValue::Float(value);
    expect_true(typed.type() == rmdb::wire::SqlType::FLOAT32,
                "Float value must expose FLOAT32 schema type");
    expect_true(typed.float_bits() == expected_bits,
                "Float value must preserve the raw binary32 bits");
}

void test_char_uses_logical_bytes_without_storage_padding() {
    const char stored[8] = {'a', 'b', 'c', '\0', '\0', '\0', '\0', '\0'};
    const std::string logical =
        rmdb::execution::logical_char_bytes(stored, sizeof(stored));
    expect_true(logical == "abc",
                "CHAR result must omit fixed-width NUL padding");

    const auto typed = rmdb::execution::TypedValue::Char(logical);
    expect_true(typed.type() == rmdb::wire::SqlType::CHAR,
                "CHAR value must expose CHAR schema type");
    expect_true(typed.char_value() == "abc",
                "CHAR value must retain its logical bytes");
}

void test_int32_and_null_are_distinct() {
    const auto integer = rmdb::execution::TypedValue::Int32(-7);
    const auto null_integer =
        rmdb::execution::TypedValue::Null(rmdb::wire::SqlType::INT32);

    expect_true(integer.present(), "INT32 constructor must create a value");
    expect_true(integer.int32_value() == -7,
                "INT32 constructor must preserve signed value");
    expect_true(!null_integer.present(),
                "NULL constructor must set present=0");
    expect_true(null_integer.type() == rmdb::wire::SqlType::INT32,
                "Protocol NULL must retain its schema type");
}

class CapturingSink final : public rmdb::execution::ResultSink {
 public:
    void begin_query(
        const std::vector<rmdb::execution::OutputColumn> &schema) override {
        schema_ = schema;
    }

    void push_row(
        const std::vector<rmdb::execution::TypedValue> &row) override {
        rows_.push_back(row);
    }

    void end_query(uint64_t row_count) override { ended_rows_ = row_count; }

    void command_ok() override { command_ok_ = true; }

    std::vector<rmdb::execution::OutputColumn> schema_;
    std::vector<std::vector<rmdb::execution::TypedValue>> rows_;
    uint64_t ended_rows_{0};
    bool command_ok_{false};
};

void test_result_sink_keeps_schema_and_row_count_typed() {
    CapturingSink sink;
    sink.begin_query({{"answer", rmdb::wire::SqlType::INT32}});
    sink.push_row({rmdb::execution::TypedValue::Int32(42)});
    sink.end_query(1);

    expect_true(sink.schema_.size() == 1,
                "Query sink must receive one schema definition");
    expect_true(sink.schema_[0].name == "answer",
                "Query sink must preserve the output name");
    expect_true(sink.rows_[0][0].int32_value() == 42,
                "Query sink must receive typed row values");
    expect_true(sink.ended_rows_ == 1,
                "Query sink must receive the exact terminal row count");
}

}  // namespace

int main() {
    test_float_preserves_binary32_bits();
    test_char_uses_logical_bytes_without_storage_padding();
    test_int32_and_null_are_distinct();
    test_result_sink_keeps_schema_and_row_count_typed();

    if (failures != 0) {
        std::cerr << failures << " execution result test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "execution_result_test: PASS\n";
    return EXIT_SUCCESS;
}
