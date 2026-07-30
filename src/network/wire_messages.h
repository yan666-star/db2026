#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "execution/execution_result.h"
#include "network/prepared_dictionary.h"

namespace rmdb::wire {

struct BatchOperation {
    const PreparedStatement *statement;
    std::vector<execution::TypedValue> parameters;
};

struct BatchRequest {
    std::vector<BatchOperation> operations;
};

struct BatchQueryResult {
    uint16_t operation_index;
    std::vector<std::vector<execution::TypedValue>> rows;
};

struct BatchResult {
    uint16_t executed_operations{0};
    BatchStatus status{BatchStatus::OK};
    uint16_t failed_operation{0xffffU};
    std::string diagnostic;
    std::vector<BatchQueryResult> results;
};

std::string decode_exec_stream(const std::vector<uint8_t> &payload);
BatchRequest decode_exec_batch(const std::vector<uint8_t> &payload,
                               const PreparedDictionary &dictionary);

std::vector<uint8_t> make_meta_frame(
    const std::vector<execution::OutputColumn> &schema);
std::vector<uint8_t> make_row_frame(
    const std::vector<execution::OutputColumn> &schema,
    const std::vector<execution::TypedValue> &row);
std::vector<uint8_t> make_result_end_frame(uint64_t row_count);
std::vector<uint8_t> make_command_ok_frame();
std::vector<uint8_t> make_transaction_abort_frame(
    const std::string &diagnostic);
std::vector<uint8_t> make_error_frame(const std::string &diagnostic);
std::vector<uint8_t> make_prepare_ok_frame(
    const PreparedDictionary &dictionary);
std::vector<uint8_t> make_batch_result_frame(
    const BatchResult &result, const BatchRequest &request);

}  // namespace rmdb::wire
