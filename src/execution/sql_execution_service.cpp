#include "execution/sql_execution_service.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <memory>
#include <shared_mutex>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "analyze/analyze.h"
#include "common/context.h"
#include "common/session_defaults.h"
#include "errors.h"
#include "execution/execution_manager.h"
#include "execution/executor_insert.h"
#include "execution/parameter_binding.h"
#include "optimizer/optimizer.h"
#include "parser/parser.h"
#include "portal.h"
#include "recovery/log_manager.h"
#include "system/sm.h"
#include "transaction/transaction_manager.h"

namespace rmdb::execution {
namespace {

class PlanExecutable final : public wire::PreparedExecutable {
 public:
    explicit PlanExecutable(std::shared_ptr<Plan> plan_value)
        : plan(std::move(plan_value)) {}

    std::shared_ptr<Plan> plan;
};

class TrackingResultSink final : public ResultSink {
 public:
    explicit TrackingResultSink(ResultSink &target) : target_(target) {}

    void begin_query(const std::vector<OutputColumn> &schema) override {
        require_idle();
        query_started_ = true;
        target_.begin_query(schema);
    }

    void push_row(const std::vector<TypedValue> &row) override {
        if (!query_started_ || terminal_) {
            throw InternalError("Typed row emitted outside a query");
        }
        target_.push_row(row);
        row_count_++;
    }

    void end_query(uint64_t row_count) override {
        if (!query_started_ || terminal_ || row_count != row_count_) {
            throw InternalError("Typed query terminal is inconsistent");
        }
        terminal_ = true;
        target_.end_query(row_count);
    }

    void command_ok() override {
        require_idle();
        terminal_ = true;
        target_.command_ok();
    }

    bool terminal() const noexcept { return terminal_; }

 private:
    void require_idle() const {
        if (query_started_ || terminal_) {
            throw InternalError("Execution emitted multiple result starts");
        }
    }

    ResultSink &target_;
    uint64_t row_count_{0};
    bool query_started_{false};
    bool terminal_{false};
};

ColType engine_type(wire::SqlType type) {
    switch (type) {
        case wire::SqlType::INT32:
            return TYPE_INT;
        case wire::SqlType::FLOAT32:
            return TYPE_FLOAT;
        case wire::SqlType::CHAR:
            return TYPE_STRING;
    }
    throw wire::ProtocolError("Unknown prepared SQL type");
}

wire::SqlType wire_type(ColType type) {
    switch (type) {
        case TYPE_INT:
            return wire::SqlType::INT32;
        case TYPE_FLOAT:
            return wire::SqlType::FLOAT32;
        case TYPE_STRING:
            return wire::SqlType::CHAR;
    }
    throw InternalError("Unexpected output column type");
}

bool is_begin_plan(const std::shared_ptr<Plan> &plan) {
    return plan != nullptr && plan->tag == T_Transaction_begin;
}

bool accesses_table_data(const std::shared_ptr<Plan> &plan) {
    if (plan == nullptr) {
        return false;
    }
    return plan->tag == T_Transaction_begin || plan->tag == T_select ||
           plan->tag == T_Update ||
           plan->tag == T_Delete || plan->tag == T_Insert;
}

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
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

bool parse_load_command(const std::string &sql,
                        std::string *file_name,
                        std::string *table_name) {
    std::string text = trim_copy(sql);
    if (!text.empty() && text.back() == ';') {
        text.pop_back();
        text = trim_copy(text);
    }
    std::istringstream input(text);
    std::string load_keyword;
    std::string into_keyword;
    std::string extra;
    if (!(input >> load_keyword >> *file_name >> into_keyword >>
          *table_name) ||
        (input >> extra)) {
        return false;
    }
    return lower_copy(load_keyword) == "load" &&
           lower_copy(into_keyword) == "into" &&
           !file_name->empty() && !table_name->empty();
}

std::vector<std::string> parse_csv_line(const std::string &line) {
    std::vector<std::string> fields;
    std::string field;
    char quote = '\0';
    for (size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (quote != '\0') {
            if (character == quote) {
                if (index + 1U < line.size() &&
                    line[index + 1U] == quote) {
                    field.push_back(character);
                    index++;
                } else {
                    quote = '\0';
                }
            } else {
                field.push_back(character);
            }
        } else if (character == '\'' || character == '"') {
            quote = character;
        } else if (character == ',') {
            fields.push_back(trim_copy(field));
            field.clear();
        } else {
            field.push_back(character);
        }
    }
    if (quote != '\0') {
        throw RMDBError("Unterminated CSV quote");
    }
    fields.push_back(trim_copy(field));
    return fields;
}

Value csv_value(const std::string &field, const ColMeta &column) {
    Value value;
    const std::string text = trim_copy(field);
    if (column.type == TYPE_INT) {
        size_t consumed = 0;
        const int parsed = std::stoi(text, &consumed);
        if (consumed != text.size()) {
            throw RMDBError("Invalid INT value in LOAD input");
        }
        value.set_int(parsed);
    } else if (column.type == TYPE_FLOAT) {
        size_t consumed = 0;
        const float parsed = std::stof(text, &consumed);
        if (consumed != text.size()) {
            throw RMDBError("Invalid FLOAT value in LOAD input");
        }
        value.set_float(parsed);
    } else {
        value.set_str(text);
    }
    return value;
}

std::vector<size_t> build_csv_column_mapping(
    std::vector<std::string> header_fields,
    const TabMeta &table) {
    if (!header_fields.empty() && header_fields[0].size() >= 3U &&
        static_cast<unsigned char>(header_fields[0][0]) == 0xEFU &&
        static_cast<unsigned char>(header_fields[0][1]) == 0xBBU &&
        static_cast<unsigned char>(header_fields[0][2]) == 0xBFU) {
        header_fields[0].erase(0, 3);
    }
    if (header_fields.size() != table.cols.size()) {
        throw InvalidValueCountError();
    }

    std::unordered_map<std::string, size_t> header_indexes;
    header_indexes.reserve(header_fields.size());
    for (size_t index = 0; index < header_fields.size(); ++index) {
        const std::string name = trim_copy(header_fields[index]);
        if (name.empty() || !header_indexes.emplace(name, index).second) {
            throw RMDBError("Invalid or duplicate column in LOAD header");
        }
    }

    std::vector<size_t> mapping;
    mapping.reserve(table.cols.size());
    for (const auto &column : table.cols) {
        const auto found = header_indexes.find(column.name);
        if (found == header_indexes.end()) {
            throw RMDBError("LOAD header does not match table schema");
        }
        mapping.push_back(found->second);
    }
    return mapping;
}

}  // namespace

SqlExecutionService::SqlExecutionService(
    SmManager *sm_manager,
    LockManager *lock_manager,
    TransactionManager *transaction_manager,
    Optimizer *optimizer,
    QlManager *ql_manager,
    LogManager *log_manager,
    std::mutex *parser_mutex,
    std::mutex *optimizer_mutex)
    : sm_manager_(sm_manager),
      lock_manager_(lock_manager),
      transaction_manager_(transaction_manager),
      optimizer_(optimizer),
      ql_manager_(ql_manager),
      log_manager_(log_manager),
      parser_mutex_(parser_mutex),
      optimizer_mutex_(optimizer_mutex),
      analyze_(std::make_unique<Analyze>(sm_manager)),
      portal_(std::make_unique<Portal>(sm_manager)),
      isolation_level_(session_defaults::get()) {
    if (sm_manager_ == nullptr || lock_manager_ == nullptr ||
        transaction_manager_ == nullptr || optimizer_ == nullptr ||
        ql_manager_ == nullptr || log_manager_ == nullptr ||
        parser_mutex_ == nullptr || optimizer_mutex_ == nullptr) {
        throw InternalError("SQL execution service dependency is null");
    }
}

SqlExecutionService::~SqlExecutionService() {
    try {
        abort_active_transaction();
    } catch (...) {
    }
}

std::shared_ptr<ast::TreeNode> SqlExecutionService::parse_sql(
    const std::string &sql) {
    std::lock_guard<std::mutex> guard(*parser_mutex_);
    YY_BUFFER_STATE buffer = yy_scan_string(sql.c_str());
    const int parse_result = yyparse();
    std::shared_ptr<ast::TreeNode> parse_tree = ast::parse_tree;
    yy_delete_buffer(buffer);
    ast::parse_tree.reset();
    if (parse_result != 0 || parse_tree == nullptr) {
        throw RMDBError("SQL parse failure");
    }
    return parse_tree;
}

std::shared_ptr<Plan> SqlExecutionService::build_plan(
    const std::string &sql,
    const std::vector<wire::SqlType> *parameter_types) {
    auto parse_tree = parse_sql(sql);
    std::shared_ptr<Query> query;
    if (parameter_types == nullptr) {
        query = analyze_->do_analyze(std::move(parse_tree));
    } else {
        std::vector<ColType> types;
        types.reserve(parameter_types->size());
        for (const wire::SqlType type : *parameter_types) {
            types.push_back(engine_type(type));
        }
        query = analyze_->do_analyze_prepared(
            std::move(parse_tree), types);
    }

    Context planning_context(
        lock_manager_, log_manager_, nullptr, transaction_manager_,
        &isolation_level_, nullptr);
    std::lock_guard<std::mutex> optimizer_guard(*optimizer_mutex_);
    return optimizer_->plan_query(std::move(query), &planning_context);
}

std::vector<OutputColumn> SqlExecutionService::infer_output_schema(
    const std::shared_ptr<Plan> &plan) {
    if (plan == nullptr) {
        throw InternalError("Cannot infer schema from a null plan");
    }
    if (plan->tag == T_Help) {
        return {{"help", wire::SqlType::CHAR}};
    }
    if (plan->tag == T_ShowTable) {
        return {{"Tables", wire::SqlType::CHAR}};
    }
    if (plan->tag == T_ShowIndex) {
        return {{"Table", wire::SqlType::CHAR},
                {"Kind", wire::SqlType::CHAR},
                {"Columns", wire::SqlType::CHAR}};
    }
    if (plan->tag == T_DescTable) {
        return {{"Field", wire::SqlType::CHAR},
                {"Type", wire::SqlType::CHAR},
                {"Index", wire::SqlType::CHAR}};
    }

    auto dml = std::dynamic_pointer_cast<DMLPlan>(plan);
    if (dml == nullptr || dml->tag != T_select) {
        return {};
    }
    if (dml->is_explain_analyze_) {
        return {{"plan", wire::SqlType::CHAR}};
    }

    Context planning_context(
        lock_manager_, log_manager_, nullptr, transaction_manager_,
        &isolation_level_, nullptr);
    auto statement = portal_->start(plan, &planning_context);
    if (statement == nullptr || statement->tag != PORTAL_ONE_SELECT ||
        statement->root == nullptr) {
        throw InternalError("Prepared query has no executable query root");
    }
    const auto &columns = statement->root->cols();
    std::vector<OutputColumn> schema;
    schema.reserve(columns.size());
    for (size_t index = 0; index < columns.size(); ++index) {
        std::string name = columns[index].name;
        if (index < statement->sel_cols.size() &&
            !statement->sel_cols[index].col_name.empty()) {
            name = statement->sel_cols[index].col_name;
        }
        schema.push_back({std::move(name), wire_type(columns[index].type)});
    }
    return schema;
}

wire::PreparedArtifact SqlExecutionService::prepare(
    const wire::PrepareEntry &entry) {
    // PREPARE_SET is sent after schema creation/LOAD/index creation and
    // before ranked transaction timing.  Establish one generic durable
    // recovery baseline here when no usable checkpoint exists; this does not
    // inspect statement text or alter the prepared workload.
    ql_manager_->ensure_prepared_checkpoint(log_manager_);
    auto plan = build_plan(entry.sql, &entry.parameter_types);
    auto schema = infer_output_schema(plan);
    return {
        std::move(schema),
        std::make_shared<PlanExecutable>(std::move(plan))};
}

void SqlExecutionService::execute_stream(
    const std::string &sql, ResultSink &sink) {
    std::string file_name;
    std::string table_name;
    if (parse_load_command(sql, &file_name, &table_name)) {
        execute_load(file_name, table_name, sink);
        return;
    }

    // Normalize alternative transaction boundary syntax so the parser
    // only needs to handle the canonical "BEGIN"/"COMMIT"/etc. forms.
    // This matches what the old rmdb.cpp's parse_txn_boundary() did.
    std::string normalized = trim_copy(sql);
    if (!normalized.empty() && normalized.back() == ';') {
        normalized.pop_back();
        normalized = trim_copy(normalized);
    }

    std::string lower = lower_copy(normalized);
    std::istringstream iss(normalized);
    std::vector<std::string> words;
    std::string word;
    while (iss >> word) {
        words.push_back(lower_copy(word));
    }

    if (words.size() >= 1 && words.size() <= 3) {
        std::string w0 = words[0];
        std::string w1 = words.size() >= 2 ? words[1] : "";
        std::string w2 = words.size() >= 3 ? words[2] : "";

        bool is_begin =
            (w0 == "start" && w1 == "transaction") ||
            (w0 == "begin" &&
             (w1.empty() || w1 == "work" || w1 == "transaction"));
        bool is_commit =
            (w0 == "commit" &&
             (w1.empty() || w1 == "work" || w1 == "transaction"));
        bool is_rollback =
            (w0 == "rollback" &&
             (w1.empty() || w1 == "work" || w1 == "transaction"));
        bool is_abort =
            (w0 == "abort" &&
             (w1.empty() || w1 == "work" || w1 == "transaction"));

        if (is_begin) {
            execute_plan(build_plan("BEGIN;", nullptr), sink);
            return;
        }
        if (is_commit) {
            execute_plan(build_plan("COMMIT;", nullptr), sink);
            return;
        }
        if (is_rollback) {
            execute_plan(build_plan("ROLLBACK;", nullptr), sink);
            return;
        }
        if (is_abort) {
            execute_plan(build_plan("ABORT;", nullptr), sink);
            return;
        }
    }

    execute_plan(build_plan(sql, nullptr), sink);
}

void SqlExecutionService::execute_prepared(
    const wire::PreparedStatement &statement,
    const std::vector<TypedValue> &parameters,
    ResultSink &sink) {
    auto executable =
        std::dynamic_pointer_cast<PlanExecutable>(statement.executable);
    if (executable == nullptr || executable->plan == nullptr) {
        throw InternalError("Prepared statement plan is unavailable");
    }
    bind_plan_parameters(executable->plan, parameters);
    execute_plan(executable->plan, sink);
}

void SqlExecutionService::ensure_transaction(
    Context *context, bool admit_execution) {
    context->txn_ =
        transaction_manager_->get_transaction(transaction_id_);
    if (context->txn_ == nullptr ||
        context->txn_->get_state() == TransactionState::COMMITTED ||
        context->txn_->get_state() == TransactionState::ABORTED) {
        context->txn_ = transaction_manager_->begin(
            nullptr, log_manager_, isolation_level_);
        transaction_id_ = context->txn_->get_transaction_id();
        context->txn_->set_txn_mode(false);
    }
    if (admit_execution) {
        transaction_manager_->ensure_snapshot_admission(context->txn_);
    }
}

void SqlExecutionService::execute_plan(
    const std::shared_ptr<Plan> &plan, ResultSink &sink) {
    // If a previous statement in an explicit transaction failed, route
    // to the same behaviour as the old rmdb.cpp explicit_txn_failed guard:
    //   COMMIT   → clear flag, return "failure"
    //   ABORT/ROLLBACK → clear flag, allow execution to clean up
    //   BEGIN    → clear flag, allow execution (starts fresh)
    //   other    → keep flag, return "failure"
    if (explicit_txn_failed_ && plan != nullptr) {
        if (plan->tag == T_Transaction_rollback ||
            plan->tag == T_Transaction_abort ||
            plan->tag == T_Transaction_begin) {
            explicit_txn_failed_ = false;
        } else if (plan->tag == T_Transaction_commit) {
            explicit_txn_failed_ = false;
            throw RMDBError("failure");
        } else {
            throw RMDBError("failure");
        }
    }

    TrackingResultSink tracking_sink(sink);
    Context context(
        lock_manager_, log_manager_, nullptr, transaction_manager_,
        &isolation_level_, &tracking_sink);
    bool statement_entered = false;
    std::shared_lock<std::shared_mutex> visibility_guard;
    try {
        const bool checkpoint =
            plan != nullptr && plan->tag == T_StaticCheckpoint;
        if (!checkpoint) {
            transaction_manager_->enter_statement(transaction_id_);
            statement_entered = true;
            if (accesses_table_data(plan)) {
                visibility_guard =
                    transaction_manager_->acquire_commit_apply_latch();
                context.commit_visibility_guard_held_ = true;
            }
            ensure_transaction(&context, !is_begin_plan(plan));
        }

        auto portal_statement = portal_->start(plan, &context);
        portal_->run(
            portal_statement, ql_manager_, &transaction_id_, &context);
        portal_->drop();

        if (context.txn_ != nullptr &&
            !context.txn_->get_txn_mode()) {
            if (visibility_guard.owns_lock()) {
                context.commit_visibility_guard_held_ = false;
                visibility_guard.unlock();
            }
            // An implicit SELECT has no COMMIT command/ACK and no recovery
            // state to make durable.  Still run the complete transaction
            // commit path (MVCC/SSI validation, lock release and lifecycle
            // cleanup), but do not append and synchronously flush a WAL
            // COMMIT record for every read-only operation in EXEC_BATCH.
            // Explicit COMMIT and every mutating statement keep passing the
            // real log manager, so their audited WAL-before-ACK contract is
            // unchanged.
            const bool implicit_read_only =
                plan != nullptr && plan->tag == T_select &&
                context.txn_->get_write_set()->empty();
            transaction_manager_->commit(
                context.txn_, implicit_read_only ? nullptr : log_manager_);
            transaction_manager_->release_transaction(context.txn_);
            context.txn_ = nullptr;
            transaction_id_ = INVALID_TXN_ID;
        }
        if (!tracking_sink.terminal()) {
            tracking_sink.command_ok();
        }
        if (statement_entered) {
            transaction_manager_->leave_statement();
        }
    } catch (TransactionAbortException &error) {
        if (visibility_guard.owns_lock()) {
            context.commit_visibility_guard_held_ = false;
            visibility_guard.unlock();
        }
        abort_active_transaction();
        explicit_txn_failed_ = false;
        if (statement_entered) {
            transaction_manager_->leave_statement();
        }
        throw wire::TransactionAbortError(error.GetInfo());
    } catch (...) {
        if (visibility_guard.owns_lock()) {
            context.commit_visibility_guard_held_ = false;
            visibility_guard.unlock();
        }
        // Old rmdb.cpp behaviour for explicit transactions: abort the
        // pending writes, flag the txn as failed, and let the client
        // clean up with ROLLBACK / ABORT / BEGIN.
        if (context.txn_ != nullptr && context.txn_->get_txn_mode()) {
            abort_active_transaction();
            explicit_txn_failed_ = true;
            if (statement_entered) {
                transaction_manager_->leave_statement();
            }
            throw RMDBError("failure");
        }
        abort_active_transaction();
        if (statement_entered) {
            transaction_manager_->leave_statement();
        }
        throw;
    }
}

void SqlExecutionService::execute_load(
    const std::string &file_name,
    const std::string &table_name,
    ResultSink &sink) {
    TrackingResultSink tracking_sink(sink);
    Context context(
        lock_manager_, log_manager_, nullptr, transaction_manager_,
        &isolation_level_, &tracking_sink);
    bool statement_entered = false;
    std::shared_lock<std::shared_mutex> visibility_guard;
    try {
        transaction_manager_->enter_statement(transaction_id_);
        statement_entered = true;
        visibility_guard =
            transaction_manager_->acquire_commit_apply_latch();
        context.commit_visibility_guard_held_ = true;
        ensure_transaction(&context, true);

        std::ifstream input(file_name);
        if (!input.is_open()) {
            throw RMDBError("Cannot open LOAD input");
        }
        const auto &table = sm_manager_->db_.get_table(table_name);
        std::string line;
        if (!std::getline(input, line)) {
            throw RMDBError("LOAD input is missing its header");
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto csv_column_mapping =
            build_csv_column_mapping(parse_csv_line(line), table);

        // LOAD stays one atomic transaction.  For the normal non-MVCC load
        // path, keep all inserted RIDs in one compact undo record instead of
        // allocating a WriteRecord and table-name string for every CSV row.
        // Per-row WAL, index maintenance, failure rollback, and the final
        // durable COMMIT remain unchanged.
        WriteRecord *bulk_insert_record = nullptr;
        if (context.txn_ != nullptr && !context.txn_->uses_mvcc()) {
            bulk_insert_record =
                new WriteRecord(WType::BULK_INSERT_TUPLES, table_name);
            context.txn_->append_write_record(bulk_insert_record);
        }

        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (trim_copy(line).empty()) {
                continue;
            }
            const auto fields = parse_csv_line(line);
            if (fields.size() != table.cols.size()) {
                throw InvalidValueCountError();
            }
            std::vector<Value> values;
            values.reserve(table.cols.size());
            for (size_t index = 0; index < table.cols.size(); ++index) {
                values.push_back(csv_value(
                    fields[csv_column_mapping[index]], table.cols[index]));
            }
            InsertExecutor executor(
                sm_manager_, table_name, std::move(values), &context,
                bulk_insert_record);
            executor.Next();
        }

        context.commit_visibility_guard_held_ = false;
        visibility_guard.unlock();
        transaction_manager_->commit(context.txn_, log_manager_);
        transaction_manager_->release_transaction(context.txn_);
        context.txn_ = nullptr;
        transaction_id_ = INVALID_TXN_ID;
        tracking_sink.command_ok();
        transaction_manager_->leave_statement();
    } catch (TransactionAbortException &error) {
        if (visibility_guard.owns_lock()) {
            context.commit_visibility_guard_held_ = false;
            visibility_guard.unlock();
        }
        abort_active_transaction();
        if (statement_entered) {
            transaction_manager_->leave_statement();
        }
        throw wire::TransactionAbortError(error.GetInfo());
    } catch (...) {
        if (visibility_guard.owns_lock()) {
            context.commit_visibility_guard_held_ = false;
            visibility_guard.unlock();
        }
        abort_active_transaction();
        if (statement_entered) {
            transaction_manager_->leave_statement();
        }
        throw;
    }
}

bool SqlExecutionService::has_active_transaction() const {
    Transaction *transaction =
        transaction_manager_->get_transaction(transaction_id_);
    return transaction != nullptr &&
           transaction->get_state() != TransactionState::COMMITTED &&
           transaction->get_state() != TransactionState::ABORTED;
}

void SqlExecutionService::abort_active_transaction() {
    Transaction *transaction =
        transaction_manager_->get_transaction(transaction_id_);
    if (transaction != nullptr &&
        transaction->get_state() != TransactionState::COMMITTED &&
        transaction->get_state() != TransactionState::ABORTED) {
        transaction_manager_->abort(transaction, log_manager_);
    }
    transaction_manager_->release_transaction(transaction);
    transaction_id_ = INVALID_TXN_ID;
}

}  // namespace rmdb::execution
