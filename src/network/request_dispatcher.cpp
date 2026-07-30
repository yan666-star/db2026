#include "network/request_dispatcher.h"

#include <cstdint>
#include <exception>
#include <utility>

#include "network/wire_codec.h"
#include "network/wire_messages.h"

namespace rmdb::wire {
namespace {

std::string safe_diagnostic(const char *message) {
    if (message == nullptr) {
        return "execution failure";
    }
    std::string diagnostic(message);
    if (diagnostic.size() > kMaxDiagnosticBytes ||
        !is_valid_utf8(diagnostic)) {
        return "execution failure";
    }
    return diagnostic;
}

bool same_schema(const std::vector<execution::OutputColumn> &left,
                 const std::vector<execution::OutputColumn> &right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t index = 0; index < left.size(); ++index) {
        if (left[index].name != right[index].name ||
            left[index].type != right[index].type) {
            return false;
        }
    }
    return true;
}

size_t encoded_cell_bytes(const execution::TypedValue &value) {
    size_t bytes = 1U;
    if (!value.present()) {
        return bytes;
    }
    switch (value.type()) {
        case SqlType::INT32:
        case SqlType::FLOAT32:
            return bytes + sizeof(uint32_t);
        case SqlType::CHAR:
            return bytes + sizeof(uint32_t) + value.char_value().size();
    }
    throw ProtocolError("unknown typed result value");
}

class StreamResultSink final : public execution::ResultSink {
 public:
    explicit StreamResultSink(FrameEmitter emit) : emit_(std::move(emit)) {}

    void begin_query(
        const std::vector<execution::OutputColumn> &schema) override {
        require_idle();
        schema_ = schema;
        query_started_ = true;
        emit_(make_meta_frame(schema_));
    }

    void push_row(
        const std::vector<execution::TypedValue> &row) override {
        if (!query_started_ || terminal_) {
            throw ProtocolError("query row outside an active query result");
        }
        emit_(make_row_frame(schema_, row));
        row_count_++;
    }

    void end_query(uint64_t row_count) override {
        if (!query_started_ || terminal_) {
            throw ProtocolError("query end outside an active query result");
        }
        if (row_count != row_count_) {
            throw ProtocolError("executor query row count mismatch");
        }
        terminal_ = true;
        emit_(make_result_end_frame(row_count));
    }

    void command_ok() override {
        require_idle();
        terminal_ = true;
        emit_(make_command_ok_frame());
    }

    void require_complete() const {
        if (!terminal_) {
            throw ProtocolError("execution produced no terminal result");
        }
    }

 private:
    void require_idle() const {
        if (query_started_ || terminal_) {
            throw ProtocolError("execution produced multiple result starts");
        }
    }

    FrameEmitter emit_;
    std::vector<execution::OutputColumn> schema_;
    uint64_t row_count_{0};
    bool query_started_{false};
    bool terminal_{false};
};

class BatchResultSink final : public execution::ResultSink {
 public:
    explicit BatchResultSink(const PreparedStatement &statement)
        : statement_(statement) {}

    void begin_query(
        const std::vector<execution::OutputColumn> &schema) override {
        require_idle();
        if (statement_.result_kind != ResultKind::QUERY ||
            !same_schema(schema, statement_.output_schema)) {
            throw ProtocolError(
                "prepared execution schema differs from PREPARE_OK");
        }
        query_started_ = true;
    }

    void push_row(
        const std::vector<execution::TypedValue> &row) override {
        if (!query_started_ || terminal_) {
            throw ProtocolError("prepared row outside an active query");
        }
        if (row.size() != statement_.output_schema.size()) {
            throw ProtocolError("prepared row width differs from schema");
        }
        size_t row_bytes = 0;
        for (size_t index = 0; index < row.size(); ++index) {
            if (row[index].type() !=
                statement_.output_schema[index].type) {
                throw ProtocolError(
                    "prepared row type differs from schema");
            }
            const size_t cell_bytes = encoded_cell_bytes(row[index]);
            if (cell_bytes > kMaxPayloadBytes - row_bytes) {
                throw ProtocolError(
                    "prepared query result exceeds frame limit");
            }
            row_bytes += cell_bytes;
        }
        if (row_bytes > kMaxPayloadBytes - encoded_rows_bytes_) {
            throw ProtocolError(
                "prepared query result exceeds frame limit");
        }
        encoded_rows_bytes_ += row_bytes;
        rows_.push_back(row);
    }

    void end_query(uint64_t row_count) override {
        if (!query_started_ || terminal_) {
            throw ProtocolError("prepared query ended in an invalid state");
        }
        if (row_count != rows_.size()) {
            throw ProtocolError("prepared query row count mismatch");
        }
        terminal_ = true;
    }

    void command_ok() override {
        require_idle();
        if (statement_.result_kind != ResultKind::COMMAND) {
            throw ProtocolError(
                "prepared query returned command completion");
        }
        terminal_ = true;
    }

    void require_complete() const {
        if (!terminal_) {
            throw ProtocolError(
                "prepared execution produced no terminal result");
        }
    }

    std::vector<std::vector<execution::TypedValue>> take_rows() {
        return std::move(rows_);
    }

