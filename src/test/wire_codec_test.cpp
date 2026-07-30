#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "network/wire_codec.h"

namespace {

int failures = 0;

void expect_true(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        failures++;
    }
}

template <typename Exception, typename Fn>
void expect_throws(Fn &&fn, const std::string &message) {
    try {
        fn();
    } catch (const Exception &) {
        return;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << message << " (wrong exception: "
                  << e.what() << ")\n";
        failures++;
        return;
    }
    std::cerr << "FAIL: " << message << " (no exception)\n";
    failures++;
}

void test_header_is_big_endian() {
    rmdb::wire::WireWriter writer;
    writer.put_u32(0x01020304U);
    writer.put_u8(0x20U);
    writer.put_u8(0U);
    writer.put_u16(0U);

    const std::vector<uint8_t> expected{
        0x01U, 0x02U, 0x03U, 0x04U, 0x20U, 0x00U, 0x00U, 0x00U};
    expect_true(writer.bytes() == expected,
                "Wire header integers must use big-endian encoding");
}

void test_reader_decodes_signed_and_unsigned_values() {
    const std::vector<uint8_t> bytes{
        0xffU, 0xffU, 0xffU, 0xfeU,
        0x01U, 0x02U,
        0x03U};
    rmdb::wire::WireReader reader(bytes);

    expect_true(reader.get_i32() == -2, "INT32 must decode as two's complement");
    expect_true(reader.get_u16() == 0x0102U, "u16 must decode as big-endian");
    expect_true(reader.get_u8() == 0x03U, "u8 must decode without conversion");
    reader.require_consumed();
}

void test_reader_rejects_truncation() {
    const std::vector<uint8_t> bytes{0x00U, 0x01U, 0x02U};
    rmdb::wire::WireReader reader(bytes);
    expect_throws<rmdb::wire::ProtocolError>(
        [&] { static_cast<void>(reader.get_u32()); },
        "Reader must reject a truncated u32");
}

void test_reader_rejects_trailing_bytes() {
    const std::vector<uint8_t> bytes{0x00U, 0x01U};
    rmdb::wire::WireReader reader(bytes);
    static_cast<void>(reader.get_u8());
    expect_throws<rmdb::wire::ProtocolError>(
        [&] { reader.require_consumed(); },
        "Reader must reject unexplained trailing payload bytes");
}

void test_header_validation() {
    rmdb::wire::FrameHeader valid{
        16U,
        static_cast<uint8_t>(rmdb::wire::ClientTag::EXEC_STREAM),
        0U,
        0U};
    rmdb::wire::validate_common_header(valid);

    auto oversized = valid;
    oversized.payload_bytes = rmdb::wire::kMaxPayloadBytes + 1U;
    expect_throws<rmdb::wire::ProtocolError>(
        [&] { rmdb::wire::validate_common_header(oversized); },
        "Frame payloads over 1 MiB must be rejected");

    auto reserved = valid;
    reserved.reserved = 1U;
    expect_throws<rmdb::wire::ProtocolError>(
        [&] { rmdb::wire::validate_common_header(reserved); },
        "Non-zero reserved header fields must be rejected");
}

}  // namespace

int main() {
    test_header_is_big_endian();
    test_reader_decodes_signed_and_unsigned_values();
    test_reader_rejects_truncation();
    test_reader_rejects_trailing_bytes();
    test_header_validation();

    if (failures != 0) {
        std::cerr << failures << " wire codec test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "wire_codec_test: PASS\n";
    return EXIT_SUCCESS;
}
