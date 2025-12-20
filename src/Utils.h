#pragma once

#include <string>

extern bool DEBUG;

enum class ValueType { INT, FLOAT, STRING, NONE };

struct Value {
    ValueType type = ValueType::NONE;
    long long int_val = 0;
    double float_val = 0.0;
    std::string str_val;

    Value() = default;
    Value(long long v) : type(ValueType::INT), int_val(v) {}
    Value(double v) : type(ValueType::FLOAT), float_val(v) {}
    Value(std::string v) : type(ValueType::STRING), str_val(std::move(v)) {}

    std::string to_string(const std::string& format = "") const {
        if (!format.empty()) {
            if (format == "{int}" && type == ValueType::INT) return std::to_string(int_val);
            if (format == "{float}" && type == ValueType::FLOAT) return std::to_string(float_val);
            if (format == "{string}" && type == ValueType::STRING) return str_val;
        }
        switch (type) {
            case ValueType::INT: return std::to_string(int_val);
            case ValueType::FLOAT: return std::to_string(float_val);
            case ValueType::STRING: return str_val;
            default: return "";
        }
    }
};