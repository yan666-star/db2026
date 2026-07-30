#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "network/wire_protocol.h"

namespace rmdb::wire {

enum class IoAttemptStatus {
    PROGRESS,
    INTERRUPTED,
    EOF_REACHED,
    ERROR,
};

struct IoAttempt {
    IoAttemptStatus status;
    size_t bytes;
};

enum class IoResult {
    OK,
    EOF_REACHED,
    ERROR,
};

enum class HandshakeResult {
    OK,
    UNSUPPORTED,
    EOF_REACHED,
    IO_ERROR,
};

using ReadSome = std::function<IoAttempt(uint8_t *, size_t)>;
using WriteSome = std::function<IoAttempt(const uint8_t *, size_t)>;

IoResult read_exact_with(const ReadSome &read_some, void *buffer,
                         size_t length);
IoResult write_all_with(const WriteSome &write_some, const void *buffer,
                        size_t length);

IoResult read_exact(int fd, void *buffer, size_t length);
IoResult write_all(int fd, const void *buffer, size_t length);

HandshakeResult perform_server_handshake_with(const ReadSome &read_some,
                                              const WriteSome &write_some);
HandshakeResult perform_server_handshake(int fd);

}  // namespace rmdb::wire
