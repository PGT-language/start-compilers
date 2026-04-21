#pragma once

#include "Ast.h"
#include "Utils.h"
#include <map>
#include <memory>
#include <vector>
#include <fstream>
#include <string>

class Interpreter {
    std::map<std::string, std::shared_ptr<FunctionDef>> functions;
    std::map<std::string, Value> globals;  // Глобальные переменные
    std::vector<SourceLocation> call_stack;  // Стек вызовов для traceback
    std::map<std::string, std::unique_ptr<std::fstream>> open_files;  // Открытые файлы
    std::ofstream log_file;  // Файл для логов
    std::string log_output = "console";  // console или file

    struct HttpRoute {
        std::string handler;
        SourceLocation location;
    };

    struct HttpRequest {
        std::string method;
        std::string path;
        std::string body;
    };

    struct ParsedUrl {
        std::string scheme;
        std::string host;
        std::string port;
        std::string path;
    };

    std::map<std::string, HttpRoute> http_routes;
    HttpRequest current_request;

    bool is_truthy(const Value& value) const;
    Value coerce_value(const Value& value, const std::string& type_name, const SourceLocation& loc) const;
    void assign_value(const std::string& name, const Value& value, std::map<std::string, Value>& locals);
    ParsedUrl parse_url(const std::string& url, const SourceLocation& loc) const;
    std::string extract_http_body(const std::string& response) const;
    std::string perform_http_request(const std::string& transport, const std::string& method, const std::string& url,
                                     const std::string& body, const SourceLocation& loc) const;
    void run_http_server(const std::string& host, long long port, const std::string& body, const SourceLocation& loc);
    void register_http_route(const std::string& method, const std::string& path,
                             const std::string& handler, const SourceLocation& loc);
    std::string make_route_key(const std::string& method, const std::string& path) const;
    std::string normalize_http_method(const std::string& method) const;
    std::string read_response_body(const std::string& body, const SourceLocation& loc) const;
    Value call_http_handler(const HttpRoute& route, const std::string& method,
                            const std::string& path, const std::string& body);
    std::string response_content_type(const Value& value) const;
    std::string response_body_from_value(const Value& value) const;
    Value parse_json(const std::string& json_str, const SourceLocation& loc) const;
    std::string stringify_json(const Value& value) const;
    std::string normalize_log_level(const std::string& level) const;
    bool is_known_log_level(const std::string& level) const;
    std::string log_level_from_builtin(const std::string& name) const;
    bool is_log_builtin_name(const std::string& name) const;
    Value set_log_output(const Value& arg, const SourceLocation& loc);
    Value execute_log_builtin(const std::string& name, const std::vector<Value>& args, const SourceLocation& loc);
    Value open_log_path(const Value& arg, const SourceLocation& loc);
    void log_message(const std::string& message, const std::string& level = "INFO");
    void execute_statement(const std::shared_ptr<AstNode>& stmt, std::map<std::string, Value>& locals);
    void execute_block(const std::vector<std::shared_ptr<AstNode>>& body, std::map<std::string, Value>& locals);
    Value execute_function(const std::string& name, const std::vector<Value>& call_args);
    Value eval(const std::shared_ptr<AstNode>& node, const std::map<std::string, Value>& locals = {});

private:
    Value parse_json_value(const std::string& json_str, size_t& pos, const SourceLocation& loc) const;
    Value parse_json_object(const std::string& json_str, size_t& pos, const SourceLocation& loc) const;
    Value parse_json_array(const std::string& json_str, size_t& pos, const SourceLocation& loc) const;
    Value parse_json_string(const std::string& json_str, size_t& pos, const SourceLocation& loc) const;
    std::string parse_json_string_value(const std::string& json_str, size_t& pos, const SourceLocation& loc) const;
    Value parse_json_bool(const std::string& json_str, size_t& pos, const SourceLocation& loc) const;
    Value parse_json_null(const std::string& json_str, size_t& pos, const SourceLocation& loc) const;
    Value parse_json_number(const std::string& json_str, size_t& pos, const SourceLocation& loc) const;
    void skip_whitespace(const std::string& json_str, size_t& pos) const;

public:
    void run(const std::vector<std::shared_ptr<AstNode>>& program);
};