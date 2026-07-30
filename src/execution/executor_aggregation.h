#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "system/sm_meta.h"
#include "execution_defs.h"
#include "execution_eval.h"
#include "executor_abstract.h"
#include "optimizer/plan.h"

class AggregationExecutor : public AbstractExecutor {
   private:
    struct AggState {
        ColType input_type = TYPE_INT;
        bool has_value = false;
        int count = 0;
        double sum = 0.0;
        std::string min_bin;
        std::string max_bin;
        std::unordered_set<std::string> distinct_values;
    };

    struct GroupState {
        std::vector<std::pair<ColType, std::string>> group_vals;
        std::unordered_map<std::string, AggState> agg_states;
    };

    std::unique_ptr<AbstractExecutor> prev_;
    AggregatePlan *plan_;
    std::vector<ColMeta> cols_;
    size_t len_ = 0;
    std::vector<std::unique_ptr<RmRecord>> out_;
    size_t idx_ = 0;
    std::vector<ColMeta> in_cols_;

   private:
    static std::string agg_key(const AggExpr &agg) {
        return std::to_string(static_cast<int>(agg.type)) + "|" +
               std::to_string(agg.is_star ? 1 : 0) + "|" +
               std::to_string(agg.is_distinct ? 1 : 0) + "|" +
               agg.col.tab_name + "." + agg.col.col_name;
    }

    const ColMeta &find_col(const TabCol &col) const {
        auto it = std::find_if(in_cols_.begin(), in_cols_.end(), [&](const ColMeta &m) {
            return (col.tab_name.empty() || m.tab_name == col.tab_name) && m.name == col.col_name;
        });
        if (it == in_cols_.end()) {
            throw ColumnNotFoundError(col.tab_name + "." + col.col_name);
        }
        return *it;
    }

    int find_order_col_idx(const OrderByItem &ob) const {
        int agg_match = -1;
        for (size_t i = 0; i < plan_->select_items_.size(); i++) {
            const auto &sel = plan_->select_items_[i];
            if (!sel.alias.empty() && sel.alias == ob.col.col_name) {
                return static_cast<int>(i);
            }
            if (!sel.is_agg && !ob.is_agg &&
                sel.col.col_name == ob.col.col_name &&
                (ob.col.tab_name.empty() || sel.col.tab_name == ob.col.tab_name ||
                 sel.col.tab_name.empty())) {
                return static_cast<int>(i);
            }
            if (sel.is_agg && !sel.agg.is_star &&
                sel.agg.col.col_name == ob.col.col_name &&
                (ob.col.tab_name.empty() || sel.agg.col.tab_name == ob.col.tab_name ||
                 sel.agg.col.tab_name.empty())) {
                if (agg_match >= 0) {
                    agg_match = -2;
                } else {
                    agg_match = static_cast<int>(i);
                }
            }
        }
        if (agg_match >= 0) {
            return agg_match;
        }
        return -1;
    }

    std::pair<ColType, std::string> read_col_bin(const RmRecord &rec, const TabCol &col) const {
        const auto &cm = find_col(col);
        const char *p = rec.data + cm.offset;
        if (cm.type == TYPE_INT) {
            int v = *(int *)p;
            return {TYPE_INT, std::string(reinterpret_cast<const char *>(&v), sizeof(int))};
        }
        if (cm.type == TYPE_FLOAT) {
            float v = *(float *)p;
            return {TYPE_FLOAT, std::string(reinterpret_cast<const char *>(&v), sizeof(float))};
        }
        return {TYPE_STRING, std::string(p, cm.len)};
    }

    static int cmp_bin(ColType lt, const std::string &lv, ColType rt, const std::string &rv) {
        if ((lt == TYPE_INT || lt == TYPE_FLOAT) && (rt == TYPE_INT || rt == TYPE_FLOAT)) {
            double l = (lt == TYPE_INT) ? static_cast<double>(*(int *)lv.data()) : static_cast<double>(*(float *)lv.data());
            double r = (rt == TYPE_INT) ? static_cast<double>(*(int *)rv.data()) : static_cast<double>(*(float *)rv.data());
            if (l < r) return -1;
            if (l > r) return 1;
            return 0;
        }
        if (lt == TYPE_STRING && rt == TYPE_STRING) {
            auto normalize = [](std::string s) {
                size_t null_pos = s.find('\0');
                if (null_pos != std::string::npos) {
                    s.erase(null_pos);
                }
                while (!s.empty() && s.back() == ' ') {
                    s.pop_back();
                }
                return s;
            };
            std::string l = normalize(lv);
            std::string r = normalize(rv);
            if (l < r) return -1;
            if (l > r) return 1;
            return 0;
        }
        throw RMDBError("failure");
    }