    size_t encoded_rows_bytes() const noexcept {
        return encoded_rows_bytes_;
    }

 private:
    void require_idle() const {
        if (query_started_ || terminal_) {
            throw ProtocolError(
                "prepared execution produced multiple result starts");
        }
    }

    const PreparedStatement &statement_;
    std::vector<std::vector<execution::TypedValue>> rows_;
    size_t encoded_rows_bytes_{0};
    bool query_started_{false};
    bool terminal_{false};
};

}  // namespace

RequestDispatcher::RequestDispatcher(ExecutionService &execution_service)
    : execution_service_(execution_service) {}

void RequestDispatcher::dispatch(const FrameHeader &header,
                                 const std::vector<uint8_t> &payload,
                                 const FrameEmitter &emit) {
    if (!emit) {
        throw ProtocolError("response frame emitter is missing");
    }
    validate_client_header(header);
    if (header.payload_bytes != payload.size()) {
        throw ProtocolError("frame header payload length mismatch");
    }

    switch (static_cast<ClientTag>(header.tag)) {
        case ClientTag::EXEC_STREAM:
            dispatch_stream(payload, emit);
            return;
        case ClientTag::PREPARE_SET:
            dispatch_prepare(payload, emit);
            return;
        case ClientTag::EXEC_BATCH:
            dispatch_batch(payload, emit);
            return;
    }
    throw ProtocolError("unknown client frame tag");
}

const PreparedDictionary &
RequestDispatcher::prepared_dictionary() const noexcept {
    return prepared_dictionary_;
}

void RequestDispatcher::abort_if_active() {
    if (execution_service_.has_active_transaction()) {
        execution_service_.abort_active_transaction();
    }
}

void RequestDispatcher::dispatch_stream(
    const std::vector<uint8_t> &payload, const FrameEmitter &emit) {
    try {
        StreamResultSink sink(emit);
        execution_service_.execute_stream(decode_exec_stream(payload), sink);
        sink.require_complete();
    } catch (const FrameEmissionError &) {
        throw;
    } catch (const TransactionAbortError &error) {
        abort_if_active();
        emit(make_transaction_abort_frame(safe_diagnostic(error.what())));
    } catch (const std::exception &error) {
        emit(make_error_frame(safe_diagnostic(error.what())));
    }
}

void RequestDispatcher::dispatch_prepare(
    const std::vector<uint8_t> &payload, const FrameEmitter &emit) {
    try {
        const auto entries = decode_prepare_set(payload);
        prepared_dictionary_.install_atomically(
            entries,
            [this](const PrepareEntry &entry) {
                return execution_service_.prepare(entry);
            });
        emit(make_prepare_ok_frame(prepared_dictionary_));
    } catch (const FrameEmissionError &) {
        throw;
    } catch (const std::exception &error) {
        emit(make_error_frame(safe_diagnostic(error.what())));
    }
}

void RequestDispatcher::dispatch_batch(
    const std::vector<uint8_t> &payload, const FrameEmitter &emit) {
    BatchRequest request;
    try {
        request = decode_exec_batch(payload, prepared_dictionary_);
    } catch (const std::exception &error) {
        abort_if_active();
        emit(make_error_frame(safe_diagnostic(error.what())));
        return;
    }

    BatchResult result;
    // BATCH_RESULT fixed fields excluding diagnostic bytes are 11 bytes.
    size_t encoded_response_bytes = 11U;
    for (size_t index = 0; index < request.operations.size(); ++index) {
        const auto &operation = request.operations[index];
        try {
            BatchResultSink sink(*operation.statement);
            execution_service_.execute_prepared(
                *operation.statement, operation.parameters, sink);
            sink.require_complete();
            if (operation.statement->result_kind == ResultKind::QUERY) {
                constexpr size_t kQueryHeaderBytes =
                    sizeof(uint16_t) + sizeof(uint32_t);
                const size_t query_bytes =
                    kQueryHeaderBytes + sink.encoded_rows_bytes();
                if (query_bytes >
                    kMaxPayloadBytes - encoded_response_bytes) {
                    throw ProtocolError(
                        "BATCH_RESULT exceeds 1 MiB frame limit");
                }
                encoded_response_bytes += query_bytes;
                result.results.push_back(
                    {static_cast<uint16_t>(index), sink.take_rows()});
            }
            result.executed_operations++;
        } catch (const TransactionAbortError &error) {
            abort_if_active();
            result.status = BatchStatus::TRANSACTION_ABORT;
            result.failed_operation = result.executed_operations;
            result.diagnostic = safe_diagnostic(error.what());
            result.results.clear();
            emit(make_batch_result_frame(result, request));
            return;
        } catch (const std::exception &error) {
            abort_if_active();
            result.status = BatchStatus::ERROR;
            result.failed_operation = result.executed_operations;
            result.diagnostic = safe_diagnostic(error.what());
            result.results.clear();
            emit(make_batch_result_frame(result, request));
            return;
        }
    }

    result.status = BatchStatus::OK;
    result.failed_operation = 0xffffU;
    emit(make_batch_result_frame(result, request));
}

}  // namespace rmdb::wire
