#pragma once

#include "network/request_dispatcher.h"
#include "network/socket_io.h"

namespace rmdb::wire {

enum class SessionResult {
    PEER_CLOSED,
    UNSUPPORTED_HANDSHAKE,
    IO_ERROR,
    PROTOCOL_ERROR,
    INTERNAL_ERROR,
};

class ConnectionSession {
 public:
    explicit ConnectionSession(ExecutionService &execution_service);

    SessionResult run_with(const ReadSome &read_some,
                           const WriteSome &write_some);
    SessionResult run(int fd);

 private:
    RequestDispatcher dispatcher_;
};

}  // namespace rmdb::wire
