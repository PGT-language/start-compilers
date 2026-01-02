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
    size_t last_pos = pos;
    size_t iterations = 0;
    while (!is_eof()) {
        if (pos == last_pos) {
            iterations++;
            if (iterations > 1000) {
                if (DEBUG) std::cout << "[DEBUG] Parser stuck in parse_program at token: " << current().type << " (" << current().value << ")" << std::endl;
                break;
            }
        } else {
            iterations = 0;
        }
        last_pos = pos;
        
        if (current().type == T_PACKAGE) {
            advance(); advance();
        } else if (current().type == T_FROM) {
            auto import = parse_import();
            if (import) nodes.push_back(import);
        } else if (current().type == T_FUNCTION) {
            auto func = parse_function();
            if (func) nodes.push_back(func);
        } else if (current().type == T_IDENTIFIER && pos + 1 < tokens.size() && tokens[pos + 1].type == T_PLUS) {
            // Глобальная переменная
            auto var = parse_var_decl();
            if (var) nodes.push_back(var);
        } else if (current().type == T_RETURN) {
            advance(); // пропускаем return вне функций
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

    // Пропускаем запятую после имени функции, если она есть
    if (current().type == T_COMMA) advance();

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

    size_t iterations = 0;
    size_t last_pos = pos;
    while (!is_eof() && current().type != T_RBRACE) {
        // Защита от бесконечного цикла
        if (pos == last_pos) {
            iterations++;
            if (iterations > 100) {
                if (DEBUG) std::cout << "[DEBUG] Parser stuck in parse_function at token: " << current().type << " (" << current().value << "), skipping to }" << std::endl;
                // Пропускаем до закрывающей скобки функции
                while (!is_eof() && current().type != T_RBRACE) {
                    advance();
                }
                break;
            }
        } else {
            iterations = 0;
        }
        last_pos = pos;
        
        size_t start_pos = pos;
        auto stmt = parse_statement();
        if (stmt) {
            func->body.push_back(stmt);
        } else {
            // Если не удалось распарсить и позиция не изменилась, значит застряли
            if (pos == start_pos) {
                if (DEBUG) std::cout << "[DEBUG] Cannot parse statement at token: " << current().type << " (" << current().value << "), skipping" << std::endl;
                
                
                // Пропускаем токен
                advance();
            } else {
                // Позиция изменилась, просто продолжаем
            }
        }
    }

    advance(); // }

    if (DEBUG) std::cout << "[DEBUG] Defined function: " << func->name << " with " << func->param_names.size() << " params" << std::endl;

    return func;
}

std::shared_ptr<AstNode> Parser::parse_statement() {
    if (current().type == T_COUT) {
        return parse_input();  // cout(input, "{type}")
    }
    if (current().type == T_PRINT || current().type == T_PRINTG || current().type == T_PRINTLN) {
        return parse_print();
    }
    if (current().type == T_IDENTIFIER && current().value == "if") {
        return parse_if();
    }
    if (current().type == T_IDENTIFIER && pos + 1 < tokens.size() && tokens[pos + 1].type == T_PLUS) {
        return parse_var_decl();
    }
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

std::shared_ptr<AstNode> Parser::parse_expr() { return parse_comparison(); }

std::shared_ptr<AstNode> Parser::parse_comparison() {
    auto node = parse_add_sub();
    while (!is_eof() && (current().type == T_GREATER || current().type == T_LESS || 
                         current().type == T_GREATER_EQUAL || current().type == T_LESS_EQUAL ||
                         current().type == T_EQUAL_EQUAL || current().type == T_NOT_EQUAL)) {
        TokenType op = current().type; advance();
        auto right = parse_add_sub();
        auto bin = std::make_shared<BinaryOp>();
        bin->op = op; bin->left = node; bin->right = right;
        node = bin;
    }
    return node;
}

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
    if (current().type == T_IDENTIFIER || current().type == T_INPUT) {
        auto id = std::make_shared<Identifier>();
        id->name = current().value;
        advance();
        return id;
    }
    return nullptr;
}

std::shared_ptr<PrintStmt> Parser::parse_print() {
    TokenType print_type = current().type;
    bool is_printg = (print_type == T_PRINTG);
    if (DEBUG) std::cout << "[DEBUG] parse_print: starting at token: " << current().type << " (" << current().value << ")" << std::endl;
    advance(); // print, printg or println
    advance(); // (

    auto p = std::make_shared<PrintStmt>();
    p->is_printg = is_printg;

    while (!is_eof() && current().type != T_RPAREN) {
        // Парсим аргумент
        if (DEBUG) std::cout << "[DEBUG] parse_print: parsing arg at token: " << current().type << " (" << current().value << ")" << std::endl;
        auto arg = parse_expr();
        if (!arg) {
            if (DEBUG) std::cout << "[DEBUG] parse_print: failed to parse expr at token: " << current().type << " (" << current().value << ")" << std::endl;
            // Если не удалось распарсить выражение, но мы уже внутри print, попробуем пропустить до закрывающей скобки
            while (!is_eof() && current().type != T_RPAREN) {
                advance();
            }
            break;
        }
        p->args.push_back(arg);
        p->formats.emplace_back(); // Добавляем пустой формат по умолчанию
        if (DEBUG) std::cout << "[DEBUG] parse_print: parsed arg, current token: " << current().type << " (" << current().value << ")" << std::endl;
        
        // Проверяем, есть ли после аргумента запятая
        if (current().type == T_COMMA) {
            advance();
            
            // Проверяем, является ли следующий токен строкой формата (начинается с "{")
            // Это может быть формат для предыдущего аргумента
            if (current().type == T_STRING_LITERAL && 
                current().value.size() >= 2 && 
                current().value[0] == '{' && current().value.back() == '}') {
                // Это формат для предыдущего аргумента
                p->formats.back() = current().value;
                advance();
                
                // Если после формата есть еще запятая, значит есть еще аргументы
                if (current().type == T_COMMA) {
                    advance();
                    // Продолжаем цикл для следующего аргумента
                } else if (current().type == T_RPAREN) {
                    // Если после формата закрывающая скобка, выходим
                    break;
                }
                // Если после формата не запятая и не скобка, продолжаем парсить
            }
            // Если после запятой не формат, значит это следующий аргумент (продолжаем цикл)
            // Цикл продолжается автоматически
        } else {
            // Если нет запятой, значит это последний аргумент
            break;
        }
    }
    if (current().type != T_RPAREN) {
        if (DEBUG) std::cout << "[DEBUG] parse_print: expected ')' but got: " << current().type << " (" << current().value << "), skipping to )" << std::endl;
        // Пропускаем до закрывающей скобки
        while (!is_eof() && current().type != T_RPAREN) {
            advance();
        }
    }
    if (current().type == T_RPAREN) {
        advance(); // )
        if (DEBUG) std::cout << "[DEBUG] parse_print: successfully parsed " << p->args.size() << " args" << std::endl;
    } else {
        if (DEBUG) std::cout << "[DEBUG] parse_print: failed to find closing ')'" << std::endl;
        return nullptr;
    }
    return p;
}

std::shared_ptr<InputStmt> Parser::parse_input() {
    advance(); // cout
    advance(); // (
    
    auto input = std::make_shared<InputStmt>();
    
    // Проверяем, что следующий токен - идентификатор (имя переменной) или input
    if (current().type == T_INPUT) {
        input->var_name = "input";
        advance(); // input
    } else if (current().type == T_IDENTIFIER) {
        input->var_name = current().value;
        advance(); // имя переменной
    } else {
        return nullptr;
    }
    
    advance(); // ,
    
    input->format = current().value;
    advance(); // "{int}", "{float}" или "{string}"
    advance(); // )

    input->prompt = "";  // промпт пока не поддерживается в синтаксисе
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

std::shared_ptr<ImportStmt> Parser::parse_import() {
    advance(); // from
    
    std::string file_path;
    if (current().type == T_STRING_LITERAL) {
        file_path = current().value;
        advance();
    } else if (current().type == T_IDENTIFIER) {
        file_path = current().value;
        advance();
    } else {
        return nullptr;
    }
    
    if (current().type != T_IMPORT) {
        return nullptr;
    }
    advance(); // import
    
    std::string import_name;
    if (current().type == T_STRING_LITERAL) {
        import_name = current().value;
        advance();
    } else if (current().type == T_IDENTIFIER) {
        import_name = current().value;
        advance();
    } else {
        return nullptr;
    }
    
    auto import = std::make_shared<ImportStmt>();
    import->file_path = file_path;
    import->import_name = import_name;
    
    if (DEBUG) std::cout << "[DEBUG] Import: " << import_name << " from " << file_path << std::endl;
    
    return import;
}

std::shared_ptr<IfStmt> Parser::parse_if() {
    advance(); // if
    if (current().type != T_LPAREN) {
        if (DEBUG) std::cout << "[DEBUG] Expected '(' after 'if'" << std::endl;
        return nullptr;
    }
    advance(); // (
    
    auto condition = parse_expr();
    
    if (current().type != T_RPAREN) {
        if (DEBUG) std::cout << "[DEBUG] Expected ')' after condition" << std::endl;
        return nullptr;
    }
    advance(); // )
    
    if (current().type != T_LBRACE) {
        if (DEBUG) std::cout << "[DEBUG] Expected '{' after if condition" << std::endl;
        return nullptr;
    }
    advance(); // {
    
    auto if_stmt = std::make_shared<IfStmt>();
    if_stmt->condition = condition;
    
    // Парсим тело if
    while (!is_eof() && current().type != T_RBRACE) {
        auto stmt = parse_statement();
        if (stmt) {
            if_stmt->then_body.push_back(stmt);
        } else {
            advance(); // пропускаем неизвестные токены
        }
    }
    
    if (current().type == T_RBRACE) {
        advance(); // }
    }
    
    // Проверяем наличие else
    if (current().type == T_IDENTIFIER && current().value == "else") {
        advance(); // else
        if (current().type == T_LBRACE) {
            advance(); // {
            while (!is_eof() && current().type != T_RBRACE) {
                auto stmt = parse_statement();
                if (stmt) {
                    if_stmt->else_body.push_back(stmt);
                } else {
                    advance();
                }
            }
            if (current().type == T_RBRACE) {
                advance(); // }
            }
        }
    }
    
    return if_stmt;
}