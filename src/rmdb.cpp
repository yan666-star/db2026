/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "analyze/analyze.h"
#include "common/config.h"
#include "execution/execution_manager.h"
#include "execution/sql_execution_service.h"
#include "network/connection_session.h"
#include "optimizer/optimizer.h"
#include "optimizer/planner.h"
#include "recovery/log_recovery.h"
#include "system/sm.h"

namespace {

constexpr uint16_t kServerPort = 8765;
constexpr int kListenBacklog = 128;
constexpr bool kVerboseServerLog = false;

volatile sig_atomic_t should_exit = 0;
int listener_fd = -1;
std::mutex parser_mutex;
std::mutex optimizer_mutex;

auto disk_manager = std::make_unique<DiskManager>();
auto buffer_pool_manager = std::make_unique<BufferPoolManager>(
    BUFFER_POOL_SIZE, disk_manager.get());
auto rm_manager = std::make_unique<RmManager>(
    disk_manager.get(), buffer_pool_manager.get());
auto ix_manager = std::make_unique<IxManager>(
    disk_manager.get(), buffer_pool_manager.get());
auto sm_manager = std::make_unique<SmManager>(
    disk_manager.get(), buffer_pool_manager.get(), rm_manager.get(),
    ix_manager.get());
auto lock_manager = std::make_unique<LockManager>();
auto transaction_manager = std::make_unique<TransactionManager>(
    lock_manager.get(), sm_manager.get());
auto planner = std::make_unique<Planner>(sm_manager.get());
auto optimizer = std::make_unique<Optimizer>(
    sm_manager.get(), planner.get());
auto ql_manager = std::make_unique<QlManager>(
    sm_manager.get(), transaction_manager.get(), planner.get());
auto log_manager = std::make_unique<LogManager>(disk_manager.get());
auto recovery_manager = std::make_unique<RecoveryManager>(
    disk_manager.get(), buffer_pool_manager.get(), sm_manager.get());

void signal_handler(int) {
    should_exit = 1;
    if (listener_fd >= 0) {
        close(listener_fd);
        listener_fd = -1;
    }
}

void *client_handler(void *raw_fd) {
    std::unique_ptr<int> client_fd(static_cast<int *>(raw_fd));
    const int fd = *client_fd;

    try {
        rmdb::execution::SqlExecutionService execution_service(
            sm_manager.get(),
            lock_manager.get(),
            transaction_manager.get(),
            optimizer.get(),
            ql_manager.get(),
            log_manager.get(),
            &parser_mutex,
            &optimizer_mutex);
        rmdb::wire::ConnectionSession session(execution_service);
        const rmdb::wire::SessionResult result = session.run(fd);
        if (kVerboseServerLog &&
            result != rmdb::wire::SessionResult::PEER_CLOSED) {
            std::cerr << "Wire session ended with status "
                      << static_cast<int>(result) << '\n';
        }
    } catch (const std::exception &error) {
        if (kVerboseServerLog) {
            std::cerr << "Wire session failed: " << error.what() << '\n';
        }
    }

    close(fd);
    return nullptr;
}

int create_listener() {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw UnixError();
    }

    const int reuse_address = 1;
    if (setsockopt(
            fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
            sizeof(reuse_address)) < 0) {
        close(fd);
        throw UnixError();
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(kServerPort);
    if (bind(
            fd, reinterpret_cast<sockaddr *>(&address),
            sizeof(address)) < 0) {
        close(fd);
        throw UnixError();
    }
    if (listen(fd, kListenBacklog) < 0) {
        close(fd);
        throw UnixError();
    }
    return fd;
}

void start_server() {
    listener_fd = create_listener();
    while (!should_exit) {
        sockaddr_in client_address{};
        socklen_t client_length = sizeof(client_address);
        const int fd = accept(
            listener_fd,
            reinterpret_cast<sockaddr *>(&client_address),
            &client_length);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (should_exit || errno == EBADF) {
                break;
            }
            if (kVerboseServerLog) {
                std::cerr << "accept failed: " << strerror(errno) << '\n';
            }
            continue;
        }

        auto client_fd = std::make_unique<int>(fd);
        pthread_t thread_id{};
        if (pthread_create(
                &thread_id, nullptr, client_handler, client_fd.get()) != 0) {
            close(fd);
            continue;
        }
        client_fd.release();
        pthread_detach(thread_id);
    }

    if (listener_fd >= 0) {
        close(listener_fd);
        listener_fd = -1;
    }
}

void initialize_database(const std::string &database_name) {
    if (!sm_manager->is_dir(database_name)) {
        sm_manager->create_db(database_name);
    }
    sm_manager->open_db(database_name);
    log_manager->initialize_from_disk();
    buffer_pool_manager->set_log_manager(log_manager.get());
    recovery_manager->set_log_manager(log_manager.get());

    recovery_manager->analyze();
    transaction_manager->advance_next_txn_id(
        recovery_manager->get_next_txn_id());
    recovery_manager->redo();
    recovery_manager->undo();
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <database>\n";
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    try {
        initialize_database(argv[1]);
        start_server();
        log_manager->flush_log_to_disk(true);
        sm_manager->close_db();
    } catch (const RMDBError &error) {
        std::cerr << error.what() << '\n';
        return 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
