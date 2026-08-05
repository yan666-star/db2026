#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "execution/parameter_binding.h"

namespace {

int failures = 0;

void expect_true(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        failures++;
    }
}

template <typename Fn>
void expect_binding_error(Fn fn, const std::string &message) {
    try {
        fn();
        expect_true(false, message);
    } catch (const rmdb::execution::ParameterBindingError &) {
    }
}

Value parameter_value(ColType declared_type, ColType target_type,
                      uint16_t index, int raw_size) {
    Value value;
    value.is_param = true;
    value.param_index = index;
    value.parameter_declared_type = declared_type;
    value.type = target_type;
    if (target_type == TYPE_INT) {
        value.int_val = 0;
    } else if (target_type == TYPE_FLOAT) {
        value.float_val = 0.0F;
    }
    if (raw_size > 0) {
        value.init_raw(raw_size);
    }
    return value;
}

void test_float_bind_preserves_binary32_value_and_raw_storage() {
    Value value = parameter_value(TYPE_FLOAT, TYPE_FLOAT, 0, sizeof(float));
    const auto parameter =
        rmdb::execution::TypedValue::FloatBits(0x3f800001U);
    rmdb::execution::bind_parameter_value(value, parameter);

    uint32_t raw_bits = 0;
    std::memcpy(&raw_bits, value.raw->data, sizeof(raw_bits));
    expect_true(raw_bits == 0x3f800001U,
                "FLOAT32 bind must preserve bits in executor raw storage");
}

void test_char_bind_uses_storage_width_without_sql_escaping() {
    Value value = parameter_value(TYPE_STRING, TYPE_STRING, 1, 8);
    const auto parameter = rmdb::execution::TypedValue::Char("a'$2");
    rmdb::execution::bind_parameter_value(value, parameter);

    expect_true(value.str_val == "a'$2",
                "CHAR bind must retain quotes and dollar text as data");
    expect_true(std::string(value.raw->data, 4) == "a'$2",
                "CHAR bind must write bytes directly into fixed storage");
    expect_true(value.raw->data[4] == '\0',
                "CHAR bind must retain NUL storage padding internally");
}

void test_null_and_declared_type_mismatch_are_rejected() {
    Value integer = parameter_value(TYPE_INT, TYPE_INT, 0, sizeof(int));
    expect_binding_error(
        [&] {
            rmdb::execution::bind_parameter_value(
                integer,
                rmdb::execution::TypedValue::Null(
                    rmdb::wire::SqlType::INT32));
        },
        "Current SQL engine must reject protocol NULL before execution");
    expect_binding_error(
        [&] {
            rmdb::execution::bind_parameter_value(
                integer, rmdb::execution::TypedValue::Char("1"));
        },
        "Typed bind must match the PREPARE declaration");
}

void test_non_finite_float_bind_is_rejected() {
    Value value = parameter_value(
        TYPE_FLOAT, TYPE_FLOAT, 0, sizeof(float));
    expect_binding_error(
        [&] {
            rmdb::execution::bind_parameter_value(
                value,
                rmdb::execution::TypedValue::FloatBits(0x7f800000U));
        },
        "FLOAT32 positive infinity must be rejected");
    expect_binding_error(
        [&] {
            rmdb::execution::bind_parameter_value(
                value,
                rmdb::execution::TypedValue::FloatBits(0x7fc00000U));
        },
        "FLOAT32 NaN must be rejected");
}

void test_chained_update_binds_every_arithmetic_operand() {
    SetClause clause;
    clause.is_arithmetic = true;
    clause.arithmetic_terms.emplace_back(
        '-', parameter_value(TYPE_INT, TYPE_INT, 0, sizeof(int)));
    clause.arithmetic_terms.emplace_back(
        '+', parameter_value(TYPE_INT, TYPE_INT, 1, sizeof(int)));
    auto plan = std::make_shared<DMLPlan>(
        T_Update, nullptr, "fixture", std::vector<Value>{},
        std::vector<Condition>{}, std::vector<SetClause>{clause});

    rmdb::execution::bind_plan_parameters(
        plan, {rmdb::execution::TypedValue::Int32(1),
               rmdb::execution::TypedValue::Int32(91)});

    expect_true(plan->set_clauses_[0].arithmetic_terms[0].second.int_val == 1,
                "first chained UPDATE operand must be bound");
    expect_true(plan->set_clauses_[0].arithmetic_terms[1].second.int_val == 91,
                "second chained UPDATE operand must be bound");
}

}  // namespace

int main() {
    test_float_bind_preserves_binary32_value_and_raw_storage();
    test_char_bind_uses_storage_width_without_sql_escaping();
    test_null_and_declared_type_mismatch_are_rejected();
    test_non_finite_float_bind_is_rejected();
    test_chained_update_binds_every_arithmetic_operand();

    if (failures != 0) {
        std::cerr << failures << " parameter binding test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "parameter_binding_test: PASS\n";
    return EXIT_SUCCESS;
}