    static bool eval_cmp(int cmp, CompOp op) {
        switch (op) {
            case OP_EQ: return cmp == 0;
            case OP_NE: return cmp != 0;
            case OP_LT: return cmp < 0;
            case OP_GT: return cmp > 0;
            case OP_LE: return cmp <= 0;
            case OP_GE: return cmp >= 0;
            default: throw RMDBError("failure");
        }
    }

    static std::pair<ColType, std::string> value_to_bin(const Value &v) {
        if (v.type == TYPE_INT) {
            int x = v.int_val;
            return {TYPE_INT, std::string(reinterpret_cast<const char *>(&x), sizeof(int))};
        }
        if (v.type == TYPE_FLOAT) {
            float x = v.float_val;
            return {TYPE_FLOAT, std::string(reinterpret_cast<const char *>(&x), sizeof(float))};
        }
        return {TYPE_STRING, v.str_val};
    }

    static std::pair<ColType, std::string> agg_result(const AggExpr &agg, const AggState &st) {
        switch (agg.type) {
            case AGG_COUNT: {
                int x = st.count;
                return {TYPE_INT, std::string(reinterpret_cast<const char *>(&x), sizeof(int))};
            }
            case AGG_SUM: {
                if (st.input_type == TYPE_INT) {
                    int x = static_cast<int>(st.sum);
                    return {TYPE_INT, std::string(reinterpret_cast<const char *>(&x), sizeof(int))};
                }
                float x = static_cast<float>(st.sum);
                if (!std::isfinite(x)) {
                    throw RMDBError("SUM(FLOAT) result must be finite");
                }
                return {TYPE_FLOAT, std::string(reinterpret_cast<const char *>(&x), sizeof(float))};
            }
            case AGG_AVG: {
                float x = (st.count == 0) ? 0.0f : static_cast<float>(st.sum / st.count);
                if (!std::isfinite(x)) {
                    throw RMDBError("AVG result must be finite");
                }
                return {TYPE_FLOAT, std::string(reinterpret_cast<const char *>(&x), sizeof(float))};
            }
            case AGG_MAX:
                if (st.max_bin.empty()) {
                    if (st.input_type == TYPE_FLOAT) {
                        float x = 0.0f;
                        return {TYPE_FLOAT, std::string(reinterpret_cast<const char *>(&x), sizeof(float))};
                    }
                    int x = 0;
                    return {TYPE_INT, std::string(reinterpret_cast<const char *>(&x), sizeof(int))};
                }
                return {st.input_type, st.max_bin};
            case AGG_MIN:
                if (st.min_bin.empty()) {
                    if (st.input_type == TYPE_FLOAT) {
                        float x = 0.0f;
                        return {TYPE_FLOAT, std::string(reinterpret_cast<const char *>(&x), sizeof(float))};
                    }
                    int x = 0;
                    return {TYPE_INT, std::string(reinterpret_cast<const char *>(&x), sizeof(int))};
                }
                return {st.input_type, st.min_bin};
            default:
                throw RMDBError("failure");
        }
    }

