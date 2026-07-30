#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "network/connection_session.h"
#include "network/wire_codec.h"

namespace {

int failures = 0;

class TestExecutable final : public rmdb::wire::PreparedExecutable {};

void expect_true(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        failures++;
    }
}

class FakeExecutionService final : public rmdb::wire::ExecutionService {
 public:
    rmdb::wire::PreparedArtifact prepare(
        const rmdb::wire::PrepareEntry &) override {
        return {{}, std::make_shared<TestExecutable>()};
    }

    void execute_stream(
        const std::string &sql,
        rmdb::execution::ResultSink &sink) override {
        if (sql == "SELECT 1;") {
            sink.begin_query({{"value", rmdb::wire::SqlType::INT32}});
            sink.push_row({rmdb::execution::TypedValue::Int32(1)});
            sink.end_query(1);
        } else {
            sink.command_ok();
        }
    }

    void execute_prepared(
        const rmdb::wire::PreparedStatement &,
        const std::vector<rmdb::execution::TypedValue> &,
        rmdb::execution::ResultSink &sink) override {
        sink.command_ok();
    }

    bool has_active_transaction() const override { return false; }
    void abort_active_transaction() override {}
};

void append(std::vector<uint8_t> &destination,
            const std::vector<uint8_t> &source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

std::vector<uint8_t> stream_request(const std::string &sql) {
    const std::vector<uint8_t> payload(sql.begin(), sql.end());
    return rmdb::wire::encode_frame(
        static_cast<uint8_t>(rmdb::wire::ClientTag::EXEC_STREAM),
        0,
        payload);
}

void test_fragmented_connection_processes_multiple_requests() {
    std::vector<uint8_t> input(
        rmdb::wire::kHandshakeV3.begin(), rmdb::wire::kHandshakeV3.end());
    append(input, stream_request("SELECT 1;"));
    append(input, stream_request("COMMIT;"));

    size_t read_offset = 0;
    std::vector<uint8_t> output;
    const rmdb::wire::ReadSome read_some =
        [&](uint8_t *destination, size_t capacity) {
            if (read_offset == input.size()) {
                return rmdb::wire::IoAttempt{
                    rmdb::wire::IoAttemptStatus::EOF_REACHED, 0};
            }
            const size_t amount =
                std::min({capacity, size_t{2}, input.size() - read_offset});
            std::memcpy(destination, input.data() + read_offset, amount);
            read_offset += amount;
            return rmdb::wire::IoAttempt{
                rmdb::wire::IoAttemptStatus::PROGRESS, amount};
        };
    const rmdb::wire::WriteSome write_some =
        [&](const uint8_t *source, size_t size) {
            const size_t amount = std::min(size, size_t{3});
            output.insert(output.end(), source, source + amount);
            return rmdb::wire::IoAttempt{
                rmdb::wire::IoAttemptStatus::PROGRESS, amount};
        };

    FakeExecutionService service;
    rmdb::wire::ConnectionSession session(service);
    const auto result = session.run_with(read_some, write_some);
    expect_true(result == rmdb::wire::SessionResult::PEER_CLOSED,
                "Clean EOF between requests must close the session normally");
    expect_true(
        output.size() >= rmdb::wire::kHandshakeBytes &&
            std::equal(
                rmdb::wire::kHandshakeV3.begin(),
                rmdb::wire::kHandshakeV3.end(),
                output.begin()),
        "Session must echo the exact v3 handshake before any frame");

    size_t offset = rmdb::wire::kHandshakeBytes;
    std::vector<uint8_t> tags;
    while (offset < output.size()) {
        const auto header = rmdb::wire::decode_header(
            output.data() + offset, rmdb::wire::kFrameHeaderBytes);
        tags.push_back(header.tag);
        offset += rmdb::wire::kFrameHeaderBytes + header.payload_bytes;
    }
    expect_true(
        tags == std::vector<uint8_t>({
                    static_cast<uint8_t>(rmdb::wire::ServerTag::META),
                    static_cast<uint8_t>(rmdb::wire::ServerTag::ROW),
                    static_cast<uint8_t>(rmdb::wire::ServerTag::RESULT_END),
                    static_cast<uint8_t>(rmdb::wire::ServerTag::COMMAND_OK)}),
        "One connection must process sequential requests to unique terminals");
}

void test_unsupported_handshake_closes_without_sql_or_reply() {
    std::vector<uint8_t> input(
        rmdb::wire::kHandshakeV3.begin(), rmdb::wire::kHandshakeV3.end());
    input[5] = 2;
    size_t offset = 0;
    std::vector<uint8_t> output;
    const rmdb::wire::ReadSome read_some =
        [&](uint8_t *destination, size_t capacity) {
            const size_t amount = std::min(capacity, input.size() - offset);
            std::memcpy(destination, input.data() + offset, amount);
            offset += amount;
            return rmdb::wire::IoAttempt{
                rmdb::wire::IoAttemptStatus::PROGRESS, amount};
        };
    const rmdb::wire::WriteSome write_some =
        [&](const uint8_t *source, size_t size) {
            output.insert(output.end(), source, source + size);
            return rmdb::wire::IoAttempt{
                rmdb::wire::IoAttemptStatus::PROGRESS, size};
        };

    FakeExecutionService service;
    rmdb::wire::ConnectionSession session(service);
    expect_true(
        session.run_with(read_some, write_some) ==
            rmdb::wire::SessionResult::UNSUPPORTED_HANDSHAKE,
        "Unsupported handshake must be classified before frame parsing");
    expect_true(output.empty(),
                "Unsupported handshake must close without a protocol reply");
}

}  // namespace

int main() {
    test_fragmented_connection_processes_multiple_requests();
    test_unsupported_handshake_closes_without_sql_or_reply();

    if (failures != 0) {
        std::cerr << failures << " connection session test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "connection_session_test: PASS\n";
    return EXIT_SUCCESS;
}
