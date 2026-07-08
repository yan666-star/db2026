/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include <netinet/in.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

#include "common/config.h"
#include "common/session_defaults.h"
#include "errors.h"
#include "execution/executor_insert.h"
#include "optimizer/optimizer.h"
#include "recovery/log_recovery.h"
#include "optimizer/plan.h"
#include "optimizer/planner.h"
#include "portal.h"
#include "analyze/analyze.h"

#define SOCK_PORT 8765
#define MAX_CONN_LIMIT 8

static bool should_exit = false;

// 构建全局所需的管理器对象
auto disk_manager = std::make_unique<DiskManager>();
auto buffer_pool_manager = std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE, disk_manager.get());
auto rm_manager = std::make_unique<RmManager>(disk_manager.get(), buffer_pool_manager.get());
auto ix_manager = std::make_unique<IxManager>(disk_manager.get(), buffer_pool_manager.get());
auto sm_manager = std::make_unique<SmManager>(disk_manager.get(), buffer_pool_manager.get(), rm_manager.get(), ix_manager.get());
auto lock_manager = std::make_unique<LockManager>();
auto txn_manager = std::make_unique<TransactionManager>(lock_manager.get(), sm_manager.get());
auto planner = std::make_unique<Planner>(sm_manager.get());
auto optimizer = std::make_unique<Optimizer>(sm_manager.get(), planner.get());
auto ql_manager = std::make_unique<QlManager>(sm_manager.get(), txn_manager.get(), nullptr);
auto log_manager = std::make_unique<LogManager>(disk_manager.get());
auto recovery = std::make_unique<RecoveryManager>(
    disk_manager.get(), buffer_pool_manager.get(), sm_manager.get());
auto portal = std::make_unique<Portal>(sm_manager.get());
auto analyze = std::make_unique<Analyze>(sm_manager.get());
pthread_mutex_t *buffer_mutex;
pthread_mutex_t *sockfd_mutex;

static constexpr bool kVerboseServerLog = false;

namespace {

std::mutex read_committed_explicit_txn_mutex;

std::string trim_copy(const std::string &value) {
    size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin]))) {
        begin++;
    }
    size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        end--;
    }
    return value.substr(begin, end - begin);
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return std::tolower(ch); });
    return value;
}

bool iequals(const std::string &lhs, const std::string &rhs) {
    return lower_copy(lhs) == lower_copy(rhs);
}

enum class TxnBoundary {
    None,
    Begin,
    Commit,
    Rollback,
    Abort,
};

TxnBoundary parse_txn_boundary(const std::string &sql) {
    std::string text = trim_copy(sql);
    if (!text.empty() && text.back() == ';') {
        text.pop_back();
        text = trim_copy(text);
    }

    std::istringstream iss(text);
    std::vector<std::string> words;
    std::string word;
    while (iss >> word) {
        words.push_back(lower_copy(word));
    }
    if (words.empty() || words.size() > 2) {
        return TxnBoundary::None;
    }

    if (words[0] == "start" && words.size() == 2 &&
        words[1] == "transaction") {
        return TxnBoundary::Begin;
    }
    if (words[0] == "begin" &&
        (words.size() == 1 || words[1] == "work" ||
         words[1] == "transaction")) {
        return TxnBoundary::Begin;
    }
    if (words[0] == "commit" &&
        (words.size() == 1 || words[1] == "work" ||
         words[1] == "transaction")) {
        return TxnBoundary::Commit;
    }
    if (words[0] == "rollback" &&
        (words.size() == 1 || words[1] == "work" ||
         words[1] == "transaction")) {
        return TxnBoundary::Rollback;
    }
    if (words[0] == "abort" &&
        (words.size() == 1 || words[1] == "work" ||
         words[1] == "transaction")) {
        return TxnBoundary::Abort;
    }
    return TxnBoundary::None;
}

std::string canonical_txn_sql(TxnBoundary boundary,
                              const std::string &raw_sql) {
    switch (boundary) {
        case TxnBoundary::Begin:
            return "BEGIN;";
        case TxnBoundary::Commit:
            return "COMMIT;";
        case TxnBoundary::Rollback:
            return "ROLLBACK;";
        case TxnBoundary::Abort:
            return "ABORT;";
        case TxnBoundary::None:
            return raw_sql;
    }
    return raw_sql;
}

