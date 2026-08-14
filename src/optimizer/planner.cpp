/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "planner.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "execution/executor_delete.h"
#include "execution/executor_index_scan.h"
#include "execution/executor_insert.h"
#include "execution/executor_nestedloop_join.h"
#include "execution/executor_projection.h"
#include "execution/executor_seq_scan.h"
#include "execution/executor_update.h"
#include "index/ix.h"

namespace {

CompOp swap_comparison(CompOp op) {
    switch (op) {
        case OP_LT:
            return OP_GT;
        case OP_GT:
            return OP_LT;
        case OP_LE:
            return OP_GE;
        case OP_GE:
            return OP_LE;
        default:
            return op;
    }
}

bool same_column(const TabCol &left, const TabCol &right) {
    return left.tab_name == right.tab_name &&
           left.col_name == right.col_name;
}

std::string column_identity(const TabCol &column) {
    return column.tab_name + "\x1f" + column.col_name;
}

void append_number(std::string *key, uint64_t value) {
    key->append(reinterpret_cast<const char *>(&value), sizeof(value));
}

std::string value_identity(const Value &value) {
    std::string key;
    append_number(&key, static_cast<uint64_t>(value.type));
    append_number(&key, value.is_param ? 1 : 0);
    if (value.is_param) {
        append_number(&key, value.param_index);
        append_number(&key,
                      static_cast<uint64_t>(value.parameter_declared_type));
        return key;
    }
    if (value.raw != nullptr) {
        append_number(&key, static_cast<uint64_t>(value.raw->size));
        key.append(value.raw->data, static_cast<size_t>(value.raw->size));
        return key;
    }
    switch (value.type) {
        case TYPE_INT:
            key.append(reinterpret_cast<const char *>(&value.int_val),
                       sizeof(value.int_val));
            break;
        case TYPE_FLOAT:
            key.append(reinterpret_cast<const char *>(&value.float_val),
                       sizeof(value.float_val));
            break;
        case TYPE_STRING:
            append_number(&key, value.str_val.size());
            key.append(value.str_val);
            break;
        default:
            break;
    }
    return key;
}

std::string condition_identity(const Condition &condition) {
    std::string key = column_identity(condition.lhs_col);
    key.push_back(static_cast<char>(condition.op));
    key.push_back(condition.is_rhs_val ? '\x01' : '\x00');
    if (condition.is_rhs_val) {
        key.append(value_identity(condition.rhs_val));
    } else {
        key.append(column_identity(condition.rhs_col));
    }
    return key;
}

void normalize_and_deduplicate(std::vector<Condition> *conditions) {
    std::unordered_set<std::string> seen;
    std::vector<Condition> normalized;
    normalized.reserve(conditions->size());
    for (Condition condition : *conditions) {
        if (!condition.is_rhs_val &&
            condition.rhs_col < condition.lhs_col) {
            std::swap(condition.lhs_col, condition.rhs_col);
            condition.op = swap_comparison(condition.op);
        }
        if (seen.insert(condition_identity(condition)).second) {
            normalized.push_back(std::move(condition));
        }
    }
    *conditions = std::move(normalized);
}

const ColMeta *find_query_column(SmManager *sm_manager, const Query &query,
                                 const TabCol &column) {
    auto derived = query.derived_tables.find(column.tab_name);
    if (derived != query.derived_tables.end()) {
        auto found = std::find_if(
            derived->second.cols.begin(), derived->second.cols.end(),
            [&](const ColMeta &meta) { return meta.name == column.col_name; });
        return found == derived->second.cols.end() ? nullptr : &*found;
    }
    if (!sm_manager->db_.is_table(column.tab_name)) {
        return nullptr;
    }
    const TabMeta &table = sm_manager->db_.get_table(column.tab_name);
    auto found = std::find_if(
        table.cols.begin(), table.cols.end(),
        [&](const ColMeta &meta) { return meta.name == column.col_name; });
    return found == table.cols.end() ? nullptr : &*found;
}

class EqualityClasses {
   public:
    void unite(const TabCol &left, const TabCol &right) {
        const std::string left_key = column_identity(left);
        const std::string right_key = column_identity(right);
        columns_.emplace(left_key, left);
        columns_.emplace(right_key, right);
        const std::string left_root = find(left_key);
        const std::string right_root = find(right_key);
        if (left_root != right_root) {
            parent_[right_root] = left_root;
        }
    }

