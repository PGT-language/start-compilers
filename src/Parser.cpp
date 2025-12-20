#include "Parser.h"
#include "Utils.h"
#include <iostream>
#include <limits>

void Parser::load_tokens(std::vector<Token> t) {
    tokens = std::move(t);
    if (tokens.empty() || tokens.back().type != T_EOF) tokens.push_back({T_EOF});
}

bool Parser::is_eof() const { return pos >= tokens.size(); }
const Token& Parser::current() const { return tokens[pos]; }
void Parser::advance() { if (!is_eof()) ++pos; }

std::vector<std::shared_ptr<AstNode>> Parser::parse_program() {
    std::vector<std::shared_ptr<AstNode>> nodes;
    while (!is_eof()) {
        if (current().type == T_PACKAGE) {
            advance(); advance();
        } else if (current().type == T_FUNCTION) {
            auto func = parse_function();
            if (func) nodes.push_back(func);
        } else {
            advance();
        }
    }
    return nodes;
}

std::shared_ptr<FunctionDef> Parser::parse_function() {
    advance(); // function
    advance(); // (

    std::string name = current().value;
    advance();

    auto func = std::make_shared<FunctionDef>();
    func->name = name;

    while (!is_eof() && current().type == T_IDENTIFIER) {
        if (pos + 1 >= tokens.size() || tokens[pos + 1].type != T_PLUS) break;

        std::string param_name = current().value;
        advance();
        advance(); // +
        advance(); // type

        func->param_names.push_back(param_name);

        if (current().type == T_COMMA) advance();
    }

    advance(); // )

    advance(); // {

    while (!is_eof() && current().type != T_RBRACE) {
        auto stmt = parse_statement();
        if (stmt) func->body.push_back(stmt);
        else advance();
    }

    advance(); // }

    if (DEBUG) std::cout << "[DEBUG] Defined function: " << func->name << " with " << func->param_names.size() << " params" << std::endl;

    return func;
}

std::shared_ptr<AstNode> Parser::parse_statement() {
    if (current().type == T_PRINT || current().type == T_PRINTLN) {
        if (pos + 2 < tokens.size() && tokens[pos + 2].type == T_INPUT) {
            return parse_input();  // cout(input, "{type}")
        }
        return parse_print();
    }
    if (current().type == T_IDENTIFIER && pos + 1 < tokens.size() && tokens[pos + 1].type == T_PLUS) {
        return parse_var_decl();
    }
    if (current().type == T_PRINT || current().type == T_PRINTLN) return parse_print();
    if (current().type == T_CONECT) return parse_conect();
    if (current().type == T_RETURN) { advance(); return nullptr; }
    return nullptr;
}

std::shared_ptr<VarDecl> Parser::parse_var_decl() {
    std::string name = current().value; advance();
    advance(); // +
    advance(); // type
    advance(); // =
    auto expr = parse_expr();
    auto decl = std::make_shared<VarDecl>();
    decl->name = name;
    decl->expr = expr;
    return decl;
}

std::shared_ptr<AstNode> Parser::parse_expr() { return parse_add_sub(); }

std::shared_ptr<AstNode> Parser::parse_add_sub() {
    auto node = parse_mul_div();
    while (!is_eof() && (current().type == T_PLUS || current().type == T_MINUS)) {
        TokenType op = current().type; advance();
        auto right = parse_mul_div();
        auto bin = std::make_shared<BinaryOp>();
        bin->op = op; bin->left = node; bin->right = right;
        node = bin;
    }
    return node;
}

std::shared_ptr<AstNode> Parser::parse_mul_div() {
    auto node = parse_primary();
    while (!is_eof() && (current().type == T_STAR || current().type == T_SLASH)) {
        TokenType op = current().type; advance();
        auto right = parse_primary();
        auto bin = std::make_shared<BinaryOp>();
        bin->op = op; bin->left = node; bin->right = right;
        node = bin;
    }
    return node;
}

std::shared_ptr<AstNode> Parser::parse_primary() {
    if (is_eof()) return nullptr;
    if (current().type == T_NUMBER) {
        std::string num_str = current().value;
        advance();
        auto lit = std::make_shared<Literal>();
        if (num_str.find('.') != std::string::npos) {
            lit->value = Value(std::stod(num_str));
        } else {
            lit->value = Value(std::stoll(num_str));
        }
        return lit;
    }
    if (current().type == T_STRING_LITERAL) {
        auto lit = std::make_shared<Literal>();
        lit->value = Value(current().value);
        advance();
        return lit;
    }
    if (current().type == T_IDENTIFIER) {
        auto id = std::make_shared<Identifier>();
        id->name = current().value;
        advance();
        return id;
    }
    return nullptr;
}

std::shared_ptr<PrintStmt> Parser::parse_print() {
    advance(); // print or println
    advance(); // (

    auto p = std::make_shared<PrintStmt>();

    while (!is_eof() && current().type != T_RPAREN) {
        p->args.push_back(parse_expr());
        if (current().type == T_COMMA) {
            advance();
            if (current().type == T_STRING_LITERAL) {
                p->formats.push_back(current().value);
                advance();
            } else {
                p->formats.emplace_back();
            }
        }
    }
    advance(); // )
    return p;
}

std::shared_ptr<InputStmt> Parser::parse_input() {
    advance(); // print или println (уже сделано в parse_statement)
    advance(); // (
    advance(); // input
    advance(); // ,
    std::string format = current().value;
    advance(); // "{int}" или "{string}"
    advance(); // )

    auto input = std::make_shared<InputStmt>();
    input->format = format;
    return input;
}

std::shared_ptr<ConectCall> Parser::parse_conect() {
    advance(); // conect
    advance(); // (

    std::string name;
    if (current().type == T_STRING_LITERAL) {
        name = current().value;
        advance();
    } else {
        name = current().value;
        advance();
    }

    auto call = std::make_shared<ConectCall>();
    call->func_name = name;

    if (current().type == T_COMMA) {
        advance();
        while (!is_eof() && current().type != T_RPAREN) {
            call->args.push_back(parse_expr());
            if (current().type == T_COMMA) advance();
        }
    }
    advance(); // )
    return call;
}