bool parse_output_file_command(const std::string &sql, bool &enabled) {
    std::string text = trim_copy(sql);
    if (!text.empty() && text.back() == ';') {
        text.pop_back();
        text = trim_copy(text);
    }

    std::istringstream iss(text);
    std::string set_kw;
    std::string output_kw;
    std::string state_kw;
    std::string extra;
    if (!(iss >> set_kw >> output_kw >> state_kw) || (iss >> extra)) {
        return false;
    }
    if (!iequals(set_kw, "set") || !iequals(output_kw, "output_file")) {
        return false;
    }
    if (iequals(state_kw, "off")) {
        enabled = false;
        return true;
    }
    if (iequals(state_kw, "on")) {
        enabled = true;
        return true;
    }
    return false;
}

bool parse_load_command(const std::string &sql, std::string &file_name,
                        std::string &table_name) {
    std::string text = trim_copy(sql);
    if (!text.empty() && text.back() == ';') {
        text.pop_back();
        text = trim_copy(text);
    }

    std::istringstream iss(text);
    std::string load_kw;
    std::string into_kw;
    std::string extra;
    if (!(iss >> load_kw >> file_name >> into_kw >> table_name)) {
        return false;
    }
    if (iss >> extra) {
        return false;
    }
    return iequals(load_kw, "load") && iequals(into_kw, "into") &&
           !file_name.empty() && !table_name.empty();
}

std::vector<std::string> parse_csv_line(const std::string &line) {
    std::vector<std::string> fields;
    std::string field;
    char quote = '\0';
    for (size_t i = 0; i < line.size(); i++) {
        char ch = line[i];
        if (quote != '\0') {
            if (ch == quote) {
                if (i + 1 < line.size() && line[i + 1] == quote) {
                    field.push_back(ch);
                    i++;
                } else {
                    quote = '\0';
                }
            } else {
                field.push_back(ch);
            }
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
        } else if (ch == ',') {
            fields.push_back(trim_copy(field));
            field.clear();
        } else {
            field.push_back(ch);
        }
    }
    fields.push_back(trim_copy(field));
    return fields;
}

Value csv_field_to_value(const std::string &field, const ColMeta &col) {
    Value value;
    std::string text = trim_copy(field);
    if (col.type == TYPE_INT) {
        value.set_int(std::stoi(text));
    } else if (col.type == TYPE_FLOAT) {
        value.set_float(std::stof(text));
    } else {
        value.set_str(text);
    }
    return value;
}

void execute_load_command(const std::string &file_name,
                          const std::string &table_name, Context *context) {
    std::ifstream input(file_name);
    if (!input.is_open()) {
        throw RMDBError("failure");
    }

    const auto &tab = sm_manager->db_.get_table(table_name);
    std::string line;
    bool is_header = true;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (is_header) {
            is_header = false;
            continue;
        }
        if (trim_copy(line).empty()) {
            continue;
        }

        std::vector<std::string> fields = parse_csv_line(line);
        if (fields.size() != tab.cols.size()) {
            throw InvalidValueCountError();
        }
        std::vector<Value> values;
        values.reserve(fields.size());
        for (size_t i = 0; i < fields.size(); i++) {
            values.push_back(csv_field_to_value(fields[i], tab.cols[i]));
        }
        InsertExecutor executor(sm_manager.get(), table_name, values, context);
        executor.Next();
    }
}

void write_output_if_enabled(const std::string &text) {
    if (!enable_output_file.load()) {
        return;
    }
    std::fstream outfile;
    outfile.open("output.txt", std::ios::out | std::ios::app);
    outfile << text;
    outfile.close();
}

void write_failure_response(char *data_send, int *offset) {
    const std::string client_msg = "failure";
    memcpy(data_send, client_msg.c_str(), client_msg.length());
    data_send[client_msg.length()] = '\n';
    data_send[client_msg.length() + 1] = '\0';
    *offset = static_cast<int>(client_msg.length() + 1);
    write_output_if_enabled("failure\n");
}

