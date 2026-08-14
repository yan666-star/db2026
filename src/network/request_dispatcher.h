#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "execution/execution_result.h"
#include "network/prepared_dictionary.h"
#include "network/wire_protocol.h"

namespace rmdb::wire {

class TransactionAbortError : public std::runtime_error {
 public:
    explicit TransactionAbortError(const std::string &message)
        : std::runtime_error(message) {}
};

class FrameEmissionError : public std::runtime_error {
 public:
    explicit FrameEmissionError(const std::string &message)
        : std::runtime_error(message) {}
};

class ExecutionService {
 public:
    virtual ~ExecutionService() = default;

    virtual PreparedArtifact prepare(const PrepareEntry &entry) = 0;
    virtual void execute_stream(const std::string &sql,
                                execution::ResultSink &sink) = 0;
    virtual void execute_prepared(
        const PreparedStatement &statement,
        const std::vector<execution::TypedValue> &parameters,
        execution::ResultSink &sink) = 0;
    virtual bool supports_prepared_batch(
        const PreparedStatement &statement) const {
        static_cast<void>(statement);
        return false;
    }
    virtual void execute_prepared_batch(
        const PreparedStatement &statement,
        const std::vector<std::vector<execution::TypedValue>> &parameter_rows,
        execution::ResultSink &sink) {
        static_cast<void>(statement);
        static_cast<void>(parameter_rows);
        static_cast<void>(sink);
        throw std::logic_error("prepared batching is unsupported");
    }
    virtual bool has_active_transaction() const = 0;
    virtual void abort_active_transaction() = 0;
    virtual void reset_after_auto_abort() {}
};

using FrameEmitter =
    std::function<void(const std::vector<uint8_t> &encoded_frame)>;

class RequestDispatcher {
 public:
    explicit RequestDispatcher(ExecutionService &execution_service);

    void dispatch(const FrameHeader &header,
                  const std::vector<uint8_t> &payload,
                  const FrameEmitter &emit);

    const PreparedDictionary &prepared_dictionary() const noexcept;

 private:
    void dispatch_stream(const std::vector<uint8_t> &payload,
                         const FrameEmitter &emit);
    void dispatch_prepare(const std::vector<uint8_t> &payload,
                          const FrameEmitter &emit);
    void dispatch_batch(const std::vector<uint8_t> &payload,
                        const FrameEmitter &emit);
    void abort_if_active();

    ExecutionService &execution_service_;
    PreparedDictionary prepared_dictionary_;
};

}  // namespace rmdb::wire
