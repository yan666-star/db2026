#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "network/prepared_dictionary.h"
#include "network/wire_codec.h"
#include "network/wire_messages.h"

namespace {

int failures = 0;

void expect_true(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        failures++;
    }
}

template <typename Fn>
void expect_protocol_error(Fn fn, const std::string &message) {
    try {
        fn();
        expect_true(false, message);
    } catch (const rmdb::wire::ProtocolError &) {
    }
}

rmdb::wire::PreparedDictionary make_dictionary() {
    rmdb::wire::PreparedDictionary dictionary;
    dictionary.install_atomically(
        {
            {7,
             rmdb::wire::ResultKind::QUERY,
             {rmdb::wire::SqlType::INT32,
              rmdb::wire::SqlType::FLOAT32,
              rmdb::wire::SqlType::CHAR},
             "SELECT a, b, c FROM t WHERE x=$1 AND y=$2 AND z=$3;"},
            {8, rmdb::wire::ResultKind::COMMAND, {}, "COMMIT;"},
        },
        [](const rmdb::wire::PrepareEntry &entry) {
            if (entry.result_kind == rmdb::wire::ResultKind::COMMAND) {
                return std::vector<rmdb::execution::OutputColumn>{};
            }
            return std::vector<rmdb::execution::OutputColumn>{
                {"a", rmdb::wire::SqlType::INT32},
                {"b", rmdb::wire::SqlType::FLOAT32},
                {"c", rmdb::wire::SqlType::CHAR}};
        });
    return dictionary;
}

void test_exec_stream_requires_non_empty_utf8() {
    const std::vector<uint8_t> sql{'s', 'h', 'o', 'w', ' ', 't', 'a', 'b',
                                   'l', 'e', 's', ';'};
    expect_true(rmdb::wire::decode_exec_stream(sql) == "show tables;",
                "EXEC_STREAM must preserve its UTF-8 SQL bytes");
    expect_protocol_error(
        [] { (void)rmdb::wire::decode_exec_stream({}); },
        "Empty EXEC_STREAM SQL must be rejected");
    expect_protocol_error(
        [] { (void)rmdb::wire::decode_exec_stream({0xff}); },
        "Invalid UTF-8 EXEC_STREAM SQL must be rejected");
}

void test_batch_decode_uses_dictionary_types() {
    const auto dictionary = make_dictionary();
    rmdb::wire::WireWriter payload;
    payload.put_u16(2);
    payload.put_u16(7);
    payload.put_u8(1);
    payload.put_i32(-2);
    payload.put_u8(1);
    payload.put_u32(0x3f800001U);
    payload.put_u8(1);
    payload.put_string_u32("abc");
    payload.put_u16(8);

    const auto batch =
        rmdb::wire::decode_exec_batch(payload.bytes(), dictionary);
    expect_true(batch.operations.size() == 2,
                "EXEC_BATCH must decode every operation");
    expect_true(batch.operations[0].statement->statement_id == 7,
                "Batch operation must resolve its statement dictionary entry");
    expect_true(batch.operations[0].parameters[0].int32_value() == -2,
                "INT32 bind must preserve its signed value");
    expect_true(batch.operations[0].parameters[1].float_bits() == 0x3f800001U,
                "FLOAT32 bind must preserve raw bits");
    expect_true(batch.operations[0].parameters[2].char_value() == "abc",
                "CHAR bind must use its logical byte count");

    auto trailing = payload.bytes();
    trailing.push_back(0);
    expect_protocol_error(
        [&] { (void)rmdb::wire::decode_exec_batch(trailing, dictionary); },
        "EXEC_BATCH trailing bytes must be rejected");
}

