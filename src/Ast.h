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
    std::vector<std::string> param_types;
    std::vector<std::shared_ptr<AstNode>> body;
    bool has_return_one = false;  // Есть ли return 1 в функции
};

struct VarDecl : AstNode {
    std::string name;
    std::string type_name;
    std::shared_ptr<AstNode> expr;
};

struct BinaryOp : AstNode {
    TokenType op;
    std::shared_ptr<AstNode> left, right;
};

struct Literal : AstNode { Value value; };
struct Identifier : AstNode { std::string name; };

struct BuiltinCallExpr : AstNode {
    std::string name;
    std::vector<std::shared_ptr<AstNode>> args;
};

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

struct CallStmt : AstNode {
    std::string func_name;
    std::vector<std::shared_ptr<AstNode>> args;
};

struct ReturnStmt : AstNode {
    std::shared_ptr<AstNode> expr;
};

struct ImportStmt : AstNode {
    std::string file_path;  // путь к файлу после "from"
    std::vector<std::string> import_names; // список функций для импорта
};

struct IfStmt : AstNode {
    std::shared_ptr<AstNode> condition;  // условие
    std::vector<std::shared_ptr<AstNode>> then_body;  // тело if
    std::vector<std::shared_ptr<AstNode>> else_body;  // тело else (может быть пустым)
};

struct WhileStmt : AstNode {
    std::shared_ptr<AstNode> condition;  // условие цикла
    std::vector<std::shared_ptr<AstNode>> body;  // тело цикла
};

struct NetOp : AstNode {
    std::string transport;  // http или https
    std::string method;  // get, post, serve, run или route
    std::shared_ptr<AstNode> url;
    std::shared_ptr<AstNode> path;  // только для route
    std::shared_ptr<AstNode> port;  // только для serve
    std::shared_ptr<AstNode> data;  // для post, serve fallback и route handler
};

struct FileOp : AstNode {
    TokenType operation;  // T_CREATE, T_WRITE, T_READ, T_CLOSE, T_DELETE
    std::shared_ptr<AstNode> file_path;  // путь к файлу (выражение)
    std::string mode;  // режим работы: "c", "w", "r", "h", "d"
    std::shared_ptr<AstNode> data;  // данные для записи (только для write), может быть nullptr
};