    std::string root(const TabCol &column) {
        const std::string key = column_identity(column);
        columns_.emplace(key, column);
        return find(key);
    }

    std::vector<TabCol> members(const std::string &root_key) {
        std::vector<TabCol> result;
        for (const auto &[key, column] : columns_) {
            if (find(key) == root_key) {
                result.push_back(column);
            }
        }
        return result;
    }

   private:
    std::string find(const std::string &key) {
        auto [it, inserted] = parent_.emplace(key, key);
        if (inserted || it->second == key) {
            return key;
        }
        it->second = find(it->second);
        return it->second;
    }

    std::unordered_map<std::string, std::string> parent_;
    std::unordered_map<std::string, TabCol> columns_;
};

void propagate_equal_literals(SmManager *sm_manager, Query *query) {
    EqualityClasses classes;
    for (const Condition &condition : query->conds) {
        if (!condition.is_rhs_val && condition.op == OP_EQ) {
            classes.unite(condition.lhs_col, condition.rhs_col);
        }
    }

    const std::vector<Condition> original = query->conds;
    for (const Condition &literal : original) {
        if (!literal.is_rhs_val || literal.op != OP_EQ) {
            continue;
        }
        const ColMeta *source =
            find_query_column(sm_manager, *query, literal.lhs_col);
        if (source == nullptr) {
            continue;
        }
        const std::string root = classes.root(literal.lhs_col);
        for (const TabCol &target : classes.members(root)) {
            if (same_column(target, literal.lhs_col)) {
                continue;
            }
            const ColMeta *target_meta =
                find_query_column(sm_manager, *query, target);
            if (target_meta == nullptr || target_meta->type != source->type ||
                target_meta->len != source->len) {
                continue;
            }
            Condition inferred = literal;
            inferred.lhs_col = target;
            query->conds.push_back(std::move(inferred));
        }
    }
    normalize_and_deduplicate(&query->conds);
}

bool literal_condition_on(const Condition &condition,
                          const std::string &table,
                          const std::string &column,
                          bool equality_only) {
    if (!condition.is_rhs_val || condition.lhs_col.tab_name != table ||
        condition.lhs_col.col_name != column || condition.op == OP_NE) {
        return false;
    }
    return !equality_only || condition.op == OP_EQ;
}

bool join_binds_column(const Condition &condition,
                       const std::string &table,
                       const std::string &column,
                       const std::set<std::string> &joined) {
    if (condition.is_rhs_val || condition.op != OP_EQ) {
        return false;
    }
    if (condition.lhs_col.tab_name == table &&
        condition.lhs_col.col_name == column) {
        return joined.count(condition.rhs_col.tab_name) != 0;
    }
    if (condition.rhs_col.tab_name == table &&
        condition.rhs_col.col_name == column) {
        return joined.count(condition.lhs_col.tab_name) != 0;
    }
    return false;
}

int best_index_prefix(SmManager *sm_manager, const Query &query,
                      const std::string &table,
                      const std::set<std::string> &joined) {
    if (!sm_manager->db_.is_table(table)) {
        return 0;
    }
    const TabMeta &meta = sm_manager->db_.get_table(table);
    int best = 0;
    for (const IndexMeta &index : meta.indexes) {
        int prefix = 0;
        for (const ColMeta &column : index.cols) {
            const bool equality = std::any_of(
                query.conds.begin(), query.conds.end(),
                [&](const Condition &condition) {
                    return literal_condition_on(condition, table, column.name,
                                                true) ||
                           join_binds_column(condition, table, column.name,
                                             joined);
                });
            if (equality) {
                ++prefix;
                continue;
            }
            const bool range = std::any_of(
                query.conds.begin(), query.conds.end(),
                [&](const Condition &condition) {
                    return literal_condition_on(condition, table, column.name,
                                                false);
                });
            if (range) {
                ++prefix;
            }
            break;
        }
        best = std::max(best, prefix);
    }
    return best;
}

int local_predicate_count(const Query &query, const std::string &table) {
    return static_cast<int>(std::count_if(
        query.conds.begin(), query.conds.end(),
        [&](const Condition &condition) {
            return condition.lhs_col.tab_name == table &&
                   (condition.is_rhs_val ||
                    condition.rhs_col.tab_name == table);
        }));
}

bool connected_to_joined(const Query &query, const std::string &table,
                         const std::set<std::string> &joined) {
    return std::any_of(
        query.conds.begin(), query.conds.end(),
        [&](const Condition &condition) {
            if (condition.is_rhs_val) {
                return false;
            }
            return (condition.lhs_col.tab_name == table &&
                    joined.count(condition.rhs_col.tab_name) != 0) ||
                   (condition.rhs_col.tab_name == table &&
                    joined.count(condition.lhs_col.tab_name) != 0);
        });
}

void reorder_inner_joins(SmManager *sm_manager, Query *query) {
    if (query->tables.size() < 2) {
        return;
    }
    const std::vector<std::string> original = query->tables;
    std::vector<bool> used(original.size(), false);
    std::vector<std::string> ordered;
    std::set<std::string> joined;
    ordered.reserve(original.size());

    while (ordered.size() < original.size()) {
        size_t best_index = original.size();
        int best_score = std::numeric_limits<int>::min();
        for (size_t index = 0; index < original.size(); ++index) {
            if (used[index]) {
                continue;
            }
            const std::string &table = original[index];
            const bool connected =
                ordered.empty() || connected_to_joined(*query, table, joined);
            const int score =
                (connected ? 100000 : 0) +
                best_index_prefix(sm_manager, *query, table, joined) * 1000 +
                local_predicate_count(*query, table) * 10 -
                static_cast<int>(index);
            if (score > best_score) {
                best_score = score;
                best_index = index;
            }
        }
        if (best_index == original.size()) {
            break;
        }
        used[best_index] = true;
        ordered.push_back(original[best_index]);
        joined.insert(original[best_index]);
    }
    if (ordered.size() == original.size()) {
        query->tables = std::move(ordered);
    }
}

}  // namespace

