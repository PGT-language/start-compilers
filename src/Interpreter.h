#pragma once

#include "Ast.h"
#include "Utils.h"
#include <map>
#include <memory>
#include <vector>

class Interpreter {
    std::map<std::string, std::shared_ptr<FunctionDef>> functions;
    std::map<std::string, Value> globals;

    void execute_function(const std::string& name, const std::vector<Value>& call_args);
    Value eval(const std::shared_ptr<AstNode>& node);

public:
    void run(const std::vector<std::shared_ptr<AstNode>>& program);
};