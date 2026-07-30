#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "network/request_dispatcher.h"
#include "network/wire_codec.h"

namespace {

int failures = 0;

void expect_true(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        failures++;
    }
}

class FakeExecutionService final : public rmdb::wire::ExecutionService {
 public:
    std::vector<rmdb::execution::OutputColumn> prepare(
        const rmdb::wire::PrepareEntry &entry) override {
        if (entry.result_kind == rmdb::wire::ResultKind::QUERY) {
            return {{"value", rmdb::wire::SqlType::INT32}};
        }
        return {};
    }

    void execute_stream(
        const std::string &sql,
        rmdb::execution::ResultSink &sink) override {
        if (sql == "SELECT 1;") {
            sink.begin_query({{"value", rmdb::wire::SqlType::INT32}});
            sink.push_row({rmdb::execution::TypedValue::Int32(1)});
            sink.end_query(1);
            return;
        }
        sink.command_ok();
    }

    void execute_prepared(
        const rmdb::wire::PreparedStatement &statement,
        const std::vector<rmdb::execution::TypedValue> &,
        rmdb::execution::ResultSink &sink) override {
        events.push_back("execute:" + std::to_string(statement.statement_id));
        if (statement.statement_id == 1) {
            active = true;
            sink.command_ok();
        } else if (statement.statement_id == 2) {
            sink.begin_query(statement.output_schema);
            sink.push_row({rmdb::execution::TypedValue::Int32(7)});
            sink.end_query(1);
        } else if (statement.statement_id == 3) {
            throw rmdb::wire::TransactionAbortError("write conflict");
        } else if (statement.statement_id == 4) {
            throw std::runtime_error("broken statement");
        } else {
            sink.command_ok();
        }
    }

    bool has_active_transaction() const override { return active; }

    void abort_active_transaction() override {
        events.push_back("abort");
        active = false;
    }

    bool active{false};
    std::vector<std::string> events;
};

std::vector<uint8_t> make_prepare_payload() {
    rmdb::wire::WireWriter payload;
    payload.put_u16(4);
    payload.put_u16(1);
    payload.put_u8(0);
    payload.put_u16(0);
    payload.put_string_u32("BEGIN;");
    payload.put_u16(2);
    payload.put_u8(1);
    payload.put_u16(0);
    payload.put_string_u32("SELECT a FROM t;");
    payload.put_u16(3);
    payload.put_u8(0);
    payload.put_u16(0);
    payload.put_string_u32("UPDATE t SET a=1;");
    payload.put_u16(4);
    payload.put_u8(0);
    payload.put_u16(0);
    payload.put_string_u32("DELETE FROM t;");
    return payload.take_bytes();
}

std::vector<uint8_t> make_batch_payload(
    const std::vector<uint16_t> &statement_ids) {
    rmdb::wire::WireWriter payload;
    payload.put_u16(static_cast<uint16_t>(statement_ids.size()));
    for (const uint16_t id : statement_ids) {
        payload.put_u16(id);
    }
    return payload.take_bytes();
}

rmdb::wire::FrameHeader request_header(
    rmdb::wire::ClientTag tag, uint32_t payload_bytes, uint8_t flags = 0) {
    return {
        payload_bytes, static_cast<uint8_t>(tag), flags, 0};
}

rmdb::wire::FrameHeader response_header(
    const std::vector<uint8_t> &frame) {
    return rmdb::wire::decode_header(
        frame.data(), rmdb::wire::kFrameHeaderBytes);
}

void install_dictionary(rmdb::wire::RequestDispatcher &dispatcher) {
    const auto payload = make_prepare_payload();
    std::vector<std::vector<uint8_t>> frames;
    dispatcher.dispatch(
        request_header(
            rmdb::wire::ClientTag::PREPARE_SET,
            static_cast<uint32_t>(payload.size())),
        payload,
        [&](const std::vector<uint8_t> &frame) { frames.push_back(frame); });
    expect_true(
        frames.size() == 1 &&
            response_header(frames[0]).tag ==
                static_cast<uint8_t>(rmdb::wire::ServerTag::PREPARE_OK),
        "PREPARE_SET must install atomically and return one PREPARE_OK");
}

void test_stream_query_emits_meta_row_end() {
    FakeExecutionService service;
    rmdb::wire::RequestDispatcher dispatcher(service);
    const std::string sql = "SELECT 1;";
    const std::vector<uint8_t> payload(sql.begin(), sql.end());
    std::vector<uint8_t> tags;
    dispatcher.dispatch(
        request_header(
            rmdb::wire::ClientTag::EXEC_STREAM,
            static_cast<uint32_t>(payload.size())),
        payload,
        [&](const std::vector<uint8_t> &frame) {
            tags.push_back(response_header(frame).tag);
        });
    expect_true(
        tags == std::vector<uint8_t>({
                    static_cast<uint8_t>(rmdb::wire::ServerTag::META),
                    static_cast<uint8_t>(rmdb::wire::ServerTag::ROW),
                    static_cast<uint8_t>(rmdb::wire::ServerTag::RESULT_END)}),
        "EXEC_STREAM query must emit META, ROW, RESULT_END in order");
}

