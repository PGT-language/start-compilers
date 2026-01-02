#pragma once

#include "Utils.h"
#include "Token.h"
#include "Error.h"
#include <memory>
#include <vector>
#include <string>

struct AstNode { 
    SourceLocation location;  // Позиция в исходном коде
    virtual ~AstNode() = default; 
};

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
    bool is_printg = false;  // true для printg, false для print/println
};

struct InputStmt : AstNode {
    std::string format;  // "{int}", "{float}", "{string}"
    std::string prompt;  // опциональный промпт для ввода
    std::string var_name;  // имя переменной, в которую сохраняется значение (по умолчанию "input")
};

struct ConectCall : AstNode {
    std::string func_name;
    std::vector<std::shared_ptr<AstNode>> args;
};

struct ImportStmt : AstNode {
    std::string file_path;  // путь к файлу после "from"
    std::string import_name; // что импортируем после "import"
};

struct IfStmt : AstNode {
    std::shared_ptr<AstNode> condition;  // условие
    std::vector<std::shared_ptr<AstNode>> then_body;  // тело if
    std::vector<std::shared_ptr<AstNode>> else_body;  // тело else (может быть пустым)
};