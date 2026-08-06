/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "execution_manager.h"
#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>
#include "common/config.h"
#include "execution_eval.h"
#include "executor_aggregation.h"
#include "executor_delete.h"
#include "executor_index_scan.h"
#include "executor_insert.h"
#include "executor_nestedloop_join.h"
#include "executor_projection.h"
#include "executor_seq_scan.h"
#include "executor_update.h"
#include "index/ix.h"
#include "execution/execution_result.h"

const char *help_info = "Supported SQL syntax:\n"
                   "  command ;\n"
                   "command:\n"
                   "  CREATE TABLE table_name (column_name type [, column_name type ...])\n"
                   "  DROP TABLE table_name\n"
                   "  CREATE INDEX table_name (column_name)\n"
                   "  DROP INDEX table_name (column_name)\n"
                   "  CREATE STATIC_CHECKPOINT\n"
                   "  INSERT INTO table_name VALUES (value [, value ...])\n"
                   "  DELETE FROM table_name [WHERE where_clause]\n"
                   "  UPDATE table_name SET column_name = value [, column_name = value ...] [WHERE where_clause]\n"
                   "  SELECT selector FROM table_name [WHERE where_clause]\n"
                   "type:\n"
                   "  {INT | FLOAT | CHAR(n)}\n"
                   "where_clause:\n"
                   "  condition [AND condition ...]\n"
                   "condition:\n"
                   "  column op {column | value}\n"
                   "column:\n"
                   "  [table_name.]column_name\n"
                   "op:\n"
                   "  {= | <> | < | > | <= | >=}\n"
                   "selector:\n"
                   "  {* | column [, column ...]}\n";

// 主要负责执行DDL语句
void QlManager::run_mutli_query(std::shared_ptr<Plan> plan, Context *context){
    if (auto x = std::dynamic_pointer_cast<DDLPlan>(plan)) {
        switch(x->tag) {
            case T_CreateTable:
            {
                sm_manager_->create_table(x->tab_name_, x->cols_, context);
                break;
            }
            case T_DropTable:
            {
                sm_manager_->drop_table(x->tab_name_, context);
                break;
            }
            case T_CreateIndex:
            {
                sm_manager_->create_index(x->tab_name_, x->tab_col_names_, context);
                break;
            }
            case T_DropIndex:
            {
                sm_manager_->drop_index(x->tab_name_, x->tab_col_names_, context);
                break;
            }
            default:
                throw InternalError("Unexpected field type");
                break;
        }
        // DDL is not represented in the data-change WAL.  Any previous
        // checkpoint remains a valid data baseline, but its catalog/index
        // snapshot is stale and must not suppress the next prepared-workload
        // checkpoint.
        checkpoint_available_.store(false, std::memory_order_release);
    }
}

void QlManager::create_static_checkpoint_internal(LogManager *log_manager) {
    if (log_manager == nullptr) {
        throw InternalError("Missing log manager for static checkpoint");
    }

    std::vector<txn_id_t> active_txns = txn_mgr_->begin_static_checkpoint();
    const char *checkpoint_stage = "applying committed MVCC deletes";
    try {
        // MVCC delete commits only drop index entries; the row's physical
        // slot survives until GC.  Apply those deletes before flushing the
        // checkpoint baseline so deleted rows cannot reappear after restart.
        txn_mgr_->apply_committed_deletes_for_checkpoint();
        checkpoint_stage = "writing checkpoint log";
        int64_t checkpoint_offset =
            log_manager->write_checkpoint_record(active_txns);
        checkpoint_stage = "flushing database pages";
        sm_manager_->flush_for_checkpoint();
        checkpoint_stage = "snapshotting indexes";
        sm_manager_->create_index_snapshots(checkpoint_offset);
        checkpoint_stage = "writing restart file";
        log_manager->persist_restart_offset(checkpoint_offset);
        checkpoint_stage = "cleaning old index snapshots";
        sm_manager_->cleanup_index_snapshots(checkpoint_offset);
    } catch (const std::exception &e) {
        std::cerr << "Static checkpoint failed while " << checkpoint_stage
                  << ": " << e.what() << std::endl;
        txn_mgr_->end_static_checkpoint();
        throw;
    } catch (...) {
        std::cerr << "Static checkpoint failed while " << checkpoint_stage
                  << ": unknown error" << std::endl;
        txn_mgr_->end_static_checkpoint();
        throw;
    }
    txn_mgr_->end_static_checkpoint();
}

void QlManager::create_static_checkpoint(LogManager *log_manager) {
    std::lock_guard<std::mutex> lock(checkpoint_create_latch_);
    create_static_checkpoint_internal(log_manager);
    checkpoint_available_.store(true, std::memory_order_release);
}

void QlManager::ensure_prepared_checkpoint(LogManager *log_manager) {
    if (checkpoint_available_.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard<std::mutex> lock(checkpoint_create_latch_);
    if (checkpoint_available_.load(std::memory_order_acquire)) {
        return;
    }
    create_static_checkpoint_internal(log_manager);
    checkpoint_available_.store(true, std::memory_order_release);
}

void QlManager::set_checkpoint_available(bool available) {
    checkpoint_available_.store(available, std::memory_order_release);
}

// 执行help; show tables; desc table; begin; commit; abort;语句
void QlManager::run_cmd_utility(std::shared_ptr<Plan> plan, txn_id_t *txn_id, Context *context) {
    if (auto x = std::dynamic_pointer_cast<OtherPlan>(plan)) {
        switch(x->tag) {
            case T_Help:
            {
                if (context->result_sink_ == nullptr) {
                    throw InternalError("Missing typed result sink");
                }
                context->result_sink_->begin_query(
                    {{"help", rmdb::wire::SqlType::CHAR}});
                context->result_sink_->push_row(
                    {rmdb::execution::TypedValue::Char(help_info)});
                context->result_sink_->end_query(1);
                break;
            }
            case T_ShowTable:
            {
                sm_manager_->show_tables(context);
                break;
            }
            case T_ShowIndex:
            {
                sm_manager_->show_index(x->tab_name_, context);
                break;
            }
            case T_DescTable:
            {
                sm_manager_->desc_table(x->tab_name_, context);
                break;
            }
            case T_StaticCheckpoint:
            {
                Transaction *current_txn = txn_mgr_->get_transaction(*txn_id);
                if (current_txn != nullptr &&
                    current_txn->get_state() == TransactionState::GROWING &&
                    current_txn->get_txn_mode()) {
                    throw RMDBError("CREATE STATIC_CHECKPOINT cannot run inside a transaction");
                }

                create_static_checkpoint(context->log_mgr_);
                break;
            }
            case T_Transaction_begin:
            {
                // 显示开启一个事务
                context->txn_->set_txn_mode(true);
                break;
            }  
            case T_Transaction_commit:
            {
                context->txn_ = txn_mgr_->get_transaction(*txn_id);
                context->txn_->set_txn_mode(false);
                txn_mgr_->commit(context->txn_, context->log_mgr_);
                txn_mgr_->release_transaction(context->txn_);
                context->txn_ = nullptr;
                *txn_id = INVALID_TXN_ID;
                break;
            }
            case T_Transaction_rollback:
            {
                context->txn_ = txn_mgr_->get_transaction(*txn_id);
                context->txn_->set_txn_mode(false);
                txn_mgr_->abort(context->txn_, context->log_mgr_);
                txn_mgr_->release_transaction(context->txn_);
                context->txn_ = nullptr;
                *txn_id = INVALID_TXN_ID;
                break;
            }
            case T_Transaction_abort:
            {
                context->txn_ = txn_mgr_->get_transaction(*txn_id);
                context->txn_->set_txn_mode(false);
                txn_mgr_->abort(context->txn_, context->log_mgr_);
                txn_mgr_->release_transaction(context->txn_);
                context->txn_ = nullptr;
                *txn_id = INVALID_TXN_ID;
                break;
            }
            default:
                throw InternalError("Unexpected field type");
                break;                        
        }

    } else if(auto x = std::dynamic_pointer_cast<SetKnobPlan>(plan)) {
        switch (x->set_knob_type_)
        {
        case ast::SetKnobType::EnableNestLoop: {
            planner_->set_enable_nestedloop_join(x->bool_value_);
            break;
        }
        case ast::SetKnobType::EnableSortMerge: {
            planner_->set_enable_sortmerge_join(x->bool_value_);
            break;
        }
        default: {
            throw RMDBError("Not implemented!\n");
            break;
        }
        }
    } else if (auto x = std::dynamic_pointer_cast<SetTransactionIsolationPlan>(plan)) {
        if (context->session_isolation_ == nullptr) {
            throw InternalError("Missing session isolation state");
        }
        *context->session_isolation_ =
            x->isolation_level_ == ast::SnapshotIsolation
                ? IsolationLevel::SNAPSHOT_ISOLATION
                : IsolationLevel::SERIALIZABLE;
    }
}

void QlManager::select_from(std::unique_ptr<AbstractExecutor> executorTreeRoot, std::vector<TabCol> sel_cols,
                            Context *context) {
    if (context->result_sink_ == nullptr) {
        throw InternalError("Missing typed result sink");
    }
    const auto &columns = executorTreeRoot->cols();
    std::vector<rmdb::execution::OutputColumn> schema;
    schema.reserve(columns.size());
    for (size_t index = 0; index < columns.size(); ++index) {
        std::string name = columns[index].name;
        if (index < sel_cols.size() && !sel_cols[index].col_name.empty()) {
            name = sel_cols[index].col_name;
        }
        rmdb::wire::SqlType type;
        switch (columns[index].type) {
            case TYPE_INT:
                type = rmdb::wire::SqlType::INT32;
                break;
            case TYPE_FLOAT:
                type = rmdb::wire::SqlType::FLOAT32;
                break;
            case TYPE_STRING:
                type = rmdb::wire::SqlType::CHAR;
                break;
            default:
                throw InternalError("Unexpected result column type");
        }
        schema.push_back({std::move(name), type});
    }
    context->result_sink_->begin_query(schema);

    uint64_t num_rec = 0;
    for (executorTreeRoot->beginTuple(); !executorTreeRoot->is_end(); executorTreeRoot->nextTuple()) {
        auto tuple = executorTreeRoot->Next();
        std::vector<rmdb::execution::TypedValue> row;
        row.reserve(columns.size());
        for (const auto &column : columns) {
            const char *data = tuple->data + column.offset;
            switch (column.type) {
                case TYPE_INT: {
                    int32_t value = 0;
                    memcpy(&value, data, sizeof(value));
                    row.push_back(
                        rmdb::execution::TypedValue::Int32(value));
                    break;
                }
                case TYPE_FLOAT: {
                    float value = 0.0F;
                    memcpy(&value, data, sizeof(value));
                    row.push_back(
                        rmdb::execution::TypedValue::Float(value));
                    break;
                }
                case TYPE_STRING:
                    row.push_back(rmdb::execution::TypedValue::Char(
                        rmdb::execution::logical_char_bytes(
                            data, static_cast<size_t>(column.len))));
                    break;
                default:
                    throw InternalError("Unexpected result column type");
            }
        }
        context->result_sink_->push_row(row);
        num_rec++;
    }
    context->result_sink_->end_query(num_rec);
}

static void reset_plan_rows(std::shared_ptr<Plan> plan) {
    if (plan == nullptr) {
        return;
    }

    plan->rows_ = 0;

    if (auto x = std::dynamic_pointer_cast<DMLPlan>(plan)) {
        reset_plan_rows(x->subplan_);
    } else if (auto x = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
        reset_plan_rows(x->subplan_);
    } else if (auto x = std::dynamic_pointer_cast<FilterPlan>(plan)) {
        reset_plan_rows(x->subplan_);
    } else if (auto x = std::dynamic_pointer_cast<JoinPlan>(plan)) {
        reset_plan_rows(x->left_);
        reset_plan_rows(x->right_);
    } else if (auto x = std::dynamic_pointer_cast<SortPlan>(plan)) {
        reset_plan_rows(x->subplan_);
    } else if (auto x = std::dynamic_pointer_cast<AggregatePlan>(plan)) {
        reset_plan_rows(x->subplan_);
    } else if (auto x = std::dynamic_pointer_cast<UnionPlan>(plan)) {
        for (auto &branch : x->branches_) {
            reset_plan_rows(branch);
        }
    }
}
//操作符格式化
static std::string op_to_string(CompOp op) {
    switch (op) {
        case OP_EQ: return "=";
        case OP_NE: return "<>";
        case OP_LT: return "<";
        case OP_GT: return ">";
        case OP_LE: return "<=";
        case OP_GE: return ">=";
    }
    return "";
}
//值格式化
static std::string value_to_string(const Value &val) {
    if (val.type == TYPE_INT) {
        return std::to_string(val.int_val);
    }
    if (val.type == TYPE_FLOAT) {
        if (val.from_float_literal) {
            return std::to_string(val.float_val);
        }
        return format_float_output(val.float_val, false);
    }
    return "'" + val.str_val + "'";
}

//列格式化
//已实现按别名打印
static std::string col_to_string(
    const TabCol &col,
    const std::map<std::string, std::string> &table_to_alias) {
    auto it = table_to_alias.find(col.tab_name);
    std::string tab = (it == table_to_alias.end()) ? col.tab_name : it->second;
    return tab + "." + col.col_name;
}

//条件格式化
static std::string condition_to_string(
    const Condition &cond,
    const std::map<std::string, std::string> &table_to_alias) {
    std::string s = col_to_string(cond.lhs_col, table_to_alias);
    s += op_to_string(cond.op);
    if (cond.is_rhs_val) {
        s += value_to_string(cond.rhs_val);
    } else {
        s += col_to_string(cond.rhs_col, table_to_alias);
    }
    return s;
}

//字符串拼接
static std::string join_sorted_strings(std::vector<std::string> vals) {
    std::sort(vals.begin(), vals.end());
    std::string out;
    for (size_t i = 0; i < vals.size(); i++) {
        if (i != 0) {
            out += ", ";
        }
        out += vals[i];
    }
    return out;
}

// col
static std::string format_columns(
    const std::vector<TabCol> &cols,
    bool display_all,
    const std::map<std::string, std::string> &table_to_alias) {
    if (display_all) {
        return "*";
    }

    std::vector<std::string> vals;
    for (auto &col : cols) {
        vals.push_back(col_to_string(col, table_to_alias));
    }
    return join_sorted_strings(vals);
}


//join condition
static std::string format_conditions(
    const std::vector<Condition> &conds,
    const std::map<std::string, std::string> &table_to_alias) {
    std::vector<std::string> vals;
    for (auto &cond : conds) {
        vals.push_back(condition_to_string(cond, table_to_alias ));
    }
    return join_sorted_strings(vals);
}

//table
static void collect_tables(std::shared_ptr<Plan> plan, std::set<std::string> &tables) {
    if (auto x = std::dynamic_pointer_cast<ScanPlan>(plan)) {
        tables.insert(x->tab_name_);
    } else if (auto x = std::dynamic_pointer_cast<FilterPlan>(plan)) {
        collect_tables(x->subplan_, tables);
    } else if (auto x = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
        collect_tables(x->subplan_, tables);
    } else if (auto x = std::dynamic_pointer_cast<JoinPlan>(plan)) {
        collect_tables(x->left_, tables);
        collect_tables(x->right_, tables);
    }
}
static std::string format_tables(std::shared_ptr<Plan> plan) {
    std::set<std::string> table_set;
    collect_tables(plan, table_set);
    std::vector<std::string> vals(table_set.begin(), table_set.end());
    return join_sorted_strings(vals);
}

static std::string format_select_items(const std::vector<SelectItem> &items) {
    std::vector<std::string> vals;
    for (auto &item : items) {
        if (item.is_agg) {
            std::string name;
            switch (item.agg.type) {
                case AGG_COUNT: name = "COUNT"; break;
                case AGG_MAX: name = "MAX"; break;
                case AGG_MIN: name = "MIN"; break;
                case AGG_SUM: name = "SUM"; break;
                case AGG_AVG: name = "AVG"; break;
                default: name = "AGG"; break;
            }
            if (item.agg.is_star) {
                vals.push_back(name + "(*)");
            } else {
                vals.push_back(
                    name + "(" +
                    (item.agg.is_distinct ? "DISTINCT " : "") +
                    item.agg.col.tab_name + "." +
                    item.agg.col.col_name + ")");
            }
        } else {
            vals.push_back(item.col.tab_name + "." + item.col.col_name);
        }
    }
    return join_sorted_strings(vals);
}


// tree display
static void append_plan_tree(std::ostringstream &out, std::shared_ptr<Plan> plan, int depth,
    const std::map<std::string, std::string> &table_to_alias) {
    std::string indent(depth, '\t');

    if (auto x = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
        out << indent
            << "Project(columns=["
            << format_columns(x->sel_cols_, x->display_all_, table_to_alias)
            << "], rows="
            << x->rows_
            << ")\n";
        append_plan_tree(out, x->subplan_, depth + 1, table_to_alias);
        return;
    }

    if (auto x = std::dynamic_pointer_cast<FilterPlan>(plan)) {
        out << indent
            << "Filter(condition=["
            << format_conditions(x->conds_, table_to_alias)
            << "], rows="
            << x->rows_
            << ")\n";
        append_plan_tree(out, x->subplan_, depth + 1, table_to_alias );
        return;
    }

    if (auto x = std::dynamic_pointer_cast<ScanPlan>(plan)) {
        out << indent
            << "Scan(table="
            << x->tab_name_;
        if (x->tag == T_IndexScan) {
            out << ", type=IndexScan, using_index=("
                << join_sorted_strings(x->index_col_names_)
                << "), rows="
                << x->rows_
                << ")\n";
        } else {
            out << ", type=SeqScan, rows="
                << x->rows_
                << ")\n";
        }
        return;
    }

    if (auto x = std::dynamic_pointer_cast<JoinPlan>(plan)) {
        out << indent
            << "Join(tables=["
            << format_tables(plan)
            << "], condition=["
            << format_conditions(x->conds_, table_to_alias)
            << "], rows="
            << x->rows_
            << ")\n";
        append_plan_tree(out, x->left_, depth + 1, table_to_alias);
        append_plan_tree(out, x->right_, depth + 1, table_to_alias);
        return;
    }

    if (auto x = std::dynamic_pointer_cast<SortPlan>(plan)) {
        out << indent
            << "Sort(column="
            << col_to_string(x->sel_col_, table_to_alias)
            << ", order="
            << (x->is_desc_ ? "DESC" : "ASC")
            << ", rows="
            << x->rows_
            << ")\n";
        append_plan_tree(out, x->subplan_, depth + 1, table_to_alias);
        return;
    }

    if (auto x = std::dynamic_pointer_cast<AggregatePlan>(plan)) {
        out << indent
            << "Aggregate(columns=["
            << format_select_items(x->select_items_)
            << "], rows="
            << x->rows_
            << ")\n";
        append_plan_tree(out, x->subplan_, depth + 1, table_to_alias);
        return;
    }

    if (auto x = std::dynamic_pointer_cast<UnionPlan>(plan)) {
        out << indent << "Union(branches=" << x->branches_.size() << ", rows=" << x->rows_ << ")\n";
        for (auto &branch : x->branches_) {
            append_plan_tree(out, branch, depth + 1, table_to_alias);
        }
        return;
    }
}

void QlManager::explain_analyze(std::unique_ptr<AbstractExecutor> executorTreeRoot,
                                std::shared_ptr<Plan> plan,
                                Context *context) {
    auto dml = std::dynamic_pointer_cast<DMLPlan>(plan);
    if (dml == nullptr || dml->subplan_ == nullptr) {
        return;
    }

    std::shared_ptr<Plan> root_plan = dml->subplan_;

    reset_plan_rows(root_plan);

    for (executorTreeRoot->beginTuple();
         !executorTreeRoot->is_end();
         executorTreeRoot->nextTuple()) {
        auto tuple = executorTreeRoot->Next();
    }

    std::ostringstream oss;
    append_plan_tree(oss, root_plan, 0, dml->table_to_alias_);
    std::string output = oss.str();

    if (context->result_sink_ == nullptr) {
        throw InternalError("Missing typed result sink");
    }
    context->result_sink_->begin_query(
        {{"plan", rmdb::wire::SqlType::CHAR}});
    context->result_sink_->push_row(
        {rmdb::execution::TypedValue::Char(output)});
    context->result_sink_->end_query(1);
}


// 执行DML语句
void QlManager::run_dml(std::unique_ptr<AbstractExecutor> exec){
    exec->Next();
}
