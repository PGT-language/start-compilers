#pragma once

#include "Utils.h"
#include "Token.h"
#include <memory>
#include <vector>
#include <string>

struct AstNode { virtual ~AstNode() = default; };

struct FunctionDef : AstNode {
    std::string name;
    std::vector<std::string> param_names;
    std::vector<std::shared_ptr<AstNode>> body;
};

struct VarDecl : AstNode {
    std::string name;
    std::shared_ptr<AstNode> expr;
};

struct BinaryOp : AstNode {
    TokenType op;
    std::shared_ptr<AstNode> left, right;
};

struct Literal : AstNode { Value value; };
struct Identifier : AstNode { std::string name; };

struct PrintStmt : AstNode {
    std::vector<std::shared_ptr<AstNode>> args;
    std::vector<std::string> formats;
};

struct InputStmt : AstNode {
    std::string format;  // "{int}", "{float}", "{string}"
};

struct ConectCall : AstNode {
    std::string func_name;
    std::vector<std::shared_ptr<AstNode>> args;
};