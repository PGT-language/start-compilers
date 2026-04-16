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

    struct ParsedUrl {
        std::string scheme;
        std::string host;
        std::string port;
        std::string path;
    };

    bool is_truthy(const Value& value) const;
    Value coerce_value(const Value& value, const std::string& type_name, const SourceLocation& loc) const;
    void assign_value(const std::string& name, const Value& value, std::map<std::string, Value>& locals);
    ParsedUrl parse_url(const std::string& url, const SourceLocation& loc) const;
    std::string extract_http_body(const std::string& response) const;
    std::string perform_http_request(const std::string& transport, const std::string& method, const std::string& url,
                                     const std::string& body, const SourceLocation& loc) const;
    void run_http_server(const std::string& host, long long port, const std::string& body, const SourceLocation& loc) const;
    void execute_statement(const std::shared_ptr<AstNode>& stmt, std::map<std::string, Value>& locals);
    void execute_block(const std::vector<std::shared_ptr<AstNode>>& body, std::map<std::string, Value>& locals);
    void execute_function(const std::string& name, const std::vector<Value>& call_args);
    Value eval(const std::shared_ptr<AstNode>& node, const std::map<std::string, Value>& locals = {});

public:
    void run(const std::vector<std::shared_ptr<AstNode>>& program);
};