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

#include <memory>
#include <set>

#include "execution/executor_delete.h"
#include "execution/executor_index_scan.h"
#include "execution/executor_insert.h"
#include "execution/executor_nestedloop_join.h"
#include "execution/executor_projection.h"
#include "execution/executor_seq_scan.h"
#include "execution/executor_update.h"
#include "index/ix.h"
#include "record_printer.h"

// 最左前缀匹配：选择能连续匹配最多索引列的索引
bool Planner::get_index_cols(std::string tab_name, std::vector<Condition> curr_conds, std::vector<std::string>& index_col_names) {
    index_col_names.clear();
    std::set<std::string> available_cols;
    for (auto& cond : curr_conds) {
        if (cond.is_rhs_val && cond.op != OP_NE && cond.lhs_col.tab_name == tab_name) {
            available_cols.insert(cond.lhs_col.col_name);
        }
    }
    if (available_cols.empty()) {
        return false;
    }

    TabMeta& tab = sm_manager_->db_.get_table(tab_name);
    const IndexMeta* best_index = nullptr;
    int best_score = 0;
    for (const auto& index : tab.indexes) {
        int score = 0;
        for (const auto& index_col : index.cols) {
            if (available_cols.count(index_col.name)) {
                score++;
            } else {
                break;
            }
        }
        if (score > best_score) {
            best_score = score;
            best_index = &index;
        }
    }
    if (!best_index || best_score == 0) {
        return false;
    }
    for (const auto& col : best_index->cols) {
        if (available_cols.count(col.name)) {
            index_col_names.push_back(col.name);
        } else {
            break;
        }
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
    // auto has_tab = [&](const std::string &tab_name) {
    //     return std::find(tab_names.begin(), tab_names.end(), tab_name) != tab_names.end();
    // };
    std::vector<Condition> solved_conds;
    auto it = conds.begin();
    while (it != conds.end()) {
        if ((tab_names.compare(it->lhs_col.tab_name) == 0 && it->is_rhs_val) || (it->lhs_col.tab_name.compare(it->rhs_col.tab_name) == 0)) {
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
    
    //TODO 实现逻辑优化规则

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
        throw RMDBError("failure");
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
    if (query->derived_tables.count(tables[i])) {
        auto &info = query->derived_tables.at(tables[i]);
        if (!info.is_union_table) {
            if (info.branch_queries.size() != 1) {
                throw InternalError("Unexpected derived subquery plan");
            }
            table_scan_executors[i] = generate_subquery_plan(info.branch_queries[0]);
            continue;
        }
        std::vector<std::shared_ptr<Plan>> branch_plans;
        for (auto &branch_query : info.branch_queries) {
            branch_plans.push_back(generate_subquery_plan(branch_query));
        }
        table_scan_executors[i] = std::make_shared<UnionPlan>(std::move(branch_plans), info.cols);
        continue;
    }

    auto curr_conds = pop_conds(query->conds, tables[i]);

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
    return std::make_shared<SortPlan>(T_Sort, std::move(plan), sort_cols, is_desc);
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
        plannerRoot = std::make_shared<ProjectionPlan>(
            T_Projection,
            std::move(plannerRoot),
            std::move(sel_cols),
            query->is_select_all,
            query->limit_num
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