void release_read_committed_explicit_guard(bool &guard_held) {
    if (!guard_held) {
        return;
    }
    read_committed_explicit_txn_mutex.unlock();
    guard_held = false;
}

}  // namespace

static jmp_buf jmpbuf;
void sigint_handler(int) {
    should_exit = true;
    log_manager->flush_log_to_disk(true);
    if (kVerboseServerLog) {
        std::cout << "The Server receive Crtl+C, will been closed\n";
    }
    longjmp(jmpbuf, 1);
}

// 判断当前正在执行的是显式事务还是单条SQL语句的事务，并更新事务ID
void SetTransaction(txn_id_t *txn_id, Context *context,
                    IsolationLevel session_isolation) {
    context->txn_ = txn_manager->get_transaction(*txn_id);
    if(context->txn_ == nullptr || context->txn_->get_state() == TransactionState::COMMITTED ||
        context->txn_->get_state() == TransactionState::ABORTED) {
        context->txn_ =
            txn_manager->begin(nullptr, context->log_mgr_, session_isolation);
        *txn_id = context->txn_->get_transaction_id();
        context->txn_->set_txn_mode(false);
    }
}

void *client_handler(void *sock_fd) {
    int fd = *((int *)sock_fd);
    pthread_mutex_unlock(sockfd_mutex);

    int i_recvBytes;
    // 接收客户端发送的请求
    char data_recv[BUFFER_LENGTH];
    // 需要返回给客户端的结果
    char *data_send = new char[BUFFER_LENGTH];
    // 需要返回给客户端的结果的长度
    int offset = 0;
    // 记录客户端当前正在执行的事务ID
    txn_id_t txn_id = INVALID_TXN_ID;
    IsolationLevel session_isolation = session_defaults::get();
    bool explicit_txn_failed = false;
    bool read_committed_explicit_guard_held = false;

    std::string output = "establish client connection, sockfd: " + std::to_string(fd) + "\n";
    if (kVerboseServerLog) {
        std::cout << output;
    }

    while (true) {
        if (kVerboseServerLog) {
            std::cout << "Waiting for request..." << std::endl;
        }
        memset(data_recv, 0, BUFFER_LENGTH);

        i_recvBytes = read(fd, data_recv, BUFFER_LENGTH);

        if (i_recvBytes == 0) {
            if (kVerboseServerLog) {
                std::cout << "Maybe the client has closed" << std::endl;
            }
            break;
        }
        if (i_recvBytes == -1) {
            if (kVerboseServerLog) {
                std::cout << "Client read error!" << std::endl;
            }
            break;
        }

        if (kVerboseServerLog) {
            printf("i_recvBytes: %d \n ", i_recvBytes);
        }

        if (strcmp(data_recv, "exit") == 0) {
            if (kVerboseServerLog) {
                std::cout << "Client exit." << std::endl;
            }
            break;
        }
        if (strcmp(data_recv, "crash") == 0) {
            if (kVerboseServerLog) {
                std::cout << "Server crash" << std::endl;
            }
            exit(1);
        }

        if (kVerboseServerLog) {
            std::cout << "Read from client " << fd << ": " << data_recv << std::endl;
        }

        memset(data_send, '\0', BUFFER_LENGTH);
        offset = 0;

        // 开启事务，初始化系统所需的上下文信息（包括事务对象指针、锁管理器指针、日志管理器指针、存放结果的buffer、记录结果长度的变量）
        auto context_holder = std::make_unique<Context>(
            lock_manager.get(), log_manager.get(), nullptr, data_send, &offset,
            txn_manager.get(), &session_isolation);
        Context *context = context_holder.get();
        bool statement_entered = false;

        std::string raw_sql = trim_copy(data_recv);
        TxnBoundary txn_boundary = parse_txn_boundary(raw_sql);
        bool output_file_enabled = true;
        if (parse_output_file_command(raw_sql, output_file_enabled)) {
            enable_output_file.store(output_file_enabled);
            if (!output_file_enabled) {
                // TPC-C performance phase runs under snapshot isolation; new
                // worker connections inherit SI without requiring a separate SET.
                session_defaults::set(IsolationLevel::SNAPSHOT_ISOLATION);
            }
            bool write_failed = write(fd, data_send, offset + 1) == -1;
            if (write_failed) {
                break;
            }
            continue;
        }

        if (explicit_txn_failed) {
            if (txn_boundary == TxnBoundary::Rollback ||
                txn_boundary == TxnBoundary::Abort) {
                explicit_txn_failed = false;
            } else if (txn_boundary == TxnBoundary::Commit) {
                explicit_txn_failed = false;
                write_failure_response(data_send, &offset);
            } else if (txn_boundary != TxnBoundary::Begin) {
                write_failure_response(data_send, &offset);
            } else {
                explicit_txn_failed = false;
            }

            if (txn_boundary != TxnBoundary::Begin &&
                txn_boundary != TxnBoundary::Rollback &&
                txn_boundary != TxnBoundary::Abort) {
                bool write_failed = write(fd, data_send, offset + 1) == -1;
                if (write_failed) {
                    break;
                }
                continue;
            }
            if (txn_boundary == TxnBoundary::Rollback ||
                txn_boundary == TxnBoundary::Abort) {
                bool write_failed = write(fd, data_send, offset + 1) == -1;
                if (write_failed) {
                    break;
                }
                continue;
            }
        }

        if (txn_boundary == TxnBoundary::Begin &&
            session_isolation == IsolationLevel::READ_COMMITTED &&
            !read_committed_explicit_guard_held) {
            read_committed_explicit_txn_mutex.lock();
            read_committed_explicit_guard_held = true;
        }

        std::string load_file;
        std::string load_table;
        if (parse_load_command(raw_sql, load_file, load_table)) {
            try {
                txn_manager->enter_statement(txn_id);
                statement_entered = true;
                SetTransaction(&txn_id, context, session_isolation);
                execute_load_command(load_file, load_table, context);
            } catch (TransactionAbortException &e) {
                std::string str = "abort\n";
                memcpy(data_send, str.c_str(), str.length());
                data_send[str.length()] = '\0';
                offset = str.length();

                bool was_explicit_txn =
                    context->txn_ != nullptr && context->txn_->get_txn_mode();
                txn_manager->abort(context->txn_, log_manager.get());
                txn_manager->release_transaction(context->txn_);
                context->txn_ = nullptr;
                txn_id = INVALID_TXN_ID;
                if (was_explicit_txn) {
                    explicit_txn_failed = true;
                    release_read_committed_explicit_guard(
                        read_committed_explicit_guard_held);
                }
                if (kVerboseServerLog) {
                    std::cout << e.GetInfo() << std::endl;
                }
                write_output_if_enabled(str);
            } catch (RMDBError &e) {
                if (kVerboseServerLog) {
                    std::cerr << e.what() << std::endl;
                }

                std::string client_msg = "failure";
                memcpy(data_send, client_msg.c_str(), client_msg.length());
                data_send[client_msg.length()] = '\n';
                data_send[client_msg.length() + 1] = '\0';
                offset = client_msg.length() + 1;

                write_output_if_enabled("failure\n");
                if (context->txn_ != nullptr &&
                    context->txn_->get_state() != TransactionState::COMMITTED &&
                    context->txn_->get_state() != TransactionState::ABORTED) {
                    bool was_explicit_txn = context->txn_->get_txn_mode();
                    txn_manager->abort(context->txn_, log_manager.get());
                    txn_manager->release_transaction(context->txn_);
                    context->txn_ = nullptr;
                    txn_id = INVALID_TXN_ID;
                    if (was_explicit_txn) {
                        explicit_txn_failed = true;
                        release_read_committed_explicit_guard(
                            read_committed_explicit_guard_held);
                    }
                }
            } catch (const std::exception &e) {
                if (kVerboseServerLog) {
                    std::cerr << e.what() << std::endl;
                }
                const std::string client_msg = "failure\n";
                memcpy(data_send, client_msg.c_str(), client_msg.size());
                data_send[client_msg.size()] = '\0';
                offset = static_cast<int>(client_msg.size());
                write_output_if_enabled("failure\n");
                if (context->txn_ != nullptr &&
                    context->txn_->get_state() != TransactionState::COMMITTED &&
                    context->txn_->get_state() != TransactionState::ABORTED) {
                    bool was_explicit_txn = context->txn_->get_txn_mode();
                    txn_manager->abort(context->txn_, log_manager.get());
                    txn_manager->release_transaction(context->txn_);
                    context->txn_ = nullptr;
                    txn_id = INVALID_TXN_ID;
                    if (was_explicit_txn) {
                        explicit_txn_failed = true;
                        release_read_committed_explicit_guard(
                            read_committed_explicit_guard_held);
                    }
                }
            }

            if(context->txn_ != nullptr && context->txn_->get_txn_mode() == false)
            {
                txn_manager->commit(context->txn_, context->log_mgr_);
                txn_manager->release_transaction(context->txn_);
                context->txn_ = nullptr;
                txn_id = INVALID_TXN_ID;
            }
            if (statement_entered) {
                txn_manager->leave_statement();
            }
            bool write_failed = write(fd, data_send, offset + 1) == -1;
            if (write_failed) {
                break;
            }
            continue;
        }

        // 用于判断是否已经调用了yy_delete_buffer来删除buf
        bool finish_analyze = false;
        pthread_mutex_lock(buffer_mutex);
        std::string parser_sql = canonical_txn_sql(txn_boundary, raw_sql);
        YY_BUFFER_STATE buf = yy_scan_string(parser_sql.c_str());
        if (yyparse() == 0) {
            if (ast::parse_tree != nullptr) {
                try {
                    bool is_checkpoint =
                        std::dynamic_pointer_cast<ast::StaticCheckpoint>(ast::parse_tree) != nullptr;
                    if (!is_checkpoint) {
                        txn_manager->enter_statement(txn_id);
                        statement_entered = true;
                        SetTransaction(&txn_id, context, session_isolation);
                    }

                    // analyze and rewrite
                    std::shared_ptr<Query> query = analyze->do_analyze(ast::parse_tree);
                    yy_delete_buffer(buf);
                    finish_analyze = true;
                    pthread_mutex_unlock(buffer_mutex);
                    // 优化器
                    std::shared_ptr<Plan> plan = optimizer->plan_query(query, context);
                    // portal
                    std::shared_ptr<PortalStmt> portalStmt = portal->start(plan, context);
                    portal->run(portalStmt, ql_manager.get(), &txn_id, context);
                    portal->drop();
                } catch (TransactionAbortException &e) {
                    // 事务需要回滚，需要把abort信息返回给客户端并写入output.txt文件中
                    std::string str = "abort\n";
                    memcpy(data_send, str.c_str(), str.length());
                    data_send[str.length()] = '\0';
                    offset = str.length();

                    // 回滚事务
                    bool was_explicit_txn =
                        context->txn_ != nullptr && context->txn_->get_txn_mode();
                    txn_manager->abort(context->txn_, log_manager.get());
                    txn_manager->release_transaction(context->txn_);
                    context->txn_ = nullptr;
                    txn_id = INVALID_TXN_ID;
                    if (was_explicit_txn) {
                        explicit_txn_failed = true;
                        release_read_committed_explicit_guard(
                            read_committed_explicit_guard_held);
                    }
                    if (kVerboseServerLog) {
                        std::cout << e.GetInfo() << std::endl;
                    }

                    write_output_if_enabled(str);
                } catch (RMDBError &e) {
                    // 遇到异常，需要打印failure到output.txt文件中，并发异常信息返回给客户端
                    if (kVerboseServerLog) {
                        std::cerr << e.what() << std::endl;
                    }

                    std::string client_msg = "failure";
                    memcpy(data_send, client_msg.c_str(), client_msg.length());
                    data_send[client_msg.length()] = '\n';
                    data_send[client_msg.length() + 1] = '\0';
                    offset = client_msg.length() + 1;

                    // 将报错信息写入output.txt
                    write_output_if_enabled("failure\n");
                    if (context->txn_ != nullptr &&
                        context->txn_->get_state() != TransactionState::COMMITTED &&
                        context->txn_->get_state() != TransactionState::ABORTED) {
                        bool was_explicit_txn = context->txn_->get_txn_mode();
                        txn_manager->abort(context->txn_, log_manager.get());
                        txn_manager->release_transaction(context->txn_);
                        context->txn_ = nullptr;
                        txn_id = INVALID_TXN_ID;
                        if (was_explicit_txn) {
                            explicit_txn_failed = true;
                            release_read_committed_explicit_guard(
                                read_committed_explicit_guard_held);
                        }
                    }
                } catch (const std::exception &e) {
                    if (kVerboseServerLog) {
                        std::cerr << e.what() << std::endl;
                    }
                    const std::string client_msg = "failure\n";
                    memcpy(data_send, client_msg.c_str(), client_msg.size());
                    data_send[client_msg.size()] = '\0';
                    offset = static_cast<int>(client_msg.size());
                    write_output_if_enabled("failure\n");
                    if (context->txn_ != nullptr &&
                        context->txn_->get_state() != TransactionState::COMMITTED &&
                        context->txn_->get_state() != TransactionState::ABORTED) {
                        bool was_explicit_txn = context->txn_->get_txn_mode();
                        txn_manager->abort(context->txn_, log_manager.get());
                        txn_manager->release_transaction(context->txn_);
                        context->txn_ = nullptr;
                        txn_id = INVALID_TXN_ID;
                        if (was_explicit_txn) {
                            explicit_txn_failed = true;
                            release_read_committed_explicit_guard(
                                read_committed_explicit_guard_held);
                        }
                    }
                }
            }
        } else {
            std::string client_msg = "failure";
            memcpy(data_send, client_msg.c_str(), client_msg.length());
            data_send[client_msg.length()] = '\n';
            data_send[client_msg.length() + 1] = '\0';
            offset = client_msg.length() + 1;

            write_output_if_enabled("failure\n");
            if (txn_boundary == TxnBoundary::Begin) {
                release_read_committed_explicit_guard(
                    read_committed_explicit_guard_held);
            }
        }
        if(finish_analyze == false) {
            yy_delete_buffer(buf);
            pthread_mutex_unlock(buffer_mutex);
        }
        // future TODO: 格式化 sql_handler.result, 传给客户端
        // send result with fixed format, use protobuf in the future
        // 如果是单挑语句，需要按照一个完整的事务来执行，所以执行完当前语句后，自动提交事务
        if(context->txn_ != nullptr && context->txn_->get_txn_mode() == false)
        {
            txn_manager->commit(context->txn_, context->log_mgr_);
            txn_manager->release_transaction(context->txn_);
            context->txn_ = nullptr;
            txn_id = INVALID_TXN_ID;
        }
        if ((txn_boundary == TxnBoundary::Commit ||
             txn_boundary == TxnBoundary::Rollback ||
             txn_boundary == TxnBoundary::Abort) &&
            txn_id == INVALID_TXN_ID) {
            release_read_committed_explicit_guard(
                read_committed_explicit_guard_held);
        }
        if (statement_entered) {
            txn_manager->leave_statement();
        }
        // Do not report success before an implicit transaction's COMMIT record
        // is durable; otherwise an acknowledged write can be lost on crash.
        bool write_failed = write(fd, data_send, offset + 1) == -1;
        if (write_failed) {
            break;
        }
    }

    // Clear
    if (kVerboseServerLog) {
        std::cout << "Terminating current client_connection..." << std::endl;
    }
    Transaction *remaining_txn = txn_manager->get_transaction(txn_id);
    if (remaining_txn != nullptr &&
        remaining_txn->get_state() != TransactionState::COMMITTED &&
        remaining_txn->get_state() != TransactionState::ABORTED) {
        txn_manager->abort(remaining_txn, log_manager.get());
    }
    txn_manager->release_transaction(remaining_txn);
    release_read_committed_explicit_guard(read_committed_explicit_guard_held);
    delete[] data_send;
    close(fd);           // close a file descriptor.
    pthread_exit(NULL);  // terminate calling thread!
    return nullptr;
}

