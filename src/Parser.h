#pragma once

#include "Ast.h"
#include "Token.h"
#include <vector>
#include <memory>

class Parser {
    std::vector<Token> tokens;
    size_t pos = 0;
    bool has_package_decl = false;
    std::string package_name;
    bool has_return_zero = false;
    std::vector<RouteDef> pending_routes;

    bool is_eof() const;
    const Token& current() const;
    void advance();

    std::shared_ptr<FunctionDef> parse_function();
    std::shared_ptr<ClassDef> parse_class();
    OrmField parse_orm_field();
    std::shared_ptr<AstNode> parse_statement();
    std::shared_ptr<VarDecl> parse_var_decl();
    std::shared_ptr<AstNode> parse_expr();
    std::shared_ptr<AstNode> parse_comparison();
    std::shared_ptr<AstNode> parse_add_sub();
    std::shared_ptr<AstNode> parse_mul_div();
    std::shared_ptr<AstNode> parse_primary();
    std::shared_ptr<BuiltinCallExpr> parse_builtin_call_expr();
    std::shared_ptr<BuiltinCallExpr> parse_namespaced_builtin_call_expr();
    std::shared_ptr<IfStmt> parse_if();
    std::shared_ptr<WhileStmt> parse_while();
    std::shared_ptr<PrintStmt> parse_print();
    std::shared_ptr<InputStmt> parse_input();
    std::shared_ptr<CallStmt> parse_call();
    std::shared_ptr<CallStmt> parse_function_call();
    std::shared_ptr<FileOp> parse_file_op();
    std::shared_ptr<NetOp> parse_net_op();
    std::shared_ptr<ImportStmt> parse_import();
    std::string parse_type_name();

public:
    void load_tokens(std::vector<Token> t);
    std::vector<std::shared_ptr<AstNode>> parse_program();
    bool found_package_main() const { return has_package_decl && package_name == "main"; }
    bool found_package_decl() const { return has_package_decl; }
    const std::string& parsed_package_name() const { return package_name; }
    bool found_return_zero() const { return has_return_zero; }
};