    void update_agg(const AggExpr &agg, AggState &st, const RmRecord &rec) const {
        if (agg.type == AGG_COUNT) {
            if (agg.is_distinct) {
                auto value = read_col_bin(rec, agg.col);
                std::string key(1, static_cast<char>(value.first));
                if (value.first == TYPE_FLOAT) {
                    float numeric = 0.0F;
                    std::memcpy(&numeric, value.second.data(),
                                sizeof(numeric));
                    if (!std::isfinite(numeric)) {
                        throw RMDBError(
                            "COUNT(DISTINCT FLOAT) input must be finite");
                    }
                    if (numeric == 0.0F) {
                        numeric = 0.0F;
                        value.second.assign(
                            reinterpret_cast<const char *>(&numeric),
                            sizeof(numeric));
                    }
                }
                key.append(value.second);
                if (!st.distinct_values.insert(std::move(key)).second) {
                    return;
                }
            }
            st.count++;
            st.has_value = true;
            return;
        }
        auto v = read_col_bin(rec, agg.col);
        st.input_type = v.first;
        st.count++;
        st.has_value = true;
        if (v.first == TYPE_INT) {
            st.sum += static_cast<double>(*(int *)v.second.data());
        } else if (v.first == TYPE_FLOAT) {
            float input = 0.0F;
            std::memcpy(&input, v.second.data(), sizeof(input));
            if (!std::isfinite(input)) {
                throw RMDBError("SUM(FLOAT) input must be finite");
            }
            st.sum += static_cast<double>(input);
            if (!std::isfinite(st.sum)) {
                throw RMDBError("SUM(FLOAT) accumulator must be finite");
            }
        }
        if (st.min_bin.empty() || cmp_bin(v.first, v.second, v.first, st.min_bin) < 0) {
            st.min_bin = v.second;
        }
        if (st.max_bin.empty() || cmp_bin(v.first, v.second, v.first, st.max_bin) > 0) {
            st.max_bin = v.second;
        }
    }

    bool pass_having(const GroupState &group) const {
        for (auto &h : plan_->havings_) {
            auto it = group.agg_states.find(agg_key(h.lhs));
            AggState empty;
            auto lhs = agg_result(h.lhs, it == group.agg_states.end() ? empty : it->second);
            auto rhs = value_to_bin(h.rhs_val);
            if (!eval_cmp(cmp_bin(lhs.first, lhs.second, rhs.first, rhs.second), h.op)) {
                return false;
            }
        }
        return true;
    }

    std::string make_group_key(const std::vector<std::pair<ColType, std::string>> &gvals) const {
        std::string k;
        for (auto &v : gvals) {
            k += std::to_string(static_cast<int>(v.first)) + ":" + std::to_string(v.second.size()) + ":";
            k += v.second;
            k += "|";
        }
        return k;
    }

   public:
    AggregationExecutor(std::unique_ptr<AbstractExecutor> prev, AggregatePlan *plan) : prev_(std::move(prev)), plan_(plan) {
        in_cols_ = prev_->cols();
        int off = 0;
        for (auto &item : plan_->select_items_) {
            ColMeta c;
            c.tab_name = "";
            c.name = item.alias.empty() ? (item.is_agg ? "agg" : item.col.col_name) : item.alias;
            if (!item.is_agg) {
                auto cm = find_col(item.col);
                c.type = cm.type;
                c.len = cm.len;
            } else if (item.agg.type == AGG_COUNT) {
                c.type = TYPE_INT;
                c.len = sizeof(int);
            } else if (item.agg.type == AGG_AVG) {
                c.type = TYPE_FLOAT;
                c.len = sizeof(float);
            } else if (item.agg.type == AGG_SUM) {
                auto cm = find_col(item.agg.col);
                c.type = cm.type == TYPE_INT ? TYPE_INT : TYPE_FLOAT;
                c.len = (c.type == TYPE_INT) ? sizeof(int) : sizeof(float);
            } else {
                auto cm = find_col(item.agg.col);
                c.type = cm.type;
                c.len = cm.len;
            }
            c.offset = off;
            off += c.len;
            cols_.push_back(c);
        }
        len_ = off;
    }

