/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <cerrno>
#include <cstring>
#include <string>
#include "optimizer/plan.h"
#include "execution/executor_abstract.h"
#include "execution/executor_nestedloop_join.h"
#include "execution/executor_projection.h"
#include "execution/executor_seq_scan.h"
#include "execution/executor_index_scan.h"
#include "execution/executor_update.h"
#include "execution/executor_insert.h"
#include "execution/executor_delete.h"
#include "execution/executor_filter.h"
#include "execution/execution_sort.h"
#include "execution/executor_aggregation.h"
#include "execution/executor_union.h"
#include "common/common.h"

typedef enum portalTag{
    PORTAL_Invalid_Query = 0,
    PORTAL_ONE_SELECT,
    PORTAL_EXPLAIN_ANALYZE,
    PORTAL_DML_WITHOUT_SELECT,
    PORTAL_MULTI_QUERY,
    PORTAL_CMD_UTILITY
} portalTag;


struct PortalStmt {
    portalTag tag;
    
    std::vector<TabCol> sel_cols;
    std::unique_ptr<AbstractExecutor> root;
    std::shared_ptr<Plan> plan;
    
    PortalStmt(portalTag tag_, std::vector<TabCol> sel_cols_, std::unique_ptr<AbstractExecutor> root_, std::shared_ptr<Plan> plan_) :
            tag(tag_), sel_cols(std::move(sel_cols_)), root(std::move(root_)), plan(std::move(plan_)) {}
};

class Portal
{
   private:
    SmManager *sm_manager_;

    static std::vector<TabCol> collect_output_cols(const std::shared_ptr<Plan> &plan) {
        if (auto p = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
            auto output_cols = p->sel_cols_;
            if (p->output_names_.size() == output_cols.size()) {
                for (size_t i = 0; i < output_cols.size(); ++i) {
                    output_cols[i].col_name = p->output_names_[i];
                }
            }
            return output_cols;
        }
        if (auto a = std::dynamic_pointer_cast<AggregatePlan>(plan)) {
            std::vector<TabCol> out_cols;
            for (size_t i = 0; i < a->select_items_.size(); i++) {
                TabCol tc;
                if (!a->select_items_[i].alias.empty()) {
                    tc = {"", a->select_items_[i].alias};
                } else if (a->select_items_[i].is_agg) {
                    tc = {"", "agg_" + std::to_string(i)};
                } else {
                    tc = a->select_items_[i].col;
                }
                out_cols.push_back(tc);
            }
            return out_cols;
        }
        if (auto s = std::dynamic_pointer_cast<SortPlan>(plan)) {
            return collect_output_cols(s->subplan_);
        }
        if (auto f = std::dynamic_pointer_cast<FilterPlan>(plan)) {
            return collect_output_cols(f->subplan_);
        }
        if (auto u = std::dynamic_pointer_cast<UnionPlan>(plan)) {
            std::vector<TabCol> out_cols;
            for (auto &col : u->out_cols_) {
                out_cols.push_back({col.tab_name, col.name});
            }
            return out_cols;
        }
        return {};
    }

   public:
    Portal(SmManager *sm_manager) : sm_manager_(sm_manager){}
    ~Portal(){}

