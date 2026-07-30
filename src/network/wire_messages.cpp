#include "network/wire_messages.h"

#include <limits>
#include <unordered_set>

#include "network/wire_codec.h"

namespace rmdb::wire {
namespace {

execution::TypedValue decode_cell(WireReader &reader, SqlType type) {
    const uint8_t present = reader.get_u8();
    if (present == 0U) {
        return execution::TypedValue::Null(type);
    }
    if (present != 1U) {
        throw ProtocolError("cell present flag must be zero or one");
    }

    switch (type) {
        case SqlType::INT32:
            return execution::TypedValue::Int32(reader.get_i32());
        case SqlType::FLOAT32:
            return execution::TypedValue::FloatBits(reader.get_u32());
        case SqlType::CHAR: {
            const uint32_t byte_count = reader.get_u32();
            return execution::TypedValue::Char(
                reader.get_string(byte_count));
        }
    }
    throw ProtocolError("unknown cell SQL type");
}

void encode_cell(WireWriter &writer,
                 const execution::TypedValue &value,
                 SqlType expected_type) {
    if (value.type() != expected_type) {
        throw ProtocolError("cell type does not match its schema");
    }
    writer.put_u8(value.present() ? 1U : 0U);
    if (!value.present()) {
        return;
    }

    switch (expected_type) {
        case SqlType::INT32:
            writer.put_i32(value.int32_value());
            return;
        case SqlType::FLOAT32:
            writer.put_u32(value.float_bits());
            return;
        case SqlType::CHAR:
            writer.put_string_u32(value.char_value());
            return;
    }
    throw ProtocolError("unknown cell SQL type");
}

void encode_column(WireWriter &writer,
                   const execution::OutputColumn &column) {
    if (column.name.empty()) {
        throw ProtocolError("result column name must not be empty");
    }
    require_valid_utf8(column.name, "result column name");
    if (column.name.size() >
        static_cast<size_t>(std::numeric_limits<uint16_t>::max())) {
        throw ProtocolError("result column name exceeds u16");
    }
    writer.put_u16(static_cast<uint16_t>(column.name.size()));
    writer.put_string(column.name);
    writer.put_u8(static_cast<uint8_t>(column.type));
}

void encode_row(WireWriter &writer,
                const std::vector<execution::OutputColumn> &schema,
                const std::vector<execution::TypedValue> &row) {
    if (row.size() != schema.size()) {
        throw ProtocolError("result row width does not match its schema");
    }
    for (size_t index = 0; index < schema.size(); ++index) {
        encode_cell(writer, row[index], schema[index].type);
    }
}

std::vector<uint8_t> make_diagnostic_frame(ServerTag tag,
                                           const std::string &diagnostic) {
    if (diagnostic.size() > kMaxDiagnosticBytes) {
        throw ProtocolError("diagnostic exceeds 64 KiB");
    }
    require_valid_utf8(diagnostic, "diagnostic");
    WireWriter payload;
    payload.put_string(diagnostic);
    return encode_frame(static_cast<uint8_t>(tag), 0, payload.bytes());
}

void validate_success_results(const BatchResult &result,
                              const BatchRequest &request) {
    if (result.executed_operations != request.operations.size() ||
        result.failed_operation != 0xffffU ||
        !result.diagnostic.empty()) {
        throw ProtocolError("invalid successful BATCH_RESULT counters");
    }

    std::vector<uint16_t> expected_queries;
    for (size_t index = 0; index < request.operations.size(); ++index) {
        if (request.operations[index].statement->result_kind ==
            ResultKind::QUERY) {
            expected_queries.push_back(static_cast<uint16_t>(index));
        }
    }
    if (result.results.size() != expected_queries.size()) {
        throw ProtocolError(
            "successful batch must contain every query result exactly once");
    }
    for (size_t index = 0; index < result.results.size(); ++index) {
        if (result.results[index].operation_index != expected_queries[index]) {
            throw ProtocolError(
                "batch query results must be strictly ordered by operation");
        }
    }
}

void validate_failed_result(const BatchResult &result,
                            const BatchRequest &request) {
    if (result.executed_operations >= request.operations.size() ||
        result.failed_operation != result.executed_operations ||
        !result.results.empty()) {
        throw ProtocolError("invalid failed BATCH_RESULT counters or rows");
    }
}

}  // namespace

std::string decode_exec_stream(const std::vector<uint8_t> &payload) {
    if (payload.empty()) {
        throw ProtocolError("EXEC_STREAM SQL must not be empty");
    }
    const std::string sql(
        reinterpret_cast<const char *>(payload.data()), payload.size());
    require_valid_utf8(sql, "EXEC_STREAM SQL");
    return sql;
}

BatchRequest decode_exec_batch(const std::vector<uint8_t> &payload,
                               const PreparedDictionary &dictionary) {
    WireReader reader(payload);
    const uint16_t operation_count = reader.get_u16();
    if (operation_count == 0U || operation_count > 256U) {
        throw ProtocolError("EXEC_BATCH operation count must be 1..=256");
    }

    BatchRequest request;
    request.operations.reserve(operation_count);
    for (uint16_t index = 0; index < operation_count; ++index) {
        const uint16_t statement_id = reader.get_u16();
        const PreparedStatement *statement = dictionary.find(statement_id);
        if (statement == nullptr) {
            throw ProtocolError("EXEC_BATCH references unknown statement id");
        }

        BatchOperation operation;
        operation.statement = statement;
        operation.parameters.reserve(statement->parameter_types.size());
        for (const SqlType type : statement->parameter_types) {
            operation.parameters.push_back(decode_cell(reader, type));
        }
        request.operations.push_back(std::move(operation));
    }
    reader.require_consumed();
    return request;
}

std::vector<uint8_t> make_meta_frame(
    const std::vector<execution::OutputColumn> &schema) {
    if (schema.empty() ||
        schema.size() >
            static_cast<size_t>(std::numeric_limits<uint16_t>::max())) {
        throw ProtocolError("META column count must be 1..=65535");
    }
    WireWriter payload;
    payload.put_u16(static_cast<uint16_t>(schema.size()));
    for (const auto &column : schema) {
        encode_column(payload, column);
    }
    return encode_frame(
        static_cast<uint8_t>(ServerTag::META), 0, payload.bytes());
}

std::vector<uint8_t> make_row_frame(
    const std::vector<execution::OutputColumn> &schema,
    const std::vector<execution::TypedValue> &row) {
    if (schema.empty()) {
        throw ProtocolError("ROW requires a non-empty schema");
    }
    WireWriter payload;
    encode_row(payload, schema, row);
    return encode_frame(
        static_cast<uint8_t>(ServerTag::ROW), 0, payload.bytes());
}

std::vector<uint8_t> make_result_end_frame(uint64_t row_count) {
    WireWriter payload;
    payload.put_u64(row_count);
    return encode_frame(
        static_cast<uint8_t>(ServerTag::RESULT_END), 0, payload.bytes());
}

std::vector<uint8_t> make_command_ok_frame() {
    return encode_frame(
        static_cast<uint8_t>(ServerTag::COMMAND_OK), 0, {});
}

std::vector<uint8_t> make_transaction_abort_frame(
    const std::string &diagnostic) {
    return make_diagnostic_frame(ServerTag::TRANSACTION_ABORT, diagnostic);
}

std::vector<uint8_t> make_error_frame(const std::string &diagnostic) {
    return make_diagnostic_frame(ServerTag::ERROR, diagnostic);
}

std::vector<uint8_t> make_prepare_ok_frame(
    const PreparedDictionary &dictionary) {
    if (dictionary.size() == 0U || dictionary.size() > 256U) {
        throw ProtocolError(
            "PREPARE_OK requires an installed 1..=256 entry dictionary");
    }

    WireWriter payload;
    payload.put_u16(static_cast<uint16_t>(dictionary.size()));
    for (const auto &statement : dictionary.in_request_order()) {
        payload.put_u16(statement.statement_id);
        if (statement.output_schema.size() >
            static_cast<size_t>(std::numeric_limits<uint16_t>::max())) {
            throw ProtocolError("prepared result schema exceeds u16");
        }
        payload.put_u16(
            static_cast<uint16_t>(statement.output_schema.size()));
        for (const auto &column : statement.output_schema) {
            encode_column(payload, column);
        }
    }
    return encode_frame(
        static_cast<uint8_t>(ServerTag::PREPARE_OK), 0, payload.bytes());
}

std::vector<uint8_t> make_batch_result_frame(
    const BatchResult &result, const BatchRequest &request) {
    if (request.operations.empty() || request.operations.size() > 256U) {
        throw ProtocolError("BATCH_RESULT request size must be 1..=256");
    }
    if (result.diagnostic.size() > kMaxDiagnosticBytes) {
        throw ProtocolError("batch diagnostic exceeds 64 KiB");
    }
    require_valid_utf8(result.diagnostic, "batch diagnostic");

    if (result.status == BatchStatus::OK) {
        validate_success_results(result, request);
    } else if (result.status == BatchStatus::TRANSACTION_ABORT ||
               result.status == BatchStatus::ERROR) {
        validate_failed_result(result, request);
    } else {
        throw ProtocolError("unknown BATCH_RESULT status");
    }

    WireWriter payload;
    payload.put_u16(result.executed_operations);
    payload.put_u8(static_cast<uint8_t>(result.status));
    payload.put_u16(result.failed_operation);
    payload.put_string_u32(result.diagnostic);
    payload.put_u16(static_cast<uint16_t>(result.results.size()));

    for (const auto &query_result : result.results) {
        const auto &operation =
            request.operations[query_result.operation_index];
        const auto &schema = operation.statement->output_schema;
        if (query_result.rows.size() >
            static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
            throw ProtocolError("batch query row count exceeds u32");
        }
        payload.put_u16(query_result.operation_index);
        payload.put_u32(static_cast<uint32_t>(query_result.rows.size()));
        for (const auto &row : query_result.rows) {
            encode_row(payload, schema, row);
        }
    }
    return encode_frame(
        static_cast<uint8_t>(ServerTag::BATCH_RESULT), 0, payload.bytes());
}

}  // namespace rmdb::wire
