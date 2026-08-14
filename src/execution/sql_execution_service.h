#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "network/request_dispatcher.h"
#include "transaction/transaction.h"

class Analyze;
class LockManager;
class LogManager;
class Optimizer;
class Portal;
class QlManager;
class SmManager;
class TransactionManager;
class Plan;
class Context;
namespace ast {
struct TreeNode;
}

namespace rmdb::execution {

class SqlExecutionService final : public wire::ExecutionService {
 public:
    SqlExecutionService(SmManager *sm_manager,
                        LockManager *lock_manager,
                        TransactionManager *transaction_manager,
                        Optimizer *optimizer,
                        QlManager *ql_manager,
                        LogManager *log_manager,
                        std::mutex *parser_mutex,
                        std::mutex *optimizer_mutex);
    ~SqlExecutionService() override;

    wire::PreparedArtifact prepare(
        const wire::PrepareEntry &entry) override;
    void execute_stream(const std::string &sql,
                        ResultSink &sink) override;
    void execute_prepared(
        const wire::PreparedStatement &statement,
        const std::vector<TypedValue> &parameters,
        ResultSink &sink) override;
    bool supports_prepared_batch(
        const wire::PreparedStatement &statement) const override;
    void execute_prepared_batch(
        const wire::PreparedStatement &statement,
        const std::vector<std::vector<TypedValue>> &parameter_rows,
        ResultSink &sink) override;

    bool has_active_transaction() const override;
    void abort_active_transaction() override;
    void reset_after_auto_abort() override;

 private:
    std::shared_ptr<ast::TreeNode> parse_sql(const std::string &sql);
    std::shared_ptr<Plan> build_plan(
        const std::string &sql,
        const std::vector<wire::SqlType> *parameter_types);
    std::vector<OutputColumn> infer_output_schema(
        const std::shared_ptr<Plan> &plan);
    void execute_plan(const std::shared_ptr<Plan> &plan, ResultSink &sink);
    void execute_load(const std::string &file_name,
                      const std::string &table_name,
                      ResultSink &sink);
    void ensure_transaction(Context *context, bool admit_execution);

    SmManager *sm_manager_;
    LockManager *lock_manager_;
    TransactionManager *transaction_manager_;
    Optimizer *optimizer_;
    QlManager *ql_manager_;
    LogManager *log_manager_;
    std::mutex *parser_mutex_;
    std::mutex *optimizer_mutex_;
    std::unique_ptr<Analyze> analyze_;
    std::unique_ptr<Portal> portal_;

    txn_id_t transaction_id_{INVALID_TXN_ID};
    IsolationLevel isolation_level_;
    bool explicit_txn_failed_{false};
};

}  // namespace rmdb::execution