    // 将查询执行计划转换成对应的算子树
    std::shared_ptr<PortalStmt> start(std::shared_ptr<Plan> plan, Context *context)
    {
        // 这里可以将select进行拆分，例如：一个select，带有return的select等
        if (auto x = std::dynamic_pointer_cast<OtherPlan>(plan)) {
            return std::make_shared<PortalStmt>(PORTAL_CMD_UTILITY, std::vector<TabCol>(), std::unique_ptr<AbstractExecutor>(),plan);
        } else if(auto x = std::dynamic_pointer_cast<SetKnobPlan>(plan)) {
            return std::make_shared<PortalStmt>(PORTAL_CMD_UTILITY, std::vector<TabCol>(), std::unique_ptr<AbstractExecutor>(), plan);
        } else if (auto x = std::dynamic_pointer_cast<SetTransactionIsolationPlan>(plan)) {
            return std::make_shared<PortalStmt>(PORTAL_CMD_UTILITY, std::vector<TabCol>(), std::unique_ptr<AbstractExecutor>(), plan);
        } else if (auto x = std::dynamic_pointer_cast<DDLPlan>(plan)) {
            return std::make_shared<PortalStmt>(PORTAL_MULTI_QUERY, std::vector<TabCol>(), std::unique_ptr<AbstractExecutor>(),plan);
        } else if (auto x = std::dynamic_pointer_cast<DMLPlan>(plan)) {
            switch(x->tag) {
                case T_select:
                {
                    std::unique_ptr<AbstractExecutor> root = convert_plan_executor(x->subplan_, context);
                    std::vector<TabCol> out_cols = collect_output_cols(x->subplan_);

                    if (x->is_explain_analyze_) {
                        return std::make_shared<PortalStmt>(
                            PORTAL_EXPLAIN_ANALYZE,
                            out_cols,
                            std::move(root),
                            plan
                        );
                    }

                    return std::make_shared<PortalStmt>(
                        PORTAL_ONE_SELECT,
                        out_cols,
                        std::move(root),
                        plan
                    );
                }
                    
                case T_Update:
                {
                    std::unique_ptr<AbstractExecutor> scan =
                        convert_plan_executor(
                            x->subplan_, context, nullptr, true, true);
                    std::vector<Rid> rids;
                    for (scan->beginTuple(); !scan->is_end(); scan->nextTuple()) {
                        rids.push_back(scan->rid());
                    }
                    std::unique_ptr<AbstractExecutor> root =std::make_unique<UpdateExecutor>(sm_manager_, 
                                                            x->tab_name_, x->set_clauses_, x->conds_, rids, context);
                    return std::make_shared<PortalStmt>(PORTAL_DML_WITHOUT_SELECT, std::vector<TabCol>(), std::move(root), plan);
                }
                case T_Delete:
                {
                    std::unique_ptr<AbstractExecutor> scan =
                        convert_plan_executor(
                            x->subplan_, context, nullptr, true, true);
                    std::vector<Rid> rids;
                    for (scan->beginTuple(); !scan->is_end(); scan->nextTuple()) {
                        rids.push_back(scan->rid());
                    }

                    std::unique_ptr<AbstractExecutor> root =
                        std::make_unique<DeleteExecutor>(sm_manager_, x->tab_name_, x->conds_, rids, context);

                    return std::make_shared<PortalStmt>(PORTAL_DML_WITHOUT_SELECT, std::vector<TabCol>(), std::move(root), plan);
                }

                case T_Insert:
                {
                    std::unique_ptr<AbstractExecutor> root =
                            std::make_unique<InsertExecutor>(sm_manager_, x->tab_name_, x->values_, context);
            
                    return std::make_shared<PortalStmt>(PORTAL_DML_WITHOUT_SELECT, std::vector<TabCol>(), std::move(root), plan);
                }


                default:
                    throw InternalError("Unexpected field type");
                    break;
            }
        } else {
            throw InternalError("Unexpected field type");
        }
        return nullptr;
    }

    // 遍历算子树并执行算子生成执行结果
    void run(std::shared_ptr<PortalStmt> portal, QlManager* ql, txn_id_t *txn_id, Context *context){
        switch(portal->tag) {
            case PORTAL_ONE_SELECT:
            {
                ql->select_from(std::move(portal->root), std::move(portal->sel_cols), context);
                break;
            }

            case PORTAL_EXPLAIN_ANALYZE:
            {
                ql->explain_analyze(std::move(portal->root), portal->plan, context);
                break;
            }

            case PORTAL_DML_WITHOUT_SELECT:
            {
                ql->run_dml(std::move(portal->root));
                break;
            }
            case PORTAL_MULTI_QUERY:
            {
                ql->run_mutli_query(portal->plan, context);
                break;
            }
            case PORTAL_CMD_UTILITY:
            {
                ql->run_cmd_utility(portal->plan, txn_id, context);
                break;
            }
            default:
            {
                throw InternalError("Unexpected field type");
            }
        }
    }