void test_batch_abort_happens_before_unique_failure_reply() {
    FakeExecutionService service;
    rmdb::wire::RequestDispatcher dispatcher(service);
    install_dictionary(dispatcher);
    service.events.clear();

    const auto payload = make_batch_payload({1, 2, 3});
    std::vector<std::vector<uint8_t>> frames;
    dispatcher.dispatch(
        request_header(
            rmdb::wire::ClientTag::EXEC_BATCH,
            static_cast<uint32_t>(payload.size()),
            rmdb::wire::kExecBatchAutoAbort),
        payload,
        [&](const std::vector<uint8_t> &frame) {
            service.events.push_back("reply");
            frames.push_back(frame);
        });

    expect_true(
        service.events ==
            std::vector<std::string>(
                {"execute:1", "execute:2", "execute:3", "abort", "reply"}),
        "AUTO_ABORT must finish rollback before sending the failure reply");
    expect_true(!service.active,
                "Failed AUTO_ABORT batch must end the active transaction");
    expect_true(frames.size() == 1,
                "EXEC_BATCH failure must emit exactly one frame");

    const auto header = response_header(frames[0]);
    expect_true(
        header.tag ==
            static_cast<uint8_t>(rmdb::wire::ServerTag::BATCH_RESULT),
        "Executed batch failure must use BATCH_RESULT");
    rmdb::wire::WireReader result(
        frames[0].data() + rmdb::wire::kFrameHeaderBytes,
        frames[0].size() - rmdb::wire::kFrameHeaderBytes);
    expect_true(result.get_u16() == 2,
                "Failure must count only operations before the victim");
    expect_true(result.get_u8() == 1,
                "Retryable conflict must use TRANSACTION_ABORT status");
    expect_true(result.get_u16() == 2,
                "failed_operation must identify the current statement");
    const uint32_t diagnostic_bytes = result.get_u32();
    (void)result.get_string(diagnostic_bytes);
    expect_true(result.get_u16() == 0,
                "Failure must discard all earlier query results");
    result.require_consumed();
}

void test_decode_error_also_auto_aborts_before_top_level_error() {
    FakeExecutionService service;
    rmdb::wire::RequestDispatcher dispatcher(service);
    install_dictionary(dispatcher);
    service.active = true;
    service.events.clear();

    rmdb::wire::WireWriter invalid;
    invalid.put_u16(1);
    invalid.put_u16(999);
    std::vector<std::vector<uint8_t>> frames;
    dispatcher.dispatch(
        request_header(
            rmdb::wire::ClientTag::EXEC_BATCH,
            static_cast<uint32_t>(invalid.size()),
            rmdb::wire::kExecBatchAutoAbort),
        invalid.bytes(),
        [&](const std::vector<uint8_t> &frame) {
            service.events.push_back("reply");
            frames.push_back(frame);
        });
    expect_true(service.events ==
                    std::vector<std::string>({"abort", "reply"}),
                "Undecodable AUTO_ABORT request must rollback before reply");
    expect_true(
        frames.size() == 1 &&
            response_header(frames[0]).tag ==
                static_cast<uint8_t>(rmdb::wire::ServerTag::ERROR),
        "Undecodable batch may return one top-level ERROR");
}

void test_success_batch_has_no_per_command_ack() {
    FakeExecutionService service;
    rmdb::wire::RequestDispatcher dispatcher(service);
    install_dictionary(dispatcher);
    const auto payload = make_batch_payload({1, 2});
    std::vector<std::vector<uint8_t>> frames;
    dispatcher.dispatch(
        request_header(
            rmdb::wire::ClientTag::EXEC_BATCH,
            static_cast<uint32_t>(payload.size()),
            rmdb::wire::kExecBatchAutoAbort),
        payload,
        [&](const std::vector<uint8_t> &frame) { frames.push_back(frame); });
    expect_true(
        frames.size() == 1 &&
            response_header(frames[0]).tag ==
                static_cast<uint8_t>(rmdb::wire::ServerTag::BATCH_RESULT),
        "Successful batch must aggregate all output into one BATCH_RESULT");
}

}  // namespace

int main() {
    test_stream_query_emits_meta_row_end();
    test_batch_abort_happens_before_unique_failure_reply();
    test_decode_error_also_auto_aborts_before_top_level_error();
    test_success_batch_has_no_per_command_ack();

    if (failures != 0) {
        std::cerr << failures << " request dispatcher test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "request_dispatcher_test: PASS\n";
    return EXIT_SUCCESS;
}