void test_stream_response_frame_sequence_is_typed() {
    const std::vector<rmdb::execution::OutputColumn> schema{
        {"i", rmdb::wire::SqlType::INT32},
        {"f", rmdb::wire::SqlType::FLOAT32},
        {"s", rmdb::wire::SqlType::CHAR}};
    const std::vector<rmdb::execution::TypedValue> row{
        rmdb::execution::TypedValue::Int32(42),
        rmdb::execution::TypedValue::FloatBits(0x80000000U),
        rmdb::execution::TypedValue::Char("xy")};

    const auto meta = rmdb::wire::make_meta_frame(schema);
    const auto row_frame = rmdb::wire::make_row_frame(schema, row);
    const auto end = rmdb::wire::make_result_end_frame(1);
    const auto command = rmdb::wire::make_command_ok_frame();

    expect_true(rmdb::wire::decode_header(meta.data(), 8).tag ==
                    static_cast<uint8_t>(rmdb::wire::ServerTag::META),
                "Query schema must use one META frame");
    expect_true(rmdb::wire::decode_header(row_frame.data(), 8).tag ==
                    static_cast<uint8_t>(rmdb::wire::ServerTag::ROW),
                "Typed query row must use a ROW frame");
    expect_true(rmdb::wire::decode_header(end.data(), 8).tag ==
                    static_cast<uint8_t>(rmdb::wire::ServerTag::RESULT_END),
                "Query success must terminate with RESULT_END");
    expect_true(rmdb::wire::decode_header(command.data(), 8).tag ==
                    static_cast<uint8_t>(rmdb::wire::ServerTag::COMMAND_OK),
                "Command success must use COMMAND_OK");

    rmdb::wire::WireReader row_reader(
        row_frame.data() + rmdb::wire::kFrameHeaderBytes,
        row_frame.size() - rmdb::wire::kFrameHeaderBytes);
    expect_true(row_reader.get_u8() == 1 && row_reader.get_i32() == 42,
                "ROW INT32 cell must use present plus big-endian i32");
    expect_true(row_reader.get_u8() == 1 &&
                    row_reader.get_u32() == 0x80000000U,
                "ROW FLOAT32 cell must send its raw bits");
    expect_true(row_reader.get_u8() == 1 &&
                    row_reader.get_u32() == 2 &&
                    row_reader.get_string(2) == "xy",
                "ROW CHAR cell must send logical length and bytes");
    row_reader.require_consumed();
}

void test_prepare_ok_and_batch_result_are_unique_frames() {
    const auto dictionary = make_dictionary();
    const auto prepare = rmdb::wire::make_prepare_ok_frame(dictionary);
    const auto prepare_header =
        rmdb::wire::decode_header(prepare.data(), rmdb::wire::kFrameHeaderBytes);
    expect_true(
        prepare_header.tag ==
            static_cast<uint8_t>(rmdb::wire::ServerTag::PREPARE_OK),
        "Successful dictionary install must return PREPARE_OK");

    rmdb::wire::WireWriter request_payload;
    request_payload.put_u16(2);
    request_payload.put_u16(7);
    request_payload.put_u8(1);
    request_payload.put_i32(1);
    request_payload.put_u8(1);
    request_payload.put_u32(0x3f800000U);
    request_payload.put_u8(1);
    request_payload.put_string_u32("key");
    request_payload.put_u16(8);
    const auto request =
        rmdb::wire::decode_exec_batch(request_payload.bytes(), dictionary);

    rmdb::wire::BatchResult result;
    result.executed_operations = 2;
    result.status = rmdb::wire::BatchStatus::OK;
    result.failed_operation = 0xffffU;
    result.results.push_back(
        {0,
         {{rmdb::execution::TypedValue::Int32(9),
           rmdb::execution::TypedValue::FloatBits(0x40000000U),
           rmdb::execution::TypedValue::Char("done")}}});

    const auto frame = rmdb::wire::make_batch_result_frame(result, request);
    const auto header =
        rmdb::wire::decode_header(frame.data(), rmdb::wire::kFrameHeaderBytes);
    expect_true(
        header.tag ==
            static_cast<uint8_t>(rmdb::wire::ServerTag::BATCH_RESULT),
        "EXEC_BATCH must return one BATCH_RESULT frame");

    result.status = rmdb::wire::BatchStatus::TRANSACTION_ABORT;
    expect_protocol_error(
        [&] { (void)rmdb::wire::make_batch_result_frame(result, request); },
        "Failed BATCH_RESULT must reject leaked partial query rows");
}

}  // namespace

int main() {
    test_exec_stream_requires_non_empty_utf8();
    test_batch_decode_uses_dictionary_types();
    test_stream_response_frame_sequence_is_typed();
    test_prepare_ok_and_batch_result_are_unique_frames();

    if (failures != 0) {
        std::cerr << failures << " wire message test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "wire_messages_test: PASS\n";
    return EXIT_SUCCESS;
}
