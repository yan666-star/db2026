#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "network/wire_protocol.h"

namespace rmdb::execution {

inline std::string logical_char_bytes(const char *data, size_t storage_size) {
    if (data == nullptr) {
        if (storage_size == 0U) {
            return {};
        }
        throw std::invalid_argument(
            "CHAR storage pointer is null for a non-empty value");
    }

    size_t logical_size = storage_size;
    while (logical_size > 0U && data[logical_size - 1U] == '\0') {
        logical_size--;
    }
    return std::string(data, logical_size);
}

class TypedValue {
 public:
    static TypedValue Int32(int32_t value) {
        TypedValue result(wire::SqlType::INT32, true);
        result.int32_value_ = value;
        return result;
    }

    static TypedValue Float(float value) {
        static_assert(sizeof(value) == sizeof(uint32_t),
                      "Wire FLOAT32 requires an IEEE-754 binary32 value");
        TypedValue result(wire::SqlType::FLOAT32, true);
        std::memcpy(&result.float_bits_, &value, sizeof(value));
        return result;
    }

    static TypedValue FloatBits(uint32_t bits) {
        TypedValue result(wire::SqlType::FLOAT32, true);
        result.float_bits_ = bits;
        return result;
    }

    static TypedValue Char(std::string value) {
        TypedValue result(wire::SqlType::CHAR, true);
        result.char_value_ = std::move(value);
        return result;
    }

    static TypedValue Null(wire::SqlType type) {
        return TypedValue(type, false);
    }

    wire::SqlType type() const noexcept { return type_; }
    bool present() const noexcept { return present_; }

    int32_t int32_value() const {
        require_value(wire::SqlType::INT32);
        return int32_value_;
    }

    uint32_t float_bits() const {
        require_value(wire::SqlType::FLOAT32);
        return float_bits_;
    }

    float float_value() const {
        const uint32_t bits = float_bits();
        float value = 0.0F;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    const std::string &char_value() const {
        require_value(wire::SqlType::CHAR);
        return char_value_;
    }

 private:
    TypedValue(wire::SqlType type, bool present)
        : type_(type), present_(present) {}

    void require_value(wire::SqlType expected) const {
        if (!present_) {
            throw std::logic_error("attempted to read a protocol NULL value");
        }
        if (type_ != expected) {
            throw std::logic_error("typed value accessed through wrong type");
        }
    }

    wire::SqlType type_;
    bool present_;
    int32_t int32_value_{0};
    uint32_t float_bits_{0};
    std::string char_value_;
};

struct OutputColumn {
    std::string name;
    wire::SqlType type;
};

class ResultSink {
 public:
    virtual ~ResultSink() = default;

    virtual void begin_query(const std::vector<OutputColumn> &schema) = 0;
    virtual void push_row(const std::vector<TypedValue> &row) = 0;
    virtual void end_query(uint64_t row_count) = 0;
    virtual void command_ok() = 0;
};

}  // namespace rmdb::execution
