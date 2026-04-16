#pragma once

#include <string>
#include <utility>

extern bool DEBUG;

enum class ValueType { INT, FLOAT, STRING, BOOL, BYTES, NONE };

struct Value {
    ValueType type = ValueType::NONE;
    long long int_val = 0;
    double float_val = 0.0;
    bool bool_val = false;
    std::string str_val;

    Value() = default;
    Value(long long v) : type(ValueType::INT), int_val(v) {}
    Value(double v) : type(ValueType::FLOAT), float_val(v) {}
    Value(std::string v) : type(ValueType::STRING), str_val(std::move(v)) {}

    static Value Bool(bool v) {
        Value value;
        value.type = ValueType::BOOL;
        value.bool_val = v;
        return value;
    }

    static Value Bytes(std::string v) {
        Value value;
        value.type = ValueType::BYTES;
        value.str_val = std::move(v);
        return value;
    }

    std::string to_string(const std::string& format = "") const {
        if (!format.empty()) {
            if (format == "{int}" && type == ValueType::INT) return std::to_string(int_val);
            if (format == "{float}" && type == ValueType::FLOAT) return std::to_string(float_val);
            if (format == "{string}" && type == ValueType::STRING) return str_val;
            if (format == "{bool}" && type == ValueType::BOOL) return bool_val ? "true" : "false";
            if (format == "{bytes}" && type == ValueType::BYTES) return str_val;
        }
        switch (type) {
            case ValueType::INT: return std::to_string(int_val);
            case ValueType::FLOAT: return std::to_string(float_val);
            case ValueType::STRING: return str_val;
            case ValueType::BOOL: return bool_val ? "true" : "false";
            case ValueType::BYTES: return str_val;
            default: return "";
        }
    }
};