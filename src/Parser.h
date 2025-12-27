#pragma once

#include "Ast.h"
#include "Token.h"
#include <vector>
#include <memory>

class Parser {
    std::vector<Token> tokens;
    size_t pos = 0;

    bool is_eof() const;
    const Token& current() const;
    void advance();

    std::shared_ptr<FunctionDef> parse_function();
    std::shared_ptr<AstNode> parse_statement();
    std::shared_ptr<VarDecl> parse_var_decl();
    std::shared_ptr<AstNode> parse_expr();
    std::shared_ptr<AstNode> parse_add_sub();
    std::shared_ptr<AstNode> parse_mul_div();
    std::shared_ptr<AstNode> parse_primary();
    std::shared_ptr<PrintStmt> parse_print();
    std::shared_ptr<InputStmt> parse_input();
    std::shared_ptr<ConectCall> parse_conect();
    std::shared_ptr<ImportStmt> parse_import();

public:
    void load_tokens(std::vector<Token> t);
    std::vector<std::shared_ptr<AstNode>> parse_program();
};