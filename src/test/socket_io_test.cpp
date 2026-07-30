#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "network/socket_io.h"

namespace {

int failures = 0;

void expect_true(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        failures++;
    }
}

void test_read_exact_combines_fragments_and_retries_interrupts() {
    const std::vector<uint8_t> source{
        0x52U, 0x4dU, 0x44U, 0x42U, 0x00U, 0x03U, 0x00U, 0x00U};
    const std::vector<size_t> chunks{2U, 1U, 5U};
    size_t chunk_index = 0;
    size_t source_offset = 0;
    bool interrupted_once = false;
    std::vector<uint8_t> output(source.size());

    auto read_some = [&](uint8_t *destination,
                         size_t capacity) -> rmdb::wire::IoAttempt {
        if (!interrupted_once) {
            interrupted_once = true;
            return {rmdb::wire::IoAttemptStatus::INTERRUPTED, 0};
        }
        const size_t count =
            std::min({chunks.at(chunk_index++), capacity,
                      source.size() - source_offset});
        std::memcpy(destination, source.data() + source_offset, count);
        source_offset += count;
        return {rmdb::wire::IoAttemptStatus::PROGRESS, count};
    };

    expect_true(
        rmdb::wire::read_exact_with(read_some, output.data(), output.size()) ==
            rmdb::wire::IoResult::OK,
        "read_exact must combine fragments and retry interrupted reads");
    expect_true(output == source,
                "read_exact must preserve all handshake bytes");
}

void test_read_exact_reports_early_eof() {
    std::vector<uint8_t> output(8U);
    bool first = true;
    auto read_some = [&](uint8_t *destination,
                         size_t) -> rmdb::wire::IoAttempt {
        if (first) {
            first = false;
            destination[0] = 0x52U;
            return {rmdb::wire::IoAttemptStatus::PROGRESS, 1U};
        }
        return {rmdb::wire::IoAttemptStatus::EOF_REACHED, 0U};
    };

    expect_true(
        rmdb::wire::read_exact_with(read_some, output.data(), output.size()) ==
            rmdb::wire::IoResult::EOF_REACHED,
        "read_exact must distinguish early EOF from a complete read");
}

void test_write_all_handles_short_writes() {
    const std::vector<uint8_t> input{
        0x52U, 0x4dU, 0x44U, 0x42U, 0x00U, 0x03U, 0x00U, 0x00U};
    std::vector<uint8_t> written;
    auto write_some = [&](const uint8_t *source,
                          size_t size) -> rmdb::wire::IoAttempt {
        const size_t count = std::min<size_t>(3U, size);
        written.insert(written.end(), source, source + count);
        return {rmdb::wire::IoAttemptStatus::PROGRESS, count};
    };

    expect_true(
        rmdb::wire::write_all_with(write_some, input.data(), input.size()) ==
            rmdb::wire::IoResult::OK,
        "write_all must continue after short writes");
    expect_true(written == input, "write_all must emit every byte once");
}

void test_handshake_is_exact_and_echoes_only_supported_version() {
    std::vector<uint8_t> incoming(
        rmdb::wire::kHandshakeV3.begin(), rmdb::wire::kHandshakeV3.end());
    size_t incoming_offset = 0;
    std::vector<uint8_t> echoed;

    auto read_some = [&](uint8_t *destination,
                         size_t capacity) -> rmdb::wire::IoAttempt {
        const size_t count =
            std::min<size_t>(2U, incoming.size() - incoming_offset);
        if (count == 0) {
            return {rmdb::wire::IoAttemptStatus::EOF_REACHED, 0U};
        }
        if (count > capacity) {
            return {rmdb::wire::IoAttemptStatus::ERROR, 0U};
        }
        std::memcpy(destination, incoming.data() + incoming_offset, count);
        incoming_offset += count;
        return {rmdb::wire::IoAttemptStatus::PROGRESS, count};
    };
    auto write_some = [&](const uint8_t *source,
                          size_t size) -> rmdb::wire::IoAttempt {
        const size_t count = std::min<size_t>(1U, size);
        echoed.insert(echoed.end(), source, source + count);
        return {rmdb::wire::IoAttemptStatus::PROGRESS, count};
    };

    expect_true(
        rmdb::wire::perform_server_handshake_with(read_some, write_some) ==
            rmdb::wire::HandshakeResult::OK,
        "server must accept the exact RMDB/3.0 handshake");
    expect_true(echoed == incoming,
                "server must echo the exact eight handshake bytes");

    incoming = {0x52U, 0x4dU, 0x44U, 0x42U,
                0x00U, 0x02U, 0x00U, 0x00U};
    incoming_offset = 0;
    echoed.clear();
    expect_true(
        rmdb::wire::perform_server_handshake_with(read_some, write_some) ==
            rmdb::wire::HandshakeResult::UNSUPPORTED,
        "server must reject unsupported handshake versions");
    expect_true(echoed.empty(),
                "server must not echo an unsupported handshake");
}

}  // namespace

int main() {
    test_read_exact_combines_fragments_and_retries_interrupts();
    test_read_exact_reports_early_eof();
    test_write_all_handles_short_writes();
    test_handshake_is_exact_and_echoes_only_supported_version();

    if (failures != 0) {
        std::cerr << failures << " socket I/O test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "socket_io_test: PASS\n";
    return EXIT_SUCCESS;
}