    size_t tupleLen() const override { return len_; }
    const std::vector<ColMeta> &cols() const override { return cols_; }
    bool is_end() const override { return idx_ >= out_.size(); }
    Rid &rid() override { return _abstract_rid; }
    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols_, target); }

    void beginTuple() override {
        out_.clear();
        idx_ = 0;
        prev_->beginTuple();

        // Merge SELECT/HAVING aggregates and update each aggregate once per input row.
        // Otherwise duplicated aggregate expressions (e.g., same COUNT in multiple HAVING conditions)
        // would be accumulated multiple times and produce incorrect results.
        std::unordered_map<std::string, AggExpr> required_aggs;
        for (auto &item : plan_->select_items_) {
            if (item.is_agg) {
                required_aggs.emplace(agg_key(item.agg), item.agg);
            }
        }
        for (auto &h : plan_->havings_) {
            required_aggs.emplace(agg_key(h.lhs), h.lhs);
        }

        std::unordered_map<std::string, GroupState> groups;
        std::vector<std::string> group_order;
        if (plan_->group_bys_.empty()) {
            // Scalar aggregates always have exactly one logical group,
            // including the empty-input case. Avoid constructing an empty
            // group-key vector/string and probing an unordered_map per row.
            GroupState scalar_group;
            while (!prev_->is_end()) {
                auto rec = prev_->Next();
                for (auto &kv : required_aggs) {
                    update_agg(kv.second,
                               scalar_group.agg_states[kv.first], *rec);
                }
                prev_->nextTuple();
            }
            groups.emplace("", std::move(scalar_group));
            group_order.push_back("");
        } else {
            while (!prev_->is_end()) {
                auto rec = prev_->Next();
                std::vector<std::pair<ColType, std::string>> gvals;
                for (auto &g : plan_->group_bys_) {
                    gvals.push_back(read_col_bin(*rec, g));
                }
                auto gk = make_group_key(gvals);
                if (groups.find(gk) == groups.end()) {
                    GroupState st;
                    st.group_vals = gvals;
                    groups[gk] = st;
                    group_order.push_back(gk);
                }
                auto &st = groups[gk];
                for (auto &kv : required_aggs) {
                    update_agg(kv.second, st.agg_states[kv.first], *rec);
                }
                prev_->nextTuple();
            }
        }

        for (auto &k : group_order) {
            auto &g = groups[k];
            if (!pass_having(g)) continue;
            auto row = std::make_unique<RmRecord>(len_);
            for (size_t i = 0; i < plan_->select_items_.size(); i++) {
                const auto &item = plan_->select_items_[i];
                char *dst = row->data + cols_[i].offset;
                if (!item.is_agg) {
                    int gi = -1;
                    for (size_t j = 0; j < plan_->group_bys_.size(); j++) {
                        if (plan_->group_bys_[j].col_name == item.col.col_name &&
                            (plan_->group_bys_[j].tab_name == item.col.tab_name ||
                             plan_->group_bys_[j].tab_name.empty() || item.col.tab_name.empty())) {
                            gi = static_cast<int>(j);
                            break;
                        }
                    }
                    if (gi < 0) throw RMDBError("failure");
                    auto &v = g.group_vals[gi];
                    std::memcpy(dst, v.second.data(), cols_[i].len);
                } else {
                    AggState empty;
                    auto it = g.agg_states.find(agg_key(item.agg));
                    auto res = agg_result(item.agg, it == g.agg_states.end() ? empty : it->second);
                    std::memcpy(dst, res.second.data(), cols_[i].len);
                }
            }
            out_.push_back(std::move(row));
        }

        if (!plan_->order_bys_.empty()) {
            std::vector<std::pair<int, bool>> order_idxs;
            for (auto &ob : plan_->order_bys_) {
                int order_idx = find_order_col_idx(ob);
                if (order_idx >= 0) {
                    order_idxs.emplace_back(order_idx, ob.is_desc);
                }
            }
            if (!order_idxs.empty()) {
                std::stable_sort(out_.begin(), out_.end(),
                                 [&](const std::unique_ptr<RmRecord> &a, const std::unique_ptr<RmRecord> &b) {
                    for (auto &[order_idx, is_desc] : order_idxs) {
                        const auto &c = cols_[order_idx];
                        std::string av(a->data + c.offset, c.len);
                        std::string bv(b->data + c.offset, c.len);
                        int cmp = cmp_bin(c.type, av, c.type, bv);
                        if (cmp != 0) {
                            return is_desc ? (cmp > 0) : (cmp < 0);
                        }
                    }
                    return false;
                });
            }
        }

        if (plan_->limit_num_ >= 0 && static_cast<int>(out_.size()) > plan_->limit_num_) {
            out_.resize(plan_->limit_num_);
        }
        if (plan_->limit_num_ == 0) {
            out_.clear();
        }
        plan_->rows_ = out_.size();
    }

    void nextTuple() override { idx_++; }
    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) return nullptr;
        return std::make_unique<RmRecord>(*out_[idx_]);
    }
};
