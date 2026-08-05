#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/common.h"
#include "execution/execution_result.h"
#include "system/sm.h"
#include "optimizer/plan.h"

namespace rmdb::execution {

class ParameterBindingError : public std::runtime_error {
 public:
    explicit ParameterBindingError(const std::string &message)
        : std::runtime_error(message) {}
};

inline wire::SqlType wire_type_for(ColType type) {
    switch (type) {
        case TYPE_INT:
            return wire::SqlType::INT32;
        case TYPE_FLOAT:
            return wire::SqlType::FLOAT32;
        case TYPE_STRING:
            return wire::SqlType::CHAR;
    }
    throw ParameterBindingError("unsupported SQL parameter type");
}

inline void bind_parameter_value(Value &slot, const TypedValue &parameter) {
    if (!slot.is_param) {
        return;
    }
    if (!parameter.present()) {
        throw ParameterBindingError(
            "SQL NULL parameters are not supported by this engine");
    }
    if (parameter.type() != wire_type_for(slot.parameter_declared_type)) {
        throw ParameterBindingError(
            "bound value type differs from PREPARE declaration");
    }

    const int raw_size = slot.raw == nullptr ? 0 : slot.raw->size;
    slot.raw.reset();
    switch (slot.type) {
        case TYPE_INT:
            if (parameter.type() != wire::SqlType::INT32) {
                throw ParameterBindingError(
                    "parameter cannot be converted to INT");
            }
            slot.set_int(parameter.int32_value());
            break;
        case TYPE_FLOAT:
            if (parameter.type() == wire::SqlType::FLOAT32) {
                float value = parameter.float_value();
                if (!std::isfinite(value)) {
                    throw ParameterBindingError(
                        "FLOAT32 parameter must be finite");
                }
                slot.set_float(value);
            } else if (parameter.type() == wire::SqlType::INT32) {
                slot.set_float(
                    static_cast<float>(parameter.int32_value()));
            } else {
                throw ParameterBindingError(
                    "parameter cannot be converted to FLOAT");
            }
            break;
        case TYPE_STRING:
            if (parameter.type() != wire::SqlType::CHAR) {
                throw ParameterBindingError(
                    "parameter cannot be converted to CHAR");
            }
            slot.set_str(parameter.char_value());
            break;
        default:
            throw ParameterBindingError("unsupported bound target type");
    }
    slot.is_param = true;
    if (raw_size > 0) {
        slot.init_raw(raw_size);
    }
}

inline void bind_condition_parameters(
    std::vector<Condition> &conditions,
    const std::vector<TypedValue> &parameters) {
    for (auto &condition : conditions) {
        if (!condition.is_rhs_val || !condition.rhs_val.is_param) {
            continue;
        }
        if (condition.rhs_val.param_index >= parameters.size()) {
            throw ParameterBindingError(
                "parameter ordinal is outside the bound vector");
        }
        bind_parameter_value(
            condition.rhs_val,
            parameters[condition.rhs_val.param_index]);
    }
}

inline void bind_plan_parameters(
    const std::shared_ptr<Plan> &plan,
    const std::vector<TypedValue> &parameters) {
    if (plan == nullptr) {
        return;
    }

    if (auto dml = std::dynamic_pointer_cast<DMLPlan>(plan)) {
        for (auto &value : dml->values_) {
            if (value.is_param) {
                if (value.param_index >= parameters.size()) {
                    throw ParameterBindingError(
                        "parameter ordinal is outside the bound vector");
                }
                bind_parameter_value(
                    value, parameters[value.param_index]);
            }
        }
        bind_condition_parameters(dml->conds_, parameters);
        for (auto &set_clause : dml->set_clauses_) {
            if (!set_clause.arithmetic_terms.empty()) {
                for (auto &term : set_clause.arithmetic_terms) {
                    auto &operand = term.second;
                    if (!operand.is_param) {
                        continue;
                    }
                    if (operand.param_index >= parameters.size()) {
                        throw ParameterBindingError(
                            "parameter ordinal is outside the bound vector");
                    }
                    bind_parameter_value(
                        operand, parameters[operand.param_index]);
                }
            } else if (set_clause.rhs.is_param) {
                if (set_clause.rhs.param_index >= parameters.size()) {
                    throw ParameterBindingError(
                        "parameter ordinal is outside the bound vector");
                }
                bind_parameter_value(
                    set_clause.rhs,
                    parameters[set_clause.rhs.param_index]);
            }
        }
        bind_plan_parameters(dml->subplan_, parameters);
        return;
    }
    if (auto scan = std::dynamic_pointer_cast<ScanPlan>(plan)) {
        bind_condition_parameters(scan->conds_, parameters);
        bind_condition_parameters(scan->fed_conds_, parameters);
        return;
    }
    if (auto filter = std::dynamic_pointer_cast<FilterPlan>(plan)) {
        bind_condition_parameters(filter->conds_, parameters);
        bind_plan_parameters(filter->subplan_, parameters);
        return;
    }
    if (auto join = std::dynamic_pointer_cast<JoinPlan>(plan)) {
        bind_condition_parameters(join->conds_, parameters);
        bind_plan_parameters(join->left_, parameters);
        bind_plan_parameters(join->right_, parameters);
        return;
    }
    if (auto projection = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
        bind_plan_parameters(projection->subplan_, parameters);
        return;
    }
    if (auto sort = std::dynamic_pointer_cast<SortPlan>(plan)) {
        bind_plan_parameters(sort->subplan_, parameters);
        return;
    }
    if (auto aggregate = std::dynamic_pointer_cast<AggregatePlan>(plan)) {
        for (auto &having : aggregate->havings_) {
            if (!having.rhs_val.is_param) {
                continue;
            }
            if (having.rhs_val.param_index >= parameters.size()) {
                throw ParameterBindingError(
                    "parameter ordinal is outside the bound vector");
            }
            bind_parameter_value(
                having.rhs_val,
                parameters[having.rhs_val.param_index]);
        }
        bind_plan_parameters(aggregate->subplan_, parameters);
        return;
    }
    if (auto union_plan = std::dynamic_pointer_cast<UnionPlan>(plan)) {
        for (const auto &branch : union_plan->branches_) {
            bind_plan_parameters(branch, parameters);
        }
    }
}

}  // namespace rmdb::execution
