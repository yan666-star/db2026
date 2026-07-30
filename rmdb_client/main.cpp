#include <netdb.h>
#include <netinet/in.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "network/socket_io.h"
#include "network/wire_codec.h"

namespace {

constexpr int kDefaultPort = 8765;

struct Column {
    std::string name;
    rmdb::wire::SqlType type;
};

bool valid_sql_type(rmdb::wire::SqlType type) {
    switch (type) {
        case rmdb::wire::SqlType::INT32:
        case rmdb::wire::SqlType::FLOAT32:
        case rmdb::wire::SqlType::CHAR:
            return true;
    }
    return false;
}

bool is_exit_command(const std::string &command) {
    return command == "exit" || command == "exit;" ||
           command == "bye" || command == "bye;";
}

int connect_unix(const char *path) {
    const int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_un address{};
    address.sun_family = PF_UNIX;
    std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
    if (connect(
            fd, reinterpret_cast<sockaddr *>(&address),
            sizeof(address)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int connect_tcp(const char *host_name, int port) {
    hostent *host = gethostbyname(host_name);
    if (host == nullptr) {
        return -1;
    }
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr = *reinterpret_cast<in_addr *>(host->h_addr);
    if (connect(
            fd, reinterpret_cast<sockaddr *>(&address),
            sizeof(address)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool handshake(int fd) {
    if (rmdb::wire::write_all(
            fd, rmdb::wire::kHandshakeV3.data(),
            rmdb::wire::kHandshakeV3.size()) != rmdb::wire::IoResult::OK) {
        return false;
    }
    std::vector<uint8_t> response(rmdb::wire::kHandshakeBytes);
    return rmdb::wire::read_exact(
               fd, response.data(), response.size()) ==
               rmdb::wire::IoResult::OK &&
           std::equal(
               response.begin(), response.end(),
               rmdb::wire::kHandshakeV3.begin());
}

std::vector<uint8_t> read_payload(
    int fd, const rmdb::wire::FrameHeader &header) {
    std::vector<uint8_t> payload(header.payload_bytes);
    if (rmdb::wire::read_exact(
            fd, payload.data(), payload.size()) !=
        rmdb::wire::IoResult::OK) {
        throw std::runtime_error("connection closed inside a frame");
    }
    return payload;
}

rmdb::wire::FrameHeader read_header(int fd) {
    std::vector<uint8_t> encoded(rmdb::wire::kFrameHeaderBytes);
    if (rmdb::wire::read_exact(
            fd, encoded.data(), encoded.size()) !=
        rmdb::wire::IoResult::OK) {
        throw std::runtime_error("connection closed before response");
    }
    const auto header =
        rmdb::wire::decode_header(encoded.data(), encoded.size());
    if (header.flags != 0 || header.reserved != 0) {
        throw rmdb::wire::ProtocolError(
            "server response flags/reserved must be zero");
    }
    return header;
}

std::string decode_cell(
    rmdb::wire::WireReader &reader, rmdb::wire::SqlType type) {
    const uint8_t present = reader.get_u8();
    if (present == 0) {
        return "NULL";
    }
    if (present != 1) {
        throw rmdb::wire::ProtocolError("invalid cell present flag");
    }
    switch (type) {
        case rmdb::wire::SqlType::INT32:
            return std::to_string(reader.get_i32());
        case rmdb::wire::SqlType::FLOAT32: {
            const uint32_t bits = reader.get_u32();
            float value = 0.0F;
            std::memcpy(&value, &bits, sizeof(value));
            return std::to_string(value);
        }
        case rmdb::wire::SqlType::CHAR: {
            const uint32_t bytes = reader.get_u32();
            return reader.get_string(bytes);
        }
    }
    throw rmdb::wire::ProtocolError("unknown result SQL type");
}

void execute_stream(int fd, const std::string &sql) {
    const std::vector<uint8_t> payload(sql.begin(), sql.end());
    const auto request = rmdb::wire::encode_frame(
        static_cast<uint8_t>(rmdb::wire::ClientTag::EXEC_STREAM),
        0,
        payload);
    if (rmdb::wire::write_all(fd, request.data(), request.size()) !=
        rmdb::wire::IoResult::OK) {
        throw std::runtime_error("failed to send EXEC_STREAM");
    }

    std::vector<Column> schema;
    uint64_t rows = 0;
    while (true) {
        const auto header = read_header(fd);
        const auto response = read_payload(fd, header);
        rmdb::wire::WireReader reader(response);
        switch (static_cast<rmdb::wire::ServerTag>(header.tag)) {
            case rmdb::wire::ServerTag::META: {
                if (!schema.empty()) {
                    throw rmdb::wire::ProtocolError("duplicate META");
                }
                const uint16_t column_count = reader.get_u16();
                if (column_count == 0) {
                    throw rmdb::wire::ProtocolError("empty META");
                }
                schema.reserve(column_count);
                for (uint16_t index = 0; index < column_count; ++index) {
                    const uint16_t name_bytes = reader.get_u16();
                    const std::string name =
                        reader.get_string(name_bytes);
                    const auto type = static_cast<rmdb::wire::SqlType>(
                        reader.get_u8());
                    if (name.empty() ||
                        !rmdb::wire::is_valid_utf8(name) ||
                        !valid_sql_type(type)) {
                        throw rmdb::wire::ProtocolError(
                            "invalid META column definition");
                    }
                    schema.push_back({name, type});
                }
                reader.require_consumed();
                for (size_t index = 0; index < schema.size(); ++index) {
                    if (index != 0) {
                        std::cout << '\t';
                    }
                    std::cout << schema[index].name;
                }
                std::cout << '\n';
                break;
            }
            case rmdb::wire::ServerTag::ROW: {
                if (schema.empty()) {
                    throw rmdb::wire::ProtocolError("ROW before META");
                }
                for (size_t index = 0; index < schema.size(); ++index) {
                    if (index != 0) {
                        std::cout << '\t';
                    }
                    std::cout << decode_cell(reader, schema[index].type);
                }
                reader.require_consumed();
                std::cout << '\n';
                rows++;
                break;
            }
            case rmdb::wire::ServerTag::RESULT_END: {
                const uint64_t declared_rows = reader.get_u64();
                reader.require_consumed();
                if (schema.empty() || declared_rows != rows) {
                    throw rmdb::wire::ProtocolError(
                        "RESULT_END row count mismatch");
                }
                std::cout << "(" << rows << " rows)\n";
                return;
            }
            case rmdb::wire::ServerTag::COMMAND_OK:
                reader.require_consumed();
                if (!schema.empty()) {
                    throw rmdb::wire::ProtocolError(
                        "COMMAND_OK after query frames");
                }
                std::cout << "OK\n";
                return;
            case rmdb::wire::ServerTag::TRANSACTION_ABORT:
            case rmdb::wire::ServerTag::ERROR: {
                const bool transaction_abort =
                    static_cast<rmdb::wire::ServerTag>(header.tag) ==
                    rmdb::wire::ServerTag::TRANSACTION_ABORT;
                const std::string diagnostic =
                    reader.get_string(reader.remaining());
                reader.require_consumed();
                if (!rmdb::wire::is_valid_utf8(diagnostic)) {
                    throw rmdb::wire::ProtocolError(
                        "invalid UTF-8 diagnostic");
                }
                std::cerr
                    << (transaction_abort
                            ? "TRANSACTION_ABORT: "
                            : "ERROR: ")
                    << diagnostic << '\n';
                return;
            }
            default:
                throw rmdb::wire::ProtocolError(
                    "unexpected response tag for EXEC_STREAM");
        }
    }
}

}  // namespace

int main(int argc, char *argv[]) {
    const char *unix_socket_path = nullptr;
    const char *server_host = "127.0.0.1";
    int server_port = kDefaultPort;
    int option = 0;
    while ((option = getopt(argc, argv, "s:h:p:")) > 0) {
        switch (option) {
            case 's':
                unix_socket_path = optarg;
                break;
            case 'h':
                server_host = optarg;
                break;
            case 'p':
                server_port = std::strtol(optarg, nullptr, 10);
                break;
            default:
                return 1;
        }
    }

    const int fd = unix_socket_path == nullptr
                       ? connect_tcp(server_host, server_port)
                       : connect_unix(unix_socket_path);
    if (fd < 0) {
        std::cerr << "Failed to connect: " << strerror(errno) << '\n';
        return 1;
    }
    if (!handshake(fd)) {
        std::cerr << "RMDB Wire v3 handshake failed\n";
        close(fd);
        return 1;
    }

    try {
        while (true) {
            char *line = readline("RMDB v3> ");
            if (line == nullptr) {
                break;
            }
            std::string command(line);
            std::free(line);
            if (command.empty()) {
                continue;
            }
            add_history(command.c_str());
            if (is_exit_command(command)) {
                break;
            }
            execute_stream(fd, command);
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
