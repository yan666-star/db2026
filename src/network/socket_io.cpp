#include "network/socket_io.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>

#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rmdb::wire {

namespace {

IoResult transfer_exact(size_t length,
                        const std::function<IoAttempt(size_t, size_t)> &step) {
    size_t offset = 0;
    while (offset < length) {
        const IoAttempt attempt = step(offset, length - offset);
        switch (attempt.status) {
            case IoAttemptStatus::INTERRUPTED:
                continue;
            case IoAttemptStatus::EOF_REACHED:
                return IoResult::EOF_REACHED;
            case IoAttemptStatus::ERROR:
                return IoResult::ERROR;
            case IoAttemptStatus::PROGRESS:
                if (attempt.bytes == 0 || attempt.bytes > length - offset) {
                    return IoResult::ERROR;
                }
                offset += attempt.bytes;
                break;
        }
    }
    return IoResult::OK;
}

HandshakeResult map_handshake_io(IoResult result) {
    switch (result) {
        case IoResult::OK:
            return HandshakeResult::OK;
        case IoResult::EOF_REACHED:
            return HandshakeResult::EOF_REACHED;
        case IoResult::ERROR:
            return HandshakeResult::IO_ERROR;
    }
    return HandshakeResult::IO_ERROR;
}

}  // namespace

IoResult read_exact_with(const ReadSome &read_some, void *buffer,
                         size_t length) {
    if ((buffer == nullptr && length != 0) || !read_some) {
        return IoResult::ERROR;
    }
    auto *bytes = static_cast<uint8_t *>(buffer);
    return transfer_exact(
        length, [&](size_t offset, size_t remaining) {
            return read_some(bytes + offset, remaining);
        });
}

IoResult write_all_with(const WriteSome &write_some, const void *buffer,
                        size_t length) {
    if ((buffer == nullptr && length != 0) || !write_some) {
        return IoResult::ERROR;
    }
    const auto *bytes = static_cast<const uint8_t *>(buffer);
    return transfer_exact(
        length, [&](size_t offset, size_t remaining) {
            return write_some(bytes + offset, remaining);
        });
}

IoResult read_exact(int fd, void *buffer, size_t length) {
#ifdef _WIN32
    static_cast<void>(fd);
    static_cast<void>(buffer);
    static_cast<void>(length);
    return IoResult::ERROR;
#else
    return read_exact_with(
        [fd](uint8_t *destination, size_t capacity) {
            const ssize_t result =
                ::recv(fd, destination, capacity, 0);
            if (result > 0) {
                return IoAttempt{IoAttemptStatus::PROGRESS,
                                 static_cast<size_t>(result)};
            }
            if (result == 0) {
                return IoAttempt{IoAttemptStatus::EOF_REACHED, 0};
            }
            if (errno == EINTR) {
                return IoAttempt{IoAttemptStatus::INTERRUPTED, 0};
            }
            return IoAttempt{IoAttemptStatus::ERROR, 0};
        },
        buffer, length);
#endif
}

IoResult write_all(int fd, const void *buffer, size_t length) {
#ifdef _WIN32
    static_cast<void>(fd);
    static_cast<void>(buffer);
    static_cast<void>(length);
    return IoResult::ERROR;
#else
    return write_all_with(
        [fd](const uint8_t *source, size_t size) {
            const ssize_t result =
                ::send(fd, source, size, 0);
            if (result > 0) {
                return IoAttempt{IoAttemptStatus::PROGRESS,
                                 static_cast<size_t>(result)};
            }
            if (result == 0) {
                return IoAttempt{IoAttemptStatus::ERROR, 0};
            }
            if (errno == EINTR) {
                return IoAttempt{IoAttemptStatus::INTERRUPTED, 0};
            }
            return IoAttempt{IoAttemptStatus::ERROR, 0};
        },
        buffer, length);
#endif
}

HandshakeResult perform_server_handshake_with(const ReadSome &read_some,
                                              const WriteSome &write_some) {
    std::array<uint8_t, kHandshakeBytes> handshake{};
    const IoResult read_result =
        read_exact_with(read_some, handshake.data(), handshake.size());
    if (read_result != IoResult::OK) {
        return map_handshake_io(read_result);
    }
    if (handshake != kHandshakeV3) {
        return HandshakeResult::UNSUPPORTED;
    }
    const IoResult write_result =
        write_all_with(write_some, handshake.data(), handshake.size());
    return map_handshake_io(write_result);
}

HandshakeResult perform_server_handshake(int fd) {
#ifdef _WIN32
    static_cast<void>(fd);
    return HandshakeResult::IO_ERROR;
#else
    ReadSome read_some = [fd](uint8_t *destination, size_t capacity) {
        const ssize_t result = ::recv(fd, destination, capacity, 0);
        if (result > 0) {
            return IoAttempt{IoAttemptStatus::PROGRESS,
                             static_cast<size_t>(result)};
        }
        if (result == 0) {
            return IoAttempt{IoAttemptStatus::EOF_REACHED, 0};
        }
        return IoAttempt{errno == EINTR ? IoAttemptStatus::INTERRUPTED
                                        : IoAttemptStatus::ERROR,
                         0};
    };
    WriteSome write_some = [fd](const uint8_t *source, size_t size) {
        const ssize_t result = ::send(fd, source, size, 0);
        if (result > 0) {
            return IoAttempt{IoAttemptStatus::PROGRESS,
                             static_cast<size_t>(result)};
        }
        return IoAttempt{errno == EINTR ? IoAttemptStatus::INTERRUPTED
                                        : IoAttemptStatus::ERROR,
                         0};
    };
    return perform_server_handshake_with(read_some, write_some);
#endif
}

}  // namespace rmdb::wire