// 最左前缀匹配：选择能连续匹配最多索引列的索引
bool Planner::get_index_cols(std::string tab_name, std::vector<Condition> curr_conds, std::vector<std::string>& index_col_names) {
    index_col_names.clear();
    TabMeta& tab = sm_manager_->db_.get_table(tab_name);
    const IndexMeta* best_index = nullptr;
    int best_score = 0;
    for (const auto& index : tab.indexes) {
        int prefix = 0;
        int equality_prefix = 0;
        for (const auto& index_col : index.cols) {
            const bool has_equality = std::any_of(
                curr_conds.begin(), curr_conds.end(),
                [&](const Condition &condition) {
                    return literal_condition_on(condition, tab_name,
                                                index_col.name, true);
                });
            if (has_equality) {
                ++prefix;
                ++equality_prefix;
                continue;
            }
            const bool has_range = std::any_of(
                curr_conds.begin(), curr_conds.end(),
                [&](const Condition &condition) {
                    return literal_condition_on(condition, tab_name,
                                                index_col.name, false);
                });
            if (has_range) {
                ++prefix;
            }
            break;
        }
        const int score = equality_prefix * 100 + prefix;
        if (score > best_score) {
            best_score = score;
            best_index = &index;
        }
    }
    if (!best_index || best_score == 0) {
        return false;
    }
    for (const auto& col : best_index->cols) {
        index_col_names.push_back(col.name);
    }
    return true;
}

/**
 * @brief 表算子条件谓词生成
 *
 * @param conds 条件
 * @param tab_names 表名
 * @return std::vector<Condition>
 */
