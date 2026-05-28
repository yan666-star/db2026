/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "analyze.h"

static void cast_val_to_col(Value &val, ColType col_type) {
    if (val.type == col_type) {
        return;
    }//1000 原来是 int literal cast 成 float 后 from_float_literal 仍是 false
    if (col_type == TYPE_FLOAT && val.type == TYPE_INT) {
        bool from_float_literal = val.from_float_literal;
        val.set_float(static_cast<float>(val.int_val));
        val.from_float_literal = from_float_literal;
        return;
    }
    throw IncompatibleTypeError(coltype2str(col_type), coltype2str(val.type));
}

static std::vector<ColMeta>::const_iterator find_col_meta(const std::vector<ColMeta> &all_cols, const TabCol &target) {
    auto it = std::find_if(all_cols.begin(), all_cols.end(), [&](const ColMeta &c) {
        return c.tab_name == target.tab_name && c.name == target.col_name;
    });
    if (it == all_cols.end()) {
        throw ColumnNotFoundError(target.tab_name + "." + target.col_name);
    }
    return it;
}

/**
 * @description: 分析器，进行语义分析和查询重写，需要检查不符合语义规定的部分
 * @param {shared_ptr<ast::TreeNode>} parse parser生成的结果集
 * @return {shared_ptr<Query>} Query 
 */
std::shared_ptr<Query> Analyze::do_analyze(std::shared_ptr<ast::TreeNode> parse)
{
    std::shared_ptr<Query> query = std::make_shared<Query>();
    if (auto x = std::dynamic_pointer_cast<ast::SelectStmt>(parse))
    {
        // 处理表名 和别名
        query->is_explain_analyze = x->is_explain_analyze;
        query->is_select_all = x->is_select_all;

        for (auto &ref : x->tabs) {
            query->tables.push_back(ref.tab_name);

            std::string visible_name = ref.alias.empty() ? ref.tab_name : ref.alias;

            query->alias_to_table[visible_name] = ref.tab_name;
            query->alias_to_table[ref.tab_name] = ref.tab_name;
            query->table_to_alias[ref.tab_name] = visible_name;
        }
        for (auto &tab_name : query->tables) {
            if (!sm_manager_->db_.is_table(tab_name)) {
                throw TableNotFoundError(tab_name);
            }
        }

        std::vector<ColMeta> all_cols;
        get_all_cols(query->tables, all_cols);
        if (x->select_items.empty()) {
            // select all columns
            for (auto &col : all_cols) {
                TabCol sel_col = {.tab_name = col.tab_name, .col_name = col.name};
                query->cols.push_back(sel_col);
                SelectItem item;
                item.is_agg = false;
                item.col = sel_col;
                query->select_items.push_back(item);
            }
        } else {
            for (auto &sv_item : x->select_items) {
                SelectItem item;
                item.alias = sv_item->alias;
                if (auto sv_col = std::dynamic_pointer_cast<ast::Col>(sv_item->expr)) {
                    item.is_agg = false;
                    item.col = check_column(all_cols, {.tab_name = sv_col->tab_name, .col_name = sv_col->col_name},
                                            query->alias_to_table);
                    query->cols.push_back(item.col);
                } else if (auto sv_agg = std::dynamic_pointer_cast<ast::AggFunc>(sv_item->expr)) {
                    item.is_agg = true;
                    item.agg.type = convert_agg_type(sv_agg->func_type);
                    item.agg.is_star = sv_agg->is_star;
                    if (!sv_agg->is_star) {
                        item.agg.col =
                            check_column(all_cols, {.tab_name = sv_agg->col->tab_name, .col_name = sv_agg->col->col_name},
                                         query->alias_to_table);
                        auto col_meta = *find_col_meta(all_cols, item.agg.col);
                        if (item.agg.type == AGG_MAX || item.agg.type == AGG_MIN || item.agg.type == AGG_SUM ||
                            item.agg.type == AGG_AVG) {
                            if (col_meta.type != TYPE_INT && col_meta.type != TYPE_FLOAT) {
                                throw RMDBError("failure");
                            }
                        }
                    }
                    query->has_agg = true;
                } else {
                    throw RMDBError("failure");
                }
                query->select_items.push_back(item);
            }
        }
        query->limit_num = x->limit_num;

        for (auto &sv_col : x->group_bys) {
            query->group_bys.push_back(
                check_column(all_cols, {.tab_name = sv_col->tab_name, .col_name = sv_col->col_name}, query->alias_to_table));
        }

        for (auto &sv_having : x->havings) {
            HavingCond h;
            h.lhs.type = convert_agg_type(sv_having->lhs->func_type);
            h.lhs.is_star = sv_having->lhs->is_star;
            if (!h.lhs.is_star) {
                h.lhs.col = check_column(all_cols,
                                         {.tab_name = sv_having->lhs->col->tab_name, .col_name = sv_having->lhs->col->col_name},
                                         query->alias_to_table);
            }
            h.op = convert_sv_comp_op(sv_having->op);
            h.rhs_val = convert_sv_value(sv_having->rhs);
            ColType lhs_type = TYPE_INT;
            if (h.lhs.type == AGG_COUNT) {
                lhs_type = TYPE_INT;
            } else if (h.lhs.type == AGG_AVG) {
                lhs_type = TYPE_FLOAT;
            } else if (!h.lhs.is_star) {
                auto col_meta = *find_col_meta(all_cols, h.lhs.col);
                lhs_type = (h.lhs.type == AGG_SUM && col_meta.type == TYPE_INT) ? TYPE_INT : TYPE_FLOAT;
                if (h.lhs.type == AGG_MAX || h.lhs.type == AGG_MIN) {
                    lhs_type = col_meta.type;
                }
            }
            cast_val_to_col(h.rhs_val, lhs_type);
            h.rhs_val.init_raw(lhs_type == TYPE_INT ? sizeof(int) : (lhs_type == TYPE_FLOAT ? sizeof(float) : 0));
            query->havings.push_back(h);
            query->has_agg = true;
        }

        if (x->has_sort) {
            OrderByItem ob;
            ob.is_desc = x->order->orderby_dir == ast::OrderBy_DESC;
            ob.is_agg = false;
            ob.col = check_column(all_cols, {.tab_name = x->order->cols->tab_name, .col_name = x->order->cols->col_name},
                                  query->alias_to_table);
            query->order_bys.push_back(ob);
        }

        if (!query->group_bys.empty()) {
            for (auto &item : query->select_items) {
                if (!item.is_agg) {
                    bool in_group = false;
                    for (auto &g : query->group_bys) {
                        if (g.tab_name == item.col.tab_name && g.col_name == item.col.col_name) {
                            in_group = true;
                            break;
                        }
                    }
                    if (!in_group) {
                        throw RMDBError("failure");
                    }
                }
            }
        } else if (query->has_agg) {
            for (auto &item : query->select_items) {
                if (!item.is_agg) {
                    throw RMDBError("failure");
                }
            }
        }

        //处理where条件
        get_clause(x->conds, query->conds);
        check_clause(query->tables, query->conds, query->alias_to_table);
    } else if (auto x = std::dynamic_pointer_cast<ast::UpdateStmt>(parse)) {
        if (!sm_manager_->db_.is_table(x->tab_name)) {
            throw TableNotFoundError(x->tab_name);
        }
        query->tables = {x->tab_name};
        TabMeta &tab = sm_manager_->db_.get_table(x->tab_name);
        for (auto &sv_set : x->set_clauses) {
            SetClause set_clause;
            set_clause.lhs = {.tab_name = x->tab_name, .col_name = sv_set->col_name};
            set_clause.rhs = convert_sv_value(sv_set->val);
            auto col = tab.get_col(sv_set->col_name);
            cast_val_to_col(set_clause.rhs, col->type);
            if (col->type != set_clause.rhs.type) {
                throw IncompatibleTypeError(coltype2str(col->type), coltype2str(set_clause.rhs.type));
            }
            set_clause.rhs.init_raw(col->len);
            query->set_clauses.push_back(set_clause);
        }
        get_clause(x->conds, query->conds);
        std::map<std::string, std::string> alias_to_table;
        alias_to_table[x->tab_name] = x->tab_name;
        check_clause({x->tab_name}, query->conds, alias_to_table);
    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(parse)) {
        //处理where条件
        get_clause(x->conds, query->conds);

        std::map<std::string, std::string> alias_to_table;
        alias_to_table[x->tab_name] = x->tab_name;

        check_clause({x->tab_name}, query->conds, alias_to_table);
    } else if (auto x = std::dynamic_pointer_cast<ast::InsertStmt>(parse)) {
        // 处理insert 的values值
        for (auto &sv_val : x->vals) {
            query->values.push_back(convert_sv_value(sv_val));
        }
    } else {
        // do nothing
    }
    query->parse = std::move(parse);
    return query;
}


    
TabCol Analyze::check_column(const std::vector<ColMeta> &all_cols,
                             TabCol target,
                             const std::map<std::string, std::string> &alias_to_table) {
    if (!target.tab_name.empty()) {
        auto it = alias_to_table.find(target.tab_name);
        if (it != alias_to_table.end()) {
            target.tab_name = it->second;
        }
    }

    if (target.tab_name.empty()) {
        std::string tab_name;
        for (auto &col : all_cols) {
            if (col.name == target.col_name) {
                if (!tab_name.empty()) {
                    throw AmbiguousColumnError(target.col_name);
                }
                tab_name = col.tab_name;
            }
        }
        if (tab_name.empty()) {
            throw ColumnNotFoundError(target.col_name);
        }
        target.tab_name = tab_name;
    } else {
        TabMeta &tab = sm_manager_->db_.get_table(target.tab_name);
        if (!tab.is_col(target.col_name)) {
            throw ColumnNotFoundError(target.tab_name + '.' + target.col_name);
        }
    }

    return target;
}
void Analyze::get_all_cols(const std::vector<std::string> &tab_names, std::vector<ColMeta> &all_cols) {
    for (auto &sel_tab_name : tab_names) {
        // 这里db_不能写成get_db(), 注意要传指针
        const auto &sel_tab_cols = sm_manager_->db_.get_table(sel_tab_name).cols;
        all_cols.insert(all_cols.end(), sel_tab_cols.begin(), sel_tab_cols.end());
    }
}

