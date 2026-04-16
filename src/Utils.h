#pragma once

#include <string>
#include <utility>
#include <map>
#include <vector>

extern bool DEBUG;

enum class ValueType { INT, FLOAT, STRING, BOOL, BYTES, OBJECT, ARRAY, NONE };

struct Value {
    ValueType type = ValueType::NONE;
    long long int_val = 0;
    double float_val = 0.0;
    bool bool_val = false;
    std::string str_val;
    std::map<std::string, Value> obj_val;
    std::vector<Value> arr_val;

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

    static Value Object(std::map<std::string, Value> v) {
        Value value;
        value.type = ValueType::OBJECT;
        value.obj_val = std::move(v);
        return value;
    }

    static Value Array(std::vector<Value> v) {
        Value value;
        value.type = ValueType::ARRAY;
        value.arr_val = std::move(v);
        return value;
    }

    std::string to_string(const std::string& format = "") const {
        if (!format.empty()) {
            if (format == "{int}" && type == ValueType::INT) return std::to_string(int_val);
            if (format == "{int}" && type == ValueType::BOOL) return std::to_string(bool_val ? 1 : 0);
            if (format == "{float}" && type == ValueType::FLOAT) return std::to_string(float_val);
            if (format == "{float}" && type == ValueType::INT) return std::to_string(static_cast<double>(int_val));
            if (format == "{string}" && type == ValueType::STRING) return str_val;
            if (format == "{string}" && type == ValueType::BYTES) return str_val;
            if (format == "{bool}" && type == ValueType::BOOL) return bool_val ? "true" : "false";
            if (format == "{bool}" && type == ValueType::INT) return int_val != 0 ? "true" : "false";
            if (format == "{bool}" && type == ValueType::FLOAT) return float_val != 0.0 ? "true" : "false";
            if (format == "{bool}" && (type == ValueType::STRING || type == ValueType::BYTES)) return str_val.empty() ? "false" : "true";
            if (format == "{bytes}" && type == ValueType::BYTES) return str_val;
            if (format == "{bytes}" && type == ValueType::STRING) return str_val;
        }
        switch (type) {
            case ValueType::INT: return std::to_string(int_val);
            case ValueType::FLOAT: return std::to_string(float_val);
            case ValueType::STRING: return str_val;
            case ValueType::BOOL: return bool_val ? "true" : "false";
            case ValueType::BYTES: return str_val;
            case ValueType::OBJECT: {
                std::string json = "{";
                for (auto it = obj_val.begin(); it != obj_val.end(); ++it) {
                    if (it != obj_val.begin()) json += ",";
                    json += "\"" + it->first + "\":" + it->second.to_json();
                }
                json += "}";
                return json;
            }
            case ValueType::ARRAY: {
                std::string json = "[";
                for (size_t i = 0; i < arr_val.size(); ++i) {
                    if (i > 0) json += ",";
                    json += arr_val[i].to_json();
                }
                json += "]";
                return json;
            }
            default: return "";
        }
    }

    std::string to_json() const {
        switch (type) {
            case ValueType::INT: return std::to_string(int_val);
            case ValueType::FLOAT: return std::to_string(float_val);
            case ValueType::STRING: return "\"" + str_val + "\"";
            case ValueType::BOOL: return bool_val ? "true" : "false";
            case ValueType::BYTES: return "\"" + str_val + "\"";
            case ValueType::OBJECT: {
                std::string json = "{";
                for (auto it = obj_val.begin(); it != obj_val.end(); ++it) {
                    if (it != obj_val.begin()) json += ",";
                    json += "\"" + it->first + "\":" + it->second.to_json();
                }
                json += "}";
                return json;
            }
            case ValueType::ARRAY: {
                std::string json = "[";
                for (size_t i = 0; i < arr_val.size(); ++i) {
                    if (i > 0) json += ",";
                    json += arr_val[i].to_json();
                }
                json += "]";
                return json;
            }
            default: return "null";
        }
    }
};