std::vector<Condition> pop_conds(std::vector<Condition> &conds, std::string tab_names) {
    std::vector<Condition> solved_conds;
    auto it = conds.begin();
    while (it != conds.end()) {
        bool lhs_matches = (it->lhs_col.tab_name == tab_names);
        bool rhs_matches = (!it->is_rhs_val && it->rhs_col.tab_name == tab_names);
        bool single_table_value_cond = lhs_matches && it->is_rhs_val;
        bool single_table_col_cond = lhs_matches && rhs_matches;
        if (single_table_value_cond || single_table_col_cond) {
            solved_conds.emplace_back(std::move(*it));
            it = conds.erase(it);
        } else {
            it++;
        }
    }
    return solved_conds;
}

int push_conds(Condition *cond, std::shared_ptr<Plan> plan)
{
    if(auto x = std::dynamic_pointer_cast<ScanPlan>(plan))
    {
        if(x->tab_name_.compare(cond->lhs_col.tab_name) == 0) {
            return 1;
        } else if(x->tab_name_.compare(cond->rhs_col.tab_name) == 0){
            return 2;
        } else {
            return 0;
        }
    }
    // 如果当前节点是 FilterPlan，就继续往它的子节点找
    // 如果当前节点是 ProjectionPlan，也继续往它的子节点找
    else if(auto x = std::dynamic_pointer_cast<FilterPlan>(plan))
    {
        return push_conds(cond, x->subplan_);
    }
    else if(auto x = std::dynamic_pointer_cast<ProjectionPlan>(plan))
    {
        return push_conds(cond, x->subplan_);
    }
    else if(auto x = std::dynamic_pointer_cast<UnionPlan>(plan))
    {
        for (auto &branch : x->branches_) {
            if (push_conds(cond, branch) != 0) {
                return 0;
            }
        }
        return 0;
    }
    else if(auto x = std::dynamic_pointer_cast<JoinPlan>(plan))
    {
        int left_res = push_conds(cond, x->left_);
        // 条件已经下推到左子节点
        if(left_res == 3){
            return 3;
        }
        int right_res = push_conds(cond, x->right_);
        // 条件已经下推到右子节点
        if(right_res == 3){
            return 3;
        }
        // 左子节点或右子节点有一个没有匹配到条件的列
        if(left_res == 0 || right_res == 0) {
            return left_res + right_res;
        }
        // 左子节点匹配到条件的右边
        if(left_res == 2) {
            // 需要将左右两边的条件变换位置
            std::map<CompOp, CompOp> swap_op = {
                {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
            };
            std::swap(cond->lhs_col, cond->rhs_col);
            cond->op = swap_op.at(cond->op);
        }
        x->conds_.emplace_back(std::move(*cond));
        return 3;
    }
    return false;
}


std::string get_plan_table_name(std::shared_ptr<Plan> plan) {
    if (auto x = std::dynamic_pointer_cast<ScanPlan>(plan)) {
        return x->tab_name_;
    }
    if (auto x = std::dynamic_pointer_cast<FilterPlan>(plan)) {
        return get_plan_table_name(x->subplan_);
    }
    if (auto x = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
        return get_plan_table_name(x->subplan_);
    }
    if (auto x = std::dynamic_pointer_cast<UnionPlan>(plan)) {
        return "";
    }
    return "";
}

static ScanPlan *get_base_scan(const std::shared_ptr<Plan> &plan) {
    if (auto scan = std::dynamic_pointer_cast<ScanPlan>(plan)) {
        return scan.get();
    }
    if (auto filter = std::dynamic_pointer_cast<FilterPlan>(plan)) {
        return get_base_scan(filter->subplan_);
    }
    if (auto project = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
        return get_base_scan(project->subplan_);
    }
    return nullptr;
}

static bool select_inner_join_index(SmManager *sm_manager,
                                    const std::string &inner_table,
                                    const std::vector<Condition> &join_conds,
                                    const std::shared_ptr<Plan> &inner_plan) {
    ScanPlan *scan = get_base_scan(inner_plan);
    if (scan == nullptr) {
        return false;
    }

    // Collect which inner-table columns are provided by join EQ conditions.
    std::set<std::string> join_inner_cols;
    for (const auto &cond : join_conds) {
        if (cond.is_rhs_val || cond.op != OP_EQ) {
            continue;
        }
        if (cond.lhs_col.tab_name == inner_table) {
            join_inner_cols.insert(cond.lhs_col.col_name);
        } else if (cond.rhs_col.tab_name == inner_table) {
            join_inner_cols.insert(cond.rhs_col.col_name);
        }
    }
    if (join_inner_cols.empty()) {
        return false;
    }

    TabMeta &tab = sm_manager->db_.get_table(inner_table);

    // First, try a single-column index on any join column (fast path).
    for (const auto &col_name : join_inner_cols) {
        std::vector<std::string> idx{col_name};
        if (tab.is_index(idx)) {
            scan->tag = T_IndexScan;
            scan->index_col_names_ = std::move(idx);
            return true;
        }
    }

    // Second, score every composite index by the length of its continuous
    // prefix covered by (join_columns ∪ literal_EQ_conditions).  Prefer the
    // longest prefix; on a tie prefer fewer total columns.
    const IndexMeta *best_index = nullptr;
    int best_prefix = 0;
    int best_total = 0;

    for (const auto &index : tab.indexes) {
        int prefix = 0;
        for (const auto &index_col : index.cols) {
            if (join_inner_cols.count(index_col.name)) {
                prefix++;
                continue;
            }
            bool has_literal_eq = std::any_of(
                scan->conds_.begin(), scan->conds_.end(),
                [&](const Condition &scan_cond) {
                    return scan_cond.is_rhs_val &&
                           scan_cond.op == OP_EQ &&
                           scan_cond.lhs_col.tab_name == inner_table &&
                           scan_cond.lhs_col.col_name == index_col.name;
                });
            if (!has_literal_eq) {
                break;
            }
            prefix++;
        }
        if (prefix == 0) {
            continue;
        }
        int total = static_cast<int>(index.cols.size());
        if (prefix > best_prefix ||
            (prefix == best_prefix && total < best_total)) {
            best_prefix = prefix;
            best_total = total;
            best_index = &index;
        }
    }

    if (best_index == nullptr) {
        return false;
    }

    scan->tag = T_IndexScan;
    scan->index_col_names_.clear();
    for (const auto &index_col : best_index->cols) {
        scan->index_col_names_.push_back(index_col.name);
    }
    return true;
}

std::shared_ptr<Plan> pop_scan(int *scantbl,
                               std::string table,
                               std::vector<std::string> &joined_tables,
                               std::vector<std::shared_ptr<Plan>> plans)
{
    for (size_t i = 0; i < plans.size(); i++) {
        std::string tab_name = get_plan_table_name(plans[i]);
        if (tab_name.compare(table) == 0)
        {
            scantbl[i] = 1;
            joined_tables.emplace_back(tab_name);
            return plans[i];
        }
    }
    return nullptr;
}


std::shared_ptr<Query> Planner::logical_optimization(std::shared_ptr<Query> query, Context *context)
{
    (void)context;
    if (query == nullptr) {
        throw InternalError("Cannot optimize a null query");
    }
    normalize_and_deduplicate(&query->conds);
    propagate_equal_literals(sm_manager_, query.get());
    reorder_inner_joins(sm_manager_, query.get());
    return query;
}

std::shared_ptr<Plan> Planner::physical_optimization(std::shared_ptr<Query> query, Context *context)
{
    std::shared_ptr<Plan> plan = make_one_rel(query);
    
    // 其他物理优化

    // 非聚合查询由 SortExecutor 处理 order by；
    // 聚合查询在 AggregationExecutor 内部按分组结果排序，避免先排序后聚合导致语义偏差。
    if (!(query->has_agg || !query->group_bys.empty() || !query->havings.empty())) {
        plan = generate_sort_plan(query, std::move(plan));
    }

    return plan;
}



std::shared_ptr<Plan> Planner::generate_subquery_plan(std::shared_ptr<Query> query) {
    std::shared_ptr<Plan> plan = make_one_rel(query);
    if (query->has_agg || !query->group_bys.empty() || !query->havings.empty()) {
        return std::make_shared<AggregatePlan>(
            std::move(plan), query->select_items, query->group_bys,
            query->havings, query->order_bys, query->limit_num);
    }
    return std::make_shared<ProjectionPlan>(
        T_Projection, std::move(plan), query->cols, query->is_select_all, -1);
}

std::shared_ptr<Plan> Planner::make_one_rel(std::shared_ptr<Query> query)
{
    
    auto x = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse);
    std::vector<std::string> tables = query->tables;
    std::map<std::string, std::vector<TabCol>> table_proj_cols;
    if (!query->is_select_all) {
    for (auto &col : query->cols) {
            table_proj_cols[col.tab_name].push_back(col);
        }
    }//加入select列只把跨表条件，也就是 Join 条件列加入局部 Project。单表 Filter 条件列不用加入，因为 Filter 在 Project 下面已经执行完了
    for (auto &cond : query->conds) {
        if (!cond.is_rhs_val && cond.lhs_col.tab_name != cond.rhs_col.tab_name) {
            table_proj_cols[cond.lhs_col.tab_name].push_back(cond.lhs_col);
            table_proj_cols[cond.rhs_col.tab_name].push_back(cond.rhs_col);
        }
    }
    //加入where条件列
    auto dedup_cols = [](std::vector<TabCol> &cols) {
    std::vector<TabCol> out;
        //去重
    for (auto &col : cols) {
        bool exists = false;

        for (auto &old : out) {
            if (old.tab_name == col.tab_name && old.col_name == col.col_name) {
                exists = true;
                break;
            }
        }

        if (!exists) {
            out.push_back(col);
        }
    }

    cols = std::move(out);
    };

    for (auto &kv : table_proj_cols) {
        dedup_cols(kv.second);
    }
    // // Scan table , 生成表算子列表tab_nodes
    std::vector<std::shared_ptr<Plan>> table_scan_executors(tables.size());
    for (size_t i = 0; i < tables.size(); i++) {
    auto curr_conds = pop_conds(query->conds, tables[i]);

    if (query->derived_tables.count(tables[i])) {
        auto &info = query->derived_tables.at(tables[i]);
        std::shared_ptr<Plan> derived_plan;
        if (!info.is_union_table) {
            if (info.branch_queries.size() != 1) {
                throw InternalError("Unexpected derived subquery plan");
            }
            derived_plan = generate_subquery_plan(info.branch_queries[0]);
            if (!curr_conds.empty()) {
                derived_plan = std::make_shared<FilterPlan>(derived_plan, curr_conds);
            }
            table_scan_executors[i] = derived_plan;
            continue;
        }
        std::vector<std::shared_ptr<Plan>> branch_plans;
        for (auto &branch_query : info.branch_queries) {
            branch_plans.push_back(generate_subquery_plan(branch_query));
        }
        derived_plan = std::make_shared<UnionPlan>(std::move(branch_plans), info.cols);
        if (!curr_conds.empty()) {
            derived_plan = std::make_shared<FilterPlan>(derived_plan, curr_conds);
        }
        table_scan_executors[i] = derived_plan;
        continue;
    }

    std::vector<std::string> index_col_names;
    bool index_exist = get_index_cols(tables[i], curr_conds, index_col_names);
        //. 在 Scan / Filter 上方包 ProjectPlan
    std::shared_ptr<Plan> scan;
    if (index_exist == false) {
        index_col_names.clear();
        scan = std::make_shared<ScanPlan>(
            T_SeqScan,
            sm_manager_,
            tables[i],
            curr_conds,
            index_col_names
        );
    } else {
        scan = std::make_shared<ScanPlan>(
            T_IndexScan,
            sm_manager_,
            tables[i],
            curr_conds,
            index_col_names
        );
    }

    std::shared_ptr<Plan> node = scan;

    if (!curr_conds.empty()) {
        node = std::make_shared<FilterPlan>(node, curr_conds);
    }
//单表查询：只保留根 Project 多表非 SELECT *：给每个 Scan 上方加局部 Project 多表 SELECT *：不加局部 Project
    if (tables.size() > 1 && !query->is_select_all) {
        auto proj_cols = table_proj_cols[tables[i]];

        if (!proj_cols.empty()) {
            node = std::make_shared<ProjectionPlan>(
                T_Projection,
                node,
                proj_cols,
                false
            );
        }
    }

    table_scan_executors[i] = node;}
    // 只有一个表，不需要join。
    if(tables.size() == 1)
    {
        return table_scan_executors[0];
    }
    // 获取剩余 join 条件 ，并按顺序构造左深树 
    auto conds = std::move(query->conds);

    // 按 FROM/JOIN 中表出现的顺序构造左深树
    std::shared_ptr<Plan> table_join_executors = table_scan_executors[0];

    std::vector<std::string> joined_tables;
    joined_tables.push_back(tables[0]);

    for (size_t i = 1; i < tables.size(); i++) {
        std::vector<Condition> join_conds;

        auto it = conds.begin();
        while (it != conds.end()) {
            if (it->is_rhs_val) {
                ++it;
                continue;
            }

            bool lhs_in_joined =
                std::find(joined_tables.begin(), joined_tables.end(), it->lhs_col.tab_name) != joined_tables.end();
            bool rhs_in_joined =
                std::find(joined_tables.begin(), joined_tables.end(), it->rhs_col.tab_name) != joined_tables.end();

            bool lhs_is_new = (it->lhs_col.tab_name == tables[i]);
            bool rhs_is_new = (it->rhs_col.tab_name == tables[i]);

            if ((lhs_in_joined && rhs_is_new) || (rhs_in_joined && lhs_is_new)) {
                join_conds.push_back(*it);
                it = conds.erase(it);
            } else {
                ++it;
            }
        }

        select_inner_join_index(sm_manager_, tables[i], join_conds, table_scan_executors[i]);

        table_join_executors = std::make_shared<JoinPlan>(
            T_NestLoop,
            std::move(table_join_executors),
            std::move(table_scan_executors[i]),
            join_conds
        );

        joined_tables.push_back(tables[i]);
    }

    return table_join_executors;

}


std::shared_ptr<Plan> Planner::generate_sort_plan(std::shared_ptr<Query> query, std::shared_ptr<Plan> plan)
{
    if (query->order_bys.empty()) {
        return plan;
    }
    std::vector<TabCol> sort_cols;
    std::vector<bool> is_desc;
    for (auto &ob : query->order_bys) {
        sort_cols.push_back(ob.col);
        is_desc.push_back(ob.is_desc);
    }
    return std::make_shared<SortPlan>(T_Sort, std::move(plan), sort_cols,
                                      is_desc, query->limit_num);
}


/**
 * @brief select plan 生成
 *
 * @param sel_cols select plan 选取的列
 * @param tab_names select plan 目标的表
 * @param conds select plan 选取条件
 */
std::shared_ptr<Plan> Planner::generate_select_plan(std::shared_ptr<Query> query, Context *context) {
    //逻辑优化
    query = logical_optimization(std::move(query), context);

    //物理优化
    std::shared_ptr<Plan> plannerRoot = physical_optimization(query, context);

    if (query->has_agg || !query->group_bys.empty() || !query->havings.empty()) {
        plannerRoot = std::make_shared<AggregatePlan>(
            std::move(plannerRoot),
            query->select_items,
            query->group_bys,
            query->havings,
            query->order_bys,
            query->limit_num);
    } else {
        auto sel_cols = query->cols;
        std::vector<std::string> output_names;
        output_names.reserve(query->select_items.size());
        for (const auto &item : query->select_items) {
            output_names.push_back(
                item.alias.empty() ? item.col.col_name : item.alias);
        }
        plannerRoot = std::make_shared<ProjectionPlan>(
            T_Projection,
            std::move(plannerRoot),
            std::move(sel_cols),
            query->is_select_all,
            query->limit_num,
            std::move(output_names)
        );
    }

    return plannerRoot;
}

// 生成DDL语句和DML语句的查询执行计划
std::shared_ptr<Plan> Planner::do_planner(std::shared_ptr<Query> query, Context *context)
{
    std::shared_ptr<Plan> plannerRoot;
    if (auto x = std::dynamic_pointer_cast<ast::CreateTable>(query->parse)) {
        // create table;
        std::vector<ColDef> col_defs;
        for (auto &field : x->fields) {
            if (auto sv_col_def = std::dynamic_pointer_cast<ast::ColDef>(field)) {
                ColDef col_def = {.name = sv_col_def->col_name,
                                  .type = interp_sv_type(sv_col_def->type_len->type),
                                  .len = sv_col_def->type_len->len};
                col_defs.push_back(col_def);
            } else {
                throw InternalError("Unexpected field type");
            }
        }
        plannerRoot = std::make_shared<DDLPlan>(T_CreateTable, x->tab_name, std::vector<std::string>(), col_defs);
    } else if (auto x = std::dynamic_pointer_cast<ast::DropTable>(query->parse)) {
        // drop table;
        plannerRoot = std::make_shared<DDLPlan>(T_DropTable, x->tab_name, std::vector<std::string>(), std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::CreateIndex>(query->parse)) {
        // create index;
        plannerRoot = std::make_shared<DDLPlan>(T_CreateIndex, x->tab_name, x->col_names, std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::DropIndex>(query->parse)) {
        // drop index
        plannerRoot = std::make_shared<DDLPlan>(T_DropIndex, x->tab_name, x->col_names, std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::InsertStmt>(query->parse)) {
        // insert;
        plannerRoot = std::make_shared<DMLPlan>(T_Insert, std::shared_ptr<Plan>(),  x->tab_name,  
                                                    query->values, std::vector<Condition>(), std::vector<SetClause>());
    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(query->parse)) {
        // delete;
        // 生成表扫描方式
        std::shared_ptr<Plan> table_scan_executors;
        // 只有一张表，不需要进行物理优化了
        // int index_no = get_indexNo(x->tab_name, query->conds);
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(x->tab_name, query->conds, index_col_names);
        
        if (index_exist == false) {  // 该表没有索引
            index_col_names.clear();
            table_scan_executors = 
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        } else {  // 存在索引
            table_scan_executors =
                std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        }

        plannerRoot = std::make_shared<DMLPlan>(T_Delete, table_scan_executors, x->tab_name,  
                                                std::vector<Value>(), query->conds, std::vector<SetClause>());
    } else if (auto x = std::dynamic_pointer_cast<ast::UpdateStmt>(query->parse)) {
        // update;
        // 生成表扫描方式
        std::shared_ptr<Plan> table_scan_executors;
        // 只有一张表，不需要进行物理优化了
        // int index_no = get_indexNo(x->tab_name, query->conds);
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(x->tab_name, query->conds, index_col_names);

        if (index_exist == false) {  // 该表没有索引
        index_col_names.clear();
            table_scan_executors = 
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        } else {  // 存在索引
            table_scan_executors =
                std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        }
        plannerRoot = std::make_shared<DMLPlan>(T_Update, table_scan_executors, x->tab_name,
                                                     std::vector<Value>(), query->conds, 
                                                     query->set_clauses);
    } else if (auto x = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse)) {
        auto table_to_alias = query->table_to_alias;
        bool is_explain_analyze = query->is_explain_analyze;
        std::shared_ptr<Plan> projection = generate_select_plan(std::move(query), context);
        plannerRoot = std::make_shared<DMLPlan>(T_select, projection, std::string(), std::vector<Value>(),
                                                    std::vector<Condition>(), std::vector<SetClause>(), is_explain_analyze, table_to_alias);
    } else {
        throw InternalError("Unexpected AST root");
    }
    return plannerRoot;
}