    // 清空资源
    void drop(){}


    std::unique_ptr<AbstractExecutor> convert_plan_executor(std::shared_ptr<Plan> plan,
                                                        Context *context,
                                                        FilterPlan *filter_plan = nullptr,
                                                        bool enable_equality_cache = false,
                                                        bool track_serializable_reads = true)
    {
        if(auto x = std::dynamic_pointer_cast<ProjectionPlan>(plan)){
            return std::make_unique<ProjectionExecutor>(
                convert_plan_executor(
                    x->subplan_, context, filter_plan, false,
                    track_serializable_reads),
                x->sel_cols_,
                x.get()
            );
        }
        //此时天剑filter在project和scan之间，所以如果当前节点是filter，就继续往它的子节点找，直到找到scan节点
        else if(auto x = std::dynamic_pointer_cast<FilterPlan>(plan)) {
            if (std::dynamic_pointer_cast<ScanPlan>(x->subplan_) != nullptr) {
                return convert_plan_executor(
                    x->subplan_, context, x.get(), false,
                    track_serializable_reads);
            }
            return std::make_unique<FilterExecutor>(
                convert_plan_executor(
                    x->subplan_, context, nullptr, false,
                    track_serializable_reads),
                x->conds_,
                x.get());
        }//FilterPlan 不创建 FilterExecutor。把自己 x.get() 传给下面的 ScanExecutor。这样 ScanExecutor 每通过一条过滤条件，就能执行 filter_plan_->rows_++。
        else if(auto x = std::dynamic_pointer_cast<ScanPlan>(plan)) {
            if(x->tag == T_SeqScan) {
                return std::make_unique<SeqScanExecutor>(
                    sm_manager_,
                    x->tab_name_,
                    x->conds_,
                    context,
                    x.get(),
                    filter_plan,
                    enable_equality_cache,
                    track_serializable_reads
                );
            }
            else {
                return std::make_unique<IndexScanExecutor>(
                    sm_manager_,
                    x->tab_name_,
                    x->conds_,
                    x->index_col_names_,
                    context,
                    x.get(),
                    track_serializable_reads
                );
            }   //SeqScanExecutor 里面可以做到：scan_plan_->rows_++;filter_plan_->rows_++;
        }
        else if(auto x = std::dynamic_pointer_cast<JoinPlan>(plan)) {
            std::unique_ptr<AbstractExecutor> left =
                convert_plan_executor(
                    x->left_, context, nullptr, false,
                    track_serializable_reads);
            std::unique_ptr<AbstractExecutor> right =
                convert_plan_executor(
                    x->right_, context, nullptr, false,
                    track_serializable_reads);
            std::unique_ptr<AbstractExecutor> join = std::make_unique<NestedLoopJoinExecutor>(
                                std::move(left), 
                                std::move(right),
                                x->conds_,
                                x.get());
            return join;
        } else if(auto x = std::dynamic_pointer_cast<SortPlan>(plan)) {
            return std::make_unique<SortExecutor>(
                convert_plan_executor(
                    x->subplan_, context, filter_plan, false,
                    track_serializable_reads),
                x.get());
        } else if (auto x = std::dynamic_pointer_cast<UnionPlan>(plan)) {
            std::vector<std::unique_ptr<AbstractExecutor>> branch_execs;
            branch_execs.reserve(x->branches_.size());
            for (auto &branch : x->branches_) {
                branch_execs.push_back(convert_plan_executor(
                    branch, context, filter_plan, false,
                    track_serializable_reads));
            }
            return std::make_unique<UnionExecutor>(std::move(branch_execs), x.get());
        } else if (auto x = std::dynamic_pointer_cast<AggregatePlan>(plan)) {
            return std::make_unique<AggregationExecutor>(
                convert_plan_executor(
                    x->subplan_, context, filter_plan, false,
                    track_serializable_reads),
                x.get()
            );
        }
        return nullptr;
    }

};
