#include "network/connection_session.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <exception>
#include <vector>

#ifndef _WIN32
#include <sys/socket.h>
#endif

#include "network/wire_codec.h"

namespace rmdb::wire {

ConnectionSession::ConnectionSession(ExecutionService &execution_service)
    : dispatcher_(execution_service) {}

SessionResult ConnectionSession::run_with(const ReadSome &read_some,
                                          const WriteSome &write_some) {
    const HandshakeResult handshake =
        perform_server_handshake_with(read_some, write_some);
    switch (handshake) {
        case HandshakeResult::UNSUPPORTED:
            return SessionResult::UNSUPPORTED_HANDSHAKE;
        case HandshakeResult::EOF_REACHED:
            return SessionResult::PEER_CLOSED;
        case HandshakeResult::IO_ERROR:
            return SessionResult::IO_ERROR;
        case HandshakeResult::OK:
            break;
    }

    while (true) {
        std::array<uint8_t, kFrameHeaderBytes> encoded_header{};
        const IoResult header_read = read_exact_with(
            read_some, encoded_header.data(), encoded_header.size());
        if (header_read == IoResult::EOF_REACHED) {
            return SessionResult::PEER_CLOSED;
        }
        if (header_read != IoResult::OK) {
            return SessionResult::IO_ERROR;
        }

        try {
            const FrameHeader header =
                decode_header(encoded_header.data(), encoded_header.size());
            validate_client_header(header);

            std::vector<uint8_t> payload(header.payload_bytes);
            const IoResult payload_read = read_exact_with(
                read_some, payload.data(), payload.size());
            if (payload_read != IoResult::OK) {
                return SessionResult::IO_ERROR;
            }

            dispatcher_.dispatch(
                header,
                payload,
                [&](const std::vector<uint8_t> &frame) {
                    if (write_all_with(
                            write_some, frame.data(), frame.size()) !=
                        IoResult::OK) {
                        throw FrameEmissionError(
                            "failed to write response frame");
                    }
                });
        } catch (const FrameEmissionError &) {
            return SessionResult::IO_ERROR;
        } catch (const ProtocolError &) {
            return SessionResult::PROTOCOL_ERROR;
        } catch (const std::exception &) {
            return SessionResult::INTERNAL_ERROR;
        }
    }
}

SessionResult ConnectionSession::run(int fd) {
#ifdef _WIN32
    static_cast<void>(fd);
    return SessionResult::IO_ERROR;
#else
    const ReadSome read_some = [fd](uint8_t *destination, size_t capacity) {
        const ssize_t result = ::recv(fd, destination, capacity, 0);
        if (result > 0) {
            return IoAttempt{
                IoAttemptStatus::PROGRESS, static_cast<size_t>(result)};
        }
        if (result == 0) {
            return IoAttempt{IoAttemptStatus::EOF_REACHED, 0};
        }
        return IoAttempt{
            errno == EINTR ? IoAttemptStatus::INTERRUPTED
                           : IoAttemptStatus::ERROR,
            0};
    };
    const WriteSome write_some = [fd](const uint8_t *source, size_t size) {
        const ssize_t result = ::send(fd, source, size, 0);
        if (result > 0) {
            return IoAttempt{
                IoAttemptStatus::PROGRESS, static_cast<size_t>(result)};
        }
        return IoAttempt{
            errno == EINTR ? IoAttemptStatus::INTERRUPTED
                           : IoAttemptStatus::ERROR,
            0};
    };
    return run_with(read_some, write_some);
#endif
}

}  // namespace rmdb::wire
