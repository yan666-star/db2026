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

static void assign_col_offsets(std::vector<ColMeta> &cols) {
    size_t off = 0;
    for (auto &col : cols) {
        col.offset = off;
        off += col.len;
    }
}

bool Analyze::union_compatible(ColType a, ColType b) {
    if (a == b) {
        return true;
    }
    if ((a == TYPE_INT && b == TYPE_FLOAT) || (a == TYPE_FLOAT && b == TYPE_INT)) {
        return true;
    }
    return a == TYPE_STRING && b == TYPE_STRING;
}

ColMeta Analyze::promote_union_col(const ColMeta &a, const ColMeta &b) {
    if (!union_compatible(a.type, b.type)) {
        throw RMDBError("failure");
    }
    ColMeta out = a;
    if (a.type == TYPE_INT && b.type == TYPE_FLOAT) {
        out.type = TYPE_FLOAT;
        out.len = static_cast<int>(sizeof(float));
    } else if (a.type == TYPE_FLOAT && b.type == TYPE_INT) {
        out.type = TYPE_FLOAT;
        out.len = static_cast<int>(sizeof(float));
    } else if (a.type == TYPE_STRING && b.type == TYPE_STRING) {
        out.type = TYPE_STRING;
        out.len = std::max(a.len, b.len);
    }
    return out;
}

std::vector<ColMeta> Analyze::get_branch_output_cols(const std::shared_ptr<Query> &query) {
    std::vector<ColMeta> all_cols;
    get_query_cols(query, all_cols);
    if (query->is_select_all) {
        return all_cols;
    }

    std::vector<ColMeta> out;
    for (const auto &item : query->select_items) {
        ColMeta col{};
        if (!item.is_agg) {
            auto it = find_col_meta(all_cols, item.col);
            col = *it;
            col.name = item.alias.empty() ? item.col.col_name : item.alias;
        } else {
            col.tab_name = "";
            col.name = item.alias.empty() ? "agg" : item.alias;
            if (item.agg.type == AGG_COUNT) {
                col.type = TYPE_INT;
                col.len = static_cast<int>(sizeof(int));
            } else if (item.agg.type == AGG_AVG) {
                col.type = TYPE_FLOAT;
                col.len = static_cast<int>(sizeof(float));
            } else {
                auto it = find_col_meta(all_cols, item.agg.col);
                if (item.agg.type == AGG_SUM && it->type == TYPE_INT) {
                    col.type = TYPE_INT;
                    col.len = static_cast<int>(sizeof(int));
                } else if (item.agg.type == AGG_SUM) {
                    col.type = TYPE_FLOAT;
                    col.len = static_cast<int>(sizeof(float));
                } else {
                    col.type = it->type;
                    col.len = it->len;
                }
            }
        }

        out.push_back(col);
    }
    assign_col_offsets(out);
    return out;
}

DerivedTableInfo Analyze::analyze_union_branches(const std::vector<std::shared_ptr<ast::SelectStmt>> &branches,
                                                 const std::string &alias) {
    if (branches.size() < 2) {
        throw RMDBError("failure");
    }

    DerivedTableInfo info;
    info.is_union_table = true;
    std::vector<std::vector<ColMeta>> branch_cols;
    for (auto &branch : branches) {
        auto branch_query = analyze_select(branch, false);
        info.branch_queries.push_back(branch_query);
        branch_cols.push_back(get_branch_output_cols(branch_query));
    }

    const size_t ncols = branch_cols[0].size();
    if (ncols == 0) {
        throw RMDBError("failure");
    }
    for (size_t i = 1; i < branch_cols.size(); i++) {
        if (branch_cols[i].size() != ncols) {
            throw RMDBError("failure");
        }
        for (size_t j = 0; j < ncols; j++) {
            if (!union_compatible(branch_cols[0][j].type, branch_cols[i][j].type)) {
                throw RMDBError("failure");
            }
        }
    }

    info.cols.clear();
    size_t current_offset = 0;
    for (size_t j = 0; j < ncols; j++) {
        ColMeta promoted = branch_cols[0][j];
        for (size_t i = 1; i < branch_cols.size(); i++) {
            promoted = promote_union_col(promoted, branch_cols[i][j]);
        }
        promoted.name = branch_cols[0][j].name;
        promoted.tab_name = alias;
        promoted.offset = static_cast<int>(current_offset);
        current_offset += promoted.len;
        info.cols.push_back(promoted);
    }
    return info;
}

DerivedTableInfo Analyze::analyze_derived_subquery(const std::shared_ptr<ast::SelectStmt> &subquery,
                                                   const std::string &alias) {
    if (subquery == nullptr) {
        throw RMDBError("failure");
    }
    if (subquery->is_union) {
        return analyze_union_branches(subquery->union_branches, alias);
    }

    if (!subquery->group_bys.empty() || !subquery->havings.empty()) {
        throw RMDBError("failure");
    }
    auto sub_q = analyze_select(subquery, true);
    if (sub_q->has_agg || !sub_q->group_bys.empty() || !sub_q->havings.empty()) {
        throw RMDBError("failure");
    }

    DerivedTableInfo info;
    info.is_union_table = false;
    info.branch_queries.push_back(sub_q);
    std::vector<ColMeta> out_cols = get_branch_output_cols(sub_q);
    size_t current_offset = 0;
    for (auto &col : out_cols) {
        col.tab_name = alias;
        col.offset = static_cast<int>(current_offset);
        current_offset += col.len;
        info.cols.push_back(col);
    }
    return info;
}

std::shared_ptr<Query> Analyze::analyze_top_level_union(std::shared_ptr<ast::SelectStmt> x) {
    static const std::string kUnionAlias = "__union_output__";
    std::shared_ptr<Query> query = std::make_shared<Query>();
    query->is_select_all = true;

    query->derived_tables[kUnionAlias] = analyze_union_branches(x->union_branches, kUnionAlias);
    query->tables.push_back(kUnionAlias);
    query->alias_to_table[kUnionAlias] = kUnionAlias;
    query->table_to_alias[kUnionAlias] = kUnionAlias;

    std::vector<ColMeta> all_cols;
    get_query_cols(query, all_cols);
    for (auto &col : all_cols) {
        TabCol sel_col = {.tab_name = col.tab_name, .col_name = col.name};
        query->cols.push_back(sel_col);
        SelectItem item;
        item.is_agg = false;
        item.col = sel_col;
        query->select_items.push_back(item);
    }
    query->limit_num = x->limit_num;

    const auto &order_list = x->orders;
    if (!order_list.empty()) {
        for (auto &sv_order : order_list) {
            OrderByItem ob;
            ob.is_desc = sv_order->orderby_dir == ast::OrderBy_DESC;
            ob.is_agg = false;
            try {
                ob.col = check_column(all_cols,
                                      {.tab_name = sv_order->cols->tab_name, .col_name = sv_order->cols->col_name},
                                      query->alias_to_table);
            } catch (ColumnNotFoundError &) {
                throw RMDBError("failure");
            } catch (AmbiguousColumnError &) {
                throw RMDBError("failure");
            }
            query->order_bys.push_back(ob);
        }
    } else if (x->has_sort && x->order) {
        OrderByItem ob;
        ob.is_desc = x->order->orderby_dir == ast::OrderBy_DESC;
        ob.is_agg = false;
        try {
            ob.col = check_column(all_cols, {.tab_name = x->order->cols->tab_name, .col_name = x->order->cols->col_name},
                                  query->alias_to_table);
        } catch (ColumnNotFoundError &) {
            throw RMDBError("failure");
        } catch (AmbiguousColumnError &) {
            throw RMDBError("failure");
        }
        query->order_bys.push_back(ob);
    }

    query->parse = x;
    return query;
}

void Analyze::get_query_cols(const std::shared_ptr<Query> &query, std::vector<ColMeta> &all_cols) {
    for (auto &tab_name : query->tables) {
        auto it = query->derived_tables.find(tab_name);
        if (it != query->derived_tables.end()) {
            all_cols.insert(all_cols.end(), it->second.cols.begin(), it->second.cols.end());
        } else {
            const auto &sel_tab_cols = sm_manager_->db_.get_table(tab_name).cols;
            all_cols.insert(all_cols.end(), sel_tab_cols.begin(), sel_tab_cols.end());
        }
    }
}

std::shared_ptr<Query> Analyze::analyze_select(std::shared_ptr<ast::SelectStmt> x, bool allow_derived) {
    if (x->is_union) {
        if (!allow_derived) {
            throw RMDBError("failure");
        }
        return analyze_top_level_union(x);
    }

    std::shared_ptr<Query> query = std::make_shared<Query>();
    query->is_explain_analyze = x->is_explain_analyze;
    query->is_select_all = x->is_select_all;

    for (auto &ref : x->tabs) {
        if (ref.is_subquery) {
            if (!allow_derived) {
                throw RMDBError("failure");
            }
            if (ref.alias.empty()) {
                throw RMDBError("failure");
            }
            if (!ref.subquery) {
                throw RMDBError("failure");
            }
            query->derived_tables[ref.alias] = analyze_derived_subquery(ref.subquery, ref.alias);
            query->tables.push_back(ref.alias);
            query->alias_to_table[ref.alias] = ref.alias;
            query->alias_to_table[ref.tab_name] = ref.alias;
            query->table_to_alias[ref.alias] = ref.alias;
        } else {
            query->tables.push_back(ref.tab_name);
            std::string visible_name = ref.alias.empty() ? ref.tab_name : ref.alias;
            query->alias_to_table[visible_name] = ref.tab_name;
            query->alias_to_table[ref.tab_name] = ref.tab_name;
            query->table_to_alias[ref.tab_name] = visible_name;
        }
    }

    for (auto &tab_name : query->tables) {
        if (query->derived_tables.count(tab_name)) {
            continue;
        }
        if (!sm_manager_->db_.is_table(tab_name)) {
            throw TableNotFoundError(tab_name);
        }
    }

    std::vector<ColMeta> all_cols;
    get_query_cols(query, all_cols);
    if (x->select_items.empty()) {
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

    const auto &order_list = x->orders;
    if (!order_list.empty()) {
        for (auto &sv_order : order_list) {
            OrderByItem ob;
            ob.is_desc = sv_order->orderby_dir == ast::OrderBy_DESC;
            ob.is_agg = false;
            try {
                ob.col = check_column(all_cols,
                                      {.tab_name = sv_order->cols->tab_name, .col_name = sv_order->cols->col_name},
                                      query->alias_to_table);
            } catch (ColumnNotFoundError &) {
                throw RMDBError("failure");
            } catch (AmbiguousColumnError &) {
                throw RMDBError("failure");
            }
            query->order_bys.push_back(ob);
        }
    } else if (x->has_sort && x->order) {
        OrderByItem ob;
        ob.is_desc = x->order->orderby_dir == ast::OrderBy_DESC;
        ob.is_agg = false;
        try {
            ob.col = check_column(all_cols, {.tab_name = x->order->cols->tab_name, .col_name = x->order->cols->col_name},
                                  query->alias_to_table);
        } catch (ColumnNotFoundError &) {
            throw RMDBError("failure");
        } catch (AmbiguousColumnError &) {
            throw RMDBError("failure");
        }
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

    get_clause(x->conds, query->conds);
    check_clause(query, query->conds, query->alias_to_table);
    query->parse = x;
    return query;
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
        query = analyze_select(x, true);
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
        check_clause(query, query->conds, alias_to_table);
    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(parse)) {
        query->tables = {x->tab_name};
        get_clause(x->conds, query->conds);

        std::map<std::string, std::string> alias_to_table;
        alias_to_table[x->tab_name] = x->tab_name;

        check_clause(query, query->conds, alias_to_table);
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
        bool found = false;
        for (auto &col : all_cols) {
            if (col.tab_name == target.tab_name && col.name == target.col_name) {
                found = true;
                break;
            }
        }
        if (!found) {
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

void Analyze::check_clause(const std::shared_ptr<Query> &query,
                           std::vector<Condition> &conds,
                           const std::map<std::string, std::string> &alias_to_table) {
    std::vector<ColMeta> all_cols;
    get_query_cols(query, all_cols);
    // Get raw values in where clause
    for (auto &cond : conds) {
        // Infer table name from column name
        cond.lhs_col = check_column(all_cols, cond.lhs_col, alias_to_table);
        if (!cond.is_rhs_val) {
            cond.rhs_col = check_column(all_cols, cond.rhs_col, alias_to_table);
        }
        ColType lhs_type;
        ColType rhs_type;
        auto lhs_meta = find_col_meta(all_cols, cond.lhs_col);
        lhs_type = lhs_meta->type;
        if (cond.is_rhs_val) {
            cast_val_to_col(cond.rhs_val, lhs_type);
            cond.rhs_val.init_raw(lhs_meta->len);
            rhs_type = cond.rhs_val.type;
        } else {
            auto rhs_meta = find_col_meta(all_cols, cond.rhs_col);
            rhs_type = rhs_meta->type;
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