void start_server() {
    // init mutex
    buffer_mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    sockfd_mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(buffer_mutex, nullptr);
    pthread_mutex_init(sockfd_mutex, nullptr);

    int sockfd_server;
    int fd_temp;
    struct sockaddr_in s_addr_in {};

    // 初始化连接
    sockfd_server = socket(AF_INET, SOCK_STREAM, 0);  // ipv4,TCP
    assert(sockfd_server != -1);
    int val = 1;
    setsockopt(sockfd_server, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // before bind(), set the attr of structure sockaddr.
    memset(&s_addr_in, 0, sizeof(s_addr_in));
    s_addr_in.sin_family = AF_INET;
    s_addr_in.sin_addr.s_addr = htonl(INADDR_ANY);
    s_addr_in.sin_port = htons(SOCK_PORT);
    fd_temp = bind(sockfd_server, (struct sockaddr *)(&s_addr_in), sizeof(s_addr_in));
    if (fd_temp == -1) {
        if (kVerboseServerLog) {
            std::cout << "Bind error!" << std::endl;
        }
        exit(1);
    }

    fd_temp = listen(sockfd_server, MAX_CONN_LIMIT);
    if (fd_temp == -1) {
        if (kVerboseServerLog) {
            std::cout << "Listen error!" << std::endl;
        }
        exit(1);
    }

    while (!should_exit) {
        if (kVerboseServerLog) {
            std::cout << "Waiting for new connection..." << std::endl;
        }
        pthread_t thread_id;
        struct sockaddr_in s_addr_client {};
        int client_length = sizeof(s_addr_client);

        if (setjmp(jmpbuf)) {
            if (kVerboseServerLog) {
                std::cout << "Break from Server Listen Loop\n";
            }
            break;
        }

        // Block here. Until server accepts a new connection.
        pthread_mutex_lock(sockfd_mutex);
        int sockfd = accept(sockfd_server, (struct sockaddr *)(&s_addr_client), (socklen_t *)(&client_length));
        if (sockfd == -1) {
            if (kVerboseServerLog) {
                std::cout << "Accept error!" << std::endl;
            }
            continue;  // ignore current socket ,continue while loop.
        }
        
        // 和客户端建立连接，并开启一个线程负责处理客户端请求
        if (pthread_create(&thread_id, nullptr, &client_handler, (void *)(&sockfd)) != 0) {
            if (kVerboseServerLog) {
                std::cout << "Create thread fail!" << std::endl;
            }
            break;  // break while loop
        }
        pthread_detach(thread_id);

    }

    // Clear
    if (kVerboseServerLog) {
        std::cout << " Try to close all client-connection.\n";
    }
    int ret = shutdown(sockfd_server, SHUT_WR);  // shut down the all or part of a full-duplex connection.
    if(ret == -1 && kVerboseServerLog) { printf("%s\n", strerror(errno)); }
//    assert(ret != -1);
    sm_manager->close_db();
    if (kVerboseServerLog) {
        std::cout << " DB has been closed.\n";
        std::cout << "Server shuts down." << std::endl;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        // 需要指定数据库名称
        std::cerr << "Usage: " << argv[0] << " <database>" << std::endl;
        exit(1);
    }

    signal(SIGINT, sigint_handler);
    signal(SIGPIPE, SIG_IGN);
    try {
        if (kVerboseServerLog) {
            std::cout << "\n"
                         "  _____  __  __ _____  ____  \n"
                         " |  __ \\|  \\/  |  __ \\|  _ \\ \n"
                         " | |__) | \\  / | |  | | |_) |\n"
                         " |  _  /| |\\/| | |  | |  _ < \n"
                         " | | \\ \\| |  | | |__| | |_) |\n"
                         " |_|  \\_\\_|  |_|_____/|____/ \n"
                         "\n"
                         "Welcome to RMDB!\n"
                         "Type 'help;' for help.\n"
                         "\n";
        }
        // Database name is passed by args
        std::string db_name = argv[1];
        if (!sm_manager->is_dir(db_name)) {
            // Database not found, create a new one
            sm_manager->create_db(db_name);
        }
        // Open database
        sm_manager->open_db(db_name);
        log_manager->initialize_from_disk();
        buffer_pool_manager->set_log_manager(log_manager.get());
        recovery->set_log_manager(log_manager.get());

        // recovery database
        recovery->analyze();
        txn_manager->advance_next_txn_id(recovery->get_next_txn_id());
        recovery->redo();
        recovery->undo();
        
        // 开启服务端，开始接受客户端连接
        start_server();
    } catch (RMDBError &e) {
        if (kVerboseServerLog) {
            std::cerr << e.what() << std::endl;
        }
        exit(1);
    }
    return 0;
}
