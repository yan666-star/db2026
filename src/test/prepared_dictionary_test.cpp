#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "network/prepared_dictionary.h"
#include "network/wire_codec.h"

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

rmdb::wire::PrepareEntry make_entry(
    uint16_t id,
    rmdb::wire::ResultKind result_kind,
    std::vector<rmdb::wire::SqlType> parameter_types,
    std::string sql) {
    return {id, result_kind, std::move(parameter_types), std::move(sql)};
}

void test_marker_scan_is_dense_and_ignores_quoted_text() {
    const auto ordinals = rmdb::wire::scan_parameter_markers(
        "SELECT '$9', c FROM t WHERE a=$2 AND b=$1 OR a=$2;");
    expect_true(ordinals == std::vector<uint16_t>({1, 2}),
                "Marker scan must return the dense unique ordinal set");

    rmdb::wire::validate_parameter_markers(
        "SELECT '$9', c FROM t WHERE a=$2 AND b=$1 OR a=$2;", 2);
    expect_protocol_error(
        [] {
            rmdb::wire::validate_parameter_markers(
                "SELECT c FROM t WHERE a=$1 AND b=$3;", 3);
        },
        "A sparse marker set must be rejected");
    expect_protocol_error(
        [] {
            rmdb::wire::validate_parameter_markers(
                "SELECT c FROM t WHERE a=$0;", 1);
        },
        "Marker ordinal zero must be rejected");
}

void test_prepare_payload_decode_is_exact() {
    rmdb::wire::WireWriter payload;
    payload.put_u16(2);

    payload.put_u16(7);
    payload.put_u8(static_cast<uint8_t>(rmdb::wire::ResultKind::QUERY));
    payload.put_u16(2);
    payload.put_u8(static_cast<uint8_t>(rmdb::wire::SqlType::INT32));
    payload.put_u8(static_cast<uint8_t>(rmdb::wire::SqlType::CHAR));
    payload.put_string_u32(
        "SELECT c FROM t WHERE a=$1 AND name=$2;");

    payload.put_u16(8);
    payload.put_u8(static_cast<uint8_t>(rmdb::wire::ResultKind::COMMAND));
    payload.put_u16(0);
    payload.put_string_u32("COMMIT;");

    const auto request = rmdb::wire::decode_prepare_set(payload.bytes());
    expect_true(request.size() == 2,
                "PREPARE_SET must decode every request entry");
    expect_true(request[0].statement_id == 7 &&
                    request[0].parameter_types.size() == 2 &&
                    request[0].sql.find("$2") != std::string::npos,
                "Decoded query entry must retain id, types and SQL");
    expect_true(request[1].result_kind == rmdb::wire::ResultKind::COMMAND,
                "Decoded command entry must retain result kind");

    auto trailing = payload.bytes();
    trailing.push_back(0xff);
    expect_protocol_error(
        [&] { (void)rmdb::wire::decode_prepare_set(trailing); },
        "PREPARE_SET trailing bytes must be rejected");

    rmdb::wire::WireWriter invalid_utf8;
    invalid_utf8.put_u16(1);
    invalid_utf8.put_u16(9);
    invalid_utf8.put_u8(
        static_cast<uint8_t>(rmdb::wire::ResultKind::COMMAND));
    invalid_utf8.put_u16(0);
    invalid_utf8.put_u32(1);
    invalid_utf8.put_u8(0xff);
    expect_protocol_error(
        [&] { (void)rmdb::wire::decode_prepare_set(invalid_utf8.bytes()); },
        "PREPARE_SET SQL must be valid UTF-8");
}

void test_failed_install_keeps_old_dictionary() {
    rmdb::wire::PreparedDictionary dictionary;
    dictionary.install_atomically(
        {make_entry(1, rmdb::wire::ResultKind::COMMAND, {}, "BEGIN;")},
        [](const rmdb::wire::PrepareEntry &) {
            return std::vector<rmdb::execution::OutputColumn>{};
        });

    expect_protocol_error(
        [&] {
            dictionary.install_atomically(
                {
                    make_entry(
                        2, rmdb::wire::ResultKind::COMMAND, {}, "COMMIT;"),
                    make_entry(
                        2, rmdb::wire::ResultKind::COMMAND, {}, "ABORT;"),
                },
                [](const rmdb::wire::PrepareEntry &) {
                    return std::vector<rmdb::execution::OutputColumn>{};
                });
        },
        "Duplicate statement ids must reject the complete replacement");

    expect_true(dictionary.find(1) != nullptr,
                "Failed replacement must preserve the old dictionary");
    expect_true(dictionary.find(2) == nullptr,
                "Failed replacement must not leak a partial new dictionary");
}

void test_query_schema_must_match_result_kind() {
    rmdb::wire::PreparedDictionary dictionary;
    expect_protocol_error(
        [&] {
            dictionary.install_atomically(
                {make_entry(
                    3, rmdb::wire::ResultKind::QUERY, {}, "SELECT a FROM t;")},
                [](const rmdb::wire::PrepareEntry &) {
                    return std::vector<rmdb::execution::OutputColumn>{};
                });
        },
        "A query must expose at least one prepared result column");

    expect_protocol_error(
        [&] {
            dictionary.install_atomically(
                {make_entry(
                    4, rmdb::wire::ResultKind::COMMAND, {}, "COMMIT;")},
                [](const rmdb::wire::PrepareEntry &) {
                    return std::vector<rmdb::execution::OutputColumn>{
                        {"unexpected", rmdb::wire::SqlType::INT32}};
                });
        },
        "A command must expose zero prepared result columns");
}

}  // namespace

int main() {
    test_marker_scan_is_dense_and_ignores_quoted_text();
    test_prepare_payload_decode_is_exact();
    test_failed_install_keeps_old_dictionary();
    test_query_schema_must_match_result_kind();

    if (failures != 0) {
        std::cerr << failures << " prepared dictionary test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "prepared_dictionary_test: PASS\n";
    return EXIT_SUCCESS;
}
