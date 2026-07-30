#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace rmdb::wire {

constexpr uint32_t kMaxPayloadBytes = 1024U * 1024U;
constexpr uint32_t kMaxDiagnosticBytes = 64U * 1024U;
constexpr size_t kHandshakeBytes = 8U;
constexpr size_t kFrameHeaderBytes = 8U;

constexpr std::array<uint8_t, kHandshakeBytes> kHandshakeV3{
    0x52U, 0x4dU, 0x44U, 0x42U,  // "RMDB"
    0x00U, 0x03U,                // major = 3
    0x00U, 0x00U                 // minor = 0
};

enum class ClientTag : uint8_t {
    EXEC_STREAM = 0x20,
    PREPARE_SET = 0x21,
    EXEC_BATCH = 0x22,
};

enum class ServerTag : uint8_t {
    META = 0x01,
    ROW = 0x02,
    COMMAND_OK = 0x10,
    RESULT_END = 0x11,
    TRANSACTION_ABORT = 0x12,
    ERROR = 0x13,
    PREPARE_OK = 0x14,
    BATCH_RESULT = 0x15,
};

enum class SqlType : uint8_t {
    INT32 = 0x01,
    FLOAT32 = 0x02,
    CHAR = 0x03,
};

enum class BatchStatus : uint8_t {
    OK = 0,
    TRANSACTION_ABORT = 1,
    ERROR = 2,
};

constexpr uint8_t kExecBatchAutoAbort = 0x01U;

class ProtocolError : public std::runtime_error {
 public:
    explicit ProtocolError(const std::string &message)
        : std::runtime_error(message) {}
};

struct FrameHeader {
    uint32_t payload_bytes{0};
    uint8_t tag{0};
    uint8_t flags{0};
    uint16_t reserved{0};
};

}  // namespace rmdb::wire
