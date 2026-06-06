#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "common/common.h"
#include "record/rm_defs.h"

inline std::string trim_trailing_zeros(std::string s) {
    if (s.find('.') == std::string::npos) {
        return s;
    }
    while (!s.empty() && s.back() == '0') {
        s.pop_back();
    }
    if (!s.empty() && s.back() == '.') {
        s.pop_back();
    }
    return s;
}

inline std::string format_float_output(float f, bool agg_float_fixed) {
    char buf[64];
    if (agg_float_fixed) {
        std::snprintf(buf, sizeof(buf), "%.6f", static_cast<double>(f));
        return std::string(buf);
    }
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(f));
    std::string s(buf);
    if (s.find('e') == std::string::npos && s.find('E') == std::string::npos) {
        return s;
    }
    std::snprintf(buf, sizeof(buf), "%.10f", static_cast<double>(f));
    return trim_trailing_zeros(std::string(buf));
}

inline std::string format_col_value(const ColMeta &col, const char *rec_buf, bool agg_float_fixed = false) {
    if (col.type == TYPE_INT) {
        return std::to_string(*(const int *)rec_buf);
    }
    if (col.type == TYPE_FLOAT) {
        return format_float_output(*(const float *)rec_buf, agg_float_fixed);
    }
    if (col.type == TYPE_STRING) {
        std::string col_str((const char *)rec_buf, col.len);
        size_t end_pos = col_str.find_last_not_of('\0');
        if (end_pos != std::string::npos) {
            col_str.resize(end_pos + 1);
        } else {
            col_str.clear();
        }
        while (!col_str.empty() && col_str.back() == ' ') {
            col_str.pop_back();
        }
        return col_str;
    }
    return "";
}

inline int compare_string_value(const char *a, const char *b, int col_len) {
    std::string sa(a, col_len);
    std::string sb(b, col_len);
    size_t null_pos_a = sa.find('\0');
    if (null_pos_a != std::string::npos) {
        sa.erase(null_pos_a);
    }
    size_t null_pos_b = sb.find('\0');
    if (null_pos_b != std::string::npos) {
        sb.erase(null_pos_b);
    }
    while (!sa.empty() && sa.back() == ' ') {
        sa.pop_back();
    }
    while (!sb.empty() && sb.back() == ' ') {
        sb.pop_back();
    }
    if (sa < sb) {
        return -1;
    }
    if (sa > sb) {
        return 1;
    }
    return 0;
}

inline int compare_col_value(const char *a, const char *b, ColType type, int col_len) {
    switch (type) {
        case TYPE_INT: {
            int ia = *(int *)a;
            int ib = *(int *)b;
            return (ia < ib) ? -1 : ((ia > ib) ? 1 : 0);
        }
        case TYPE_FLOAT: {
            float fa = *(float *)a;
            float fb = *(float *)b;
            return (fa < fb) ? -1 : ((fa > fb) ? 1 : 0);
        }
        case TYPE_STRING:
            return compare_string_value(a, b, col_len);
        default:
            throw InternalError("Unexpected data type");
    }
}

inline int compare_number_value(double a, double b) {
    return (a < b) ? -1 : ((a > b) ? 1 : 0);
}

inline int compare_col_to_value(const char *lhs_data, const ColMeta &lhs_col, const Value &rhs_val) {
    if (lhs_col.type == TYPE_INT) {
        double lhs = static_cast<double>(*(const int *)lhs_data);
        double rhs = 0;
        if (rhs_val.type == TYPE_FLOAT) {
            rhs = static_cast<double>(rhs_val.float_val);
        } else if (rhs_val.type == TYPE_INT) {
            rhs = static_cast<double>(rhs_val.int_val);
        } else {
            throw IncompatibleTypeError(coltype2str(lhs_col.type), coltype2str(rhs_val.type));
        }
        return compare_number_value(lhs, rhs);
    }
    if (lhs_col.type == TYPE_FLOAT) {
        double lhs = static_cast<double>(*(const float *)lhs_data);
        double rhs = 0;
        if (rhs_val.type == TYPE_FLOAT) {
            rhs = static_cast<double>(rhs_val.float_val);
        } else if (rhs_val.type == TYPE_INT) {
            rhs = static_cast<double>(rhs_val.int_val);
        } else {
            throw IncompatibleTypeError(coltype2str(lhs_col.type), coltype2str(rhs_val.type));
        }
        return compare_number_value(lhs, rhs);
    }
    if (lhs_col.type == TYPE_STRING) {
        std::vector<char> rhs(lhs_col.len, 0);
        if (rhs_val.type == TYPE_STRING) {
            int copy_len = std::min(lhs_col.len, static_cast<int>(rhs_val.str_val.size()));
            if (copy_len > 0) {
                memcpy(rhs.data(), rhs_val.str_val.data(), copy_len);
            }
        } else if (rhs_val.raw != nullptr) {
            int copy_len = std::min(lhs_col.len, rhs_val.raw->size);
            if (copy_len > 0) {
                memcpy(rhs.data(), rhs_val.raw->data, copy_len);
            }
        } else {
            throw IncompatibleTypeError(coltype2str(lhs_col.type), coltype2str(rhs_val.type));
        }
        return compare_string_value(lhs_data, rhs.data(), lhs_col.len);
    }
    throw InternalError("Unexpected data type");
}

inline int compare_record_by_cols(const RmRecord &a, const RmRecord &b, const std::vector<ColMeta> &cols) {
    for (const auto &col : cols) {
        int cmp = compare_col_value(a.data + col.offset, b.data + col.offset, col.type, col.len);
        if (cmp != 0) {
            return cmp;
        }
    }
    return 0;
}

inline bool eval_comp(int cmp, CompOp op) {
    switch (op) {
        case OP_EQ:
            return cmp == 0;
        case OP_NE:
            return cmp != 0;
        case OP_LT:
            return cmp < 0;
        case OP_GT:
            return cmp > 0;
        case OP_LE:
            return cmp <= 0;
        case OP_GE:
            return cmp >= 0;
        default:
            throw InternalError("Unexpected comparison operator");
    }
}

inline const ColMeta *find_col(const std::vector<ColMeta> &cols, const TabCol &target) {
    auto pos = std::find_if(cols.begin(), cols.end(), [&](const ColMeta &col) {
        return col.tab_name == target.tab_name && col.name == target.col_name;
    });
    if (pos == cols.end()) {
        pos = std::find_if(cols.begin(), cols.end(), [&](const ColMeta &col) {
            return col.name == target.col_name;
        });
    }
    if (pos == cols.end()) {
        throw ColumnNotFoundError(target.tab_name + '.' + target.col_name);
    }
    return &(*pos);
}

inline bool eval_condition(const RmRecord &rec, const Condition &cond, const std::vector<ColMeta> &cols) {
    const ColMeta *lhs_col = find_col(cols, cond.lhs_col);
    const char *lhs_data = rec.data + lhs_col->offset;

    int cmp;
    if (cond.is_rhs_val) {
        cmp = compare_col_to_value(lhs_data, *lhs_col, cond.rhs_val);
    } else {
        const ColMeta *rhs_col = find_col(cols, cond.rhs_col);
        cmp = compare_col_value(lhs_data, rec.data + rhs_col->offset, lhs_col->type, lhs_col->len);
    }
    return eval_comp(cmp, cond.op);
}

inline bool eval_conditions(const RmRecord &rec, const std::vector<Condition> &conds, const std::vector<ColMeta> &cols) {
    for (const auto &cond : conds) {
        if (!eval_condition(rec, cond, cols)) {
            return false;
        }
    }
    return true;
}