void Analyze::get_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds, std::vector<Condition> &conds) {
    conds.clear();
    for (auto &expr : sv_conds) {
        Condition cond;
        auto lhs_col = std::dynamic_pointer_cast<ast::Col>(expr->lhs);
        if (lhs_col == nullptr) {
            throw RMDBError("failure");
        }
        cond.lhs_col = {.tab_name = lhs_col->tab_name, .col_name = lhs_col->col_name};
        cond.op = convert_sv_comp_op(expr->op);
        if (auto rhs_val = std::dynamic_pointer_cast<ast::Value>(expr->rhs)) {
            cond.is_rhs_val = true;
            cond.rhs_val = convert_sv_value(rhs_val);
        } else if (auto rhs_col = std::dynamic_pointer_cast<ast::Col>(expr->rhs)) {
            cond.is_rhs_val = false;
            cond.rhs_col = {.tab_name = rhs_col->tab_name, .col_name = rhs_col->col_name};
        }
        conds.push_back(cond);
    }
}

void Analyze::check_clause(const std::vector<std::string> &tab_names,
                           std::vector<Condition> &conds,
                           const std::map<std::string, std::string> &alias_to_table) {
    // auto all_cols = get_all_cols(tab_names);
    std::vector<ColMeta> all_cols;
    get_all_cols(tab_names, all_cols);
    // Get raw values in where clause
    for (auto &cond : conds) {
        // Infer table name from column name
        cond.lhs_col = check_column(all_cols, cond.lhs_col, alias_to_table);
        if (!cond.is_rhs_val) {
            cond.rhs_col = check_column(all_cols, cond.rhs_col, alias_to_table);
        }
        TabMeta &lhs_tab = sm_manager_->db_.get_table(cond.lhs_col.tab_name);
        auto lhs_col = lhs_tab.get_col(cond.lhs_col.col_name);
        ColType lhs_type = lhs_col->type;
        ColType rhs_type;
        if (cond.is_rhs_val) {
            cast_val_to_col(cond.rhs_val, lhs_type);
            cond.rhs_val.init_raw(lhs_col->len);
            rhs_type = cond.rhs_val.type;
        } else {
            TabMeta &rhs_tab = sm_manager_->db_.get_table(cond.rhs_col.tab_name);
            auto rhs_col = rhs_tab.get_col(cond.rhs_col.col_name);
            rhs_type = rhs_col->type;
        }
        if (lhs_type != rhs_type) {
            throw IncompatibleTypeError(coltype2str(lhs_type), coltype2str(rhs_type));
        }
    }
}


Value Analyze::convert_sv_value(const std::shared_ptr<ast::Value> &sv_val) {
    Value val;
    if (auto int_lit = std::dynamic_pointer_cast<ast::IntLit>(sv_val)) {
        val.set_int(int_lit->val);
   } else if (auto float_lit = std::dynamic_pointer_cast<ast::FloatLit>(sv_val)) {
        val.set_float(float_lit->val);
        val.from_float_literal = true;
    } else if (auto str_lit = std::dynamic_pointer_cast<ast::StringLit>(sv_val)) {
        val.set_str(str_lit->val);
    } else {
        throw InternalError("Unexpected sv value type");
    }
    return val;
}

CompOp Analyze::convert_sv_comp_op(ast::SvCompOp op) {
    std::map<ast::SvCompOp, CompOp> m = {
        {ast::SV_OP_EQ, OP_EQ}, {ast::SV_OP_NE, OP_NE}, {ast::SV_OP_LT, OP_LT},
        {ast::SV_OP_GT, OP_GT}, {ast::SV_OP_LE, OP_LE}, {ast::SV_OP_GE, OP_GE},
    };
    return m.at(op);
}

AggType Analyze::convert_agg_type(ast::AggFuncType func_type) {
    switch (func_type) {
        case ast::AGG_COUNT:
            return AGG_COUNT;
        case ast::AGG_MAX:
            return AGG_MAX;
        case ast::AGG_MIN:
            return AGG_MIN;
        case ast::AGG_SUM:
            return AGG_SUM;
        case ast::AGG_AVG:
            return AGG_AVG;
        default:
            throw RMDBError("failure");
    }
}
