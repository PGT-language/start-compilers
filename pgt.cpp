#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <memory>  // <<<--- ЭТО БЫЛО ГЛАВНОЙ ПРОБЛЕМОЙ!

// Типы значений
enum class ValueType { INT, FLOAT, STRING, NONE };

struct Value {
    ValueType type = ValueType::NONE;
    int int_val = 0;
    double float_val = 0.0;
    std::string str_val;

    Value() = default;
    Value(int v) : type(ValueType::INT), int_val(v) {}
    Value(double v) : type(ValueType::FLOAT), float_val(v) {}
    Value(std::string v) : type(ValueType::STRING), str_val(std::move(v)) {}

    std::string to_string(const std::string& format = "") const {
        if (!format.empty()) {
            if (format == "{int}" && type == ValueType::INT) return std::to_string(int_val);
            if (format == "{float}" && type == ValueType::FLOAT) return std::to_string(float_val);
            if (format == "{string}" && type == ValueType::STRING) return str_val;
        }
        switch (type) {
            case ValueType::INT: return std::to_string(int_val);
            case ValueType::FLOAT: return std::to_string(float_val);
            case ValueType::STRING: return str_val;
            default: return "[none]";
        }
    }
};

// Токены
enum TokenType {
    T_PACKAGE, T_FUNCTION, T_IF, T_ELSE, T_INT, T_FLOAT, T_STRING,
    T_PRINT, T_PRINTLN, T_RETURN, T_CONECT,
    T_IDENTIFIER, T_STRING_LITERAL, T_NUMBER,
    T_LBRACE, T_RBRACE, T_LPAREN, T_RPAREN,
    T_COMMA, T_PLUS, T_EQUAL, T_COLON, T_EOF
};

struct Token {
    TokenType type;
    std::string value;
    int line = 0;
};

// Лексер
class Lexer {
    std::string source;
    size_t pos = 0;
    int line = 1;

    char peek() const { return pos < source.size() ? source[pos] : 0; }
    char get() { return pos < source.size() ? source[pos++] : 0; }

public:
    explicit Lexer(std::string src) : source(std::move(src)) {}

    Token next_token() {
        while (true) {
            char c = peek();
            if (c == 0) return {T_EOF, "", line};
            if (std::isspace(c)) { get(); if (c == '\n') line++; continue; }
            if (c == '{') { get(); return {T_LBRACE, "{", line}; }
            if (c == '}') { get(); return {T_RBRACE, "}", line}; }
            if (c == '(') { get(); return {T_LPAREN, "(", line}; }
            if (c == ')') { get(); return {T_RPAREN, ")", line}; }
            if (c == ',') { get(); return {T_COMMA, ",", line}; }
            if (c == '+') { get(); return {T_PLUS, "+", line}; }
            if (c == '=') { get(); return {T_EQUAL, "=", line}; }
            if (c == ':') { get(); return {T_COLON, ":", line}; }

            if (c == '"') {
                get();
                std::string str;
                while (peek() != '"' && peek() != 0) str += get();
                if (peek() == '"') get();
                return {T_STRING_LITERAL, str, line};
            }

            if (std::isdigit(c) || (c == '-' && std::isdigit(peek() + 1)) || (c == '.' && std::isdigit(peek() + 1))) {
                std::string num;
                if (c == '-' || c == '.') num += get();
                while (std::isdigit(peek()) || peek() == '.') num += get();
                return {T_NUMBER, num, line};
            }

            if (std::isalpha(c) || c == '_') {
                std::string id;
                while (std::isalnum(peek()) || peek() == '_' || peek() == ':') id += get();

                if (id == "package") return {T_PACKAGE, id, line};
                if (id == "function") return {T_FUNCTION, id, line};
                if (id == "if") return {T_IF, id, line};
                if (id == "else") return {T_ELSE, id, line};
                if (id == "int") return {T_INT, id, line};
                if (id == "float") return {T_FLOAT, id, line};
                if (id == "string") return {T_STRING, id, line};
                if (id == "print") return {T_PRINT, id, line};
                if (id == "println") return {T_PRINTLN, id, line};
                if (id == "return") return {T_RETURN, id, line};
                if (id == "conect") return {T_CONECT, id, line};

                return {T_IDENTIFIER, id, line};
            }

            std::cerr << "Unknown character at line " << line << ": " << c << std::endl;
            get();
        }
    }
};

// Узлы AST
struct AstNode {
    virtual ~AstNode() = default;
};

struct FunctionDef : AstNode {
    std::string name;
    std::vector<std::shared_ptr<AstNode>> body;
};

struct VarDecl : AstNode {
    std::string name;
    TokenType type_token;
    std::shared_ptr<AstNode> value;
};

struct Literal : AstNode {
    Value value;
};

struct Identifier : AstNode {
    std::string name;
};

struct PrintStmt : AstNode {
    std::vector<std::shared_ptr<AstNode>> args;
    std::vector<std::string> formats;
};

struct ConectCall : AstNode {
    std::string func_name;
};

struct ReturnStmt : AstNode {
    std::shared_ptr<AstNode> value;
};

// Парсер
class Parser {
    std::vector<Token> tokens;
    size_t pos = 0;

    const Token& current() const { return tokens[pos]; }
    void eat(TokenType type) {
        if (pos < tokens.size() && current().type == type) pos++;
        else std::cerr << "Expected token " << type << " at line " << (pos < tokens.size() ? current().line : 0) << std::endl;
    }

public:
    void load_tokens(std::vector<Token> t) { tokens = std::move(t); }

    std::vector<std::shared_ptr<AstNode>> parse_program() {
        std::vector<std::shared_ptr<AstNode>> nodes;

        while (pos < tokens.size() && current().type != T_EOF) {
            if (current().type == T_PACKAGE) {
                eat(T_PACKAGE);
                eat(T_IDENTIFIER);
            } else if (current().type == T_FUNCTION) {
                nodes.push_back(parse_function());
            } else if (current().type == T_RETURN) {
                nodes.push_back(parse_return());
            } else {
                pos++;
            }
        }
        return nodes;
    }

private:
    std::shared_ptr<FunctionDef> parse_function() {
        eat(T_FUNCTION);
        eat(T_LPAREN);
        std::string name = current().value;
        eat(T_IDENTIFIER);
        eat(T_RPAREN);
        eat(T_LBRACE);

        auto func = std::make_shared<FunctionDef>();
        func->name = name;

        while (pos < tokens.size() && current().type != T_RBRACE) {
            auto stmt = parse_statement();
            if (stmt) func->body.push_back(stmt);
        }
        eat(T_RBRACE);
        return func;
    }

    std::shared_ptr<AstNode> parse_statement() {
        if (current().type == T_IDENTIFIER && lookahead_is_var_decl()) {
            return parse_var_decl();
        } else if (current().type == T_PRINT || current().type == T_PRINTLN) {
            return parse_print();
        } else if (current().type == T_CONECT) {
            return parse_conect();
        } else if (current().type == T_RETURN) {
            return parse_return();
        }
        return nullptr;
    }

    bool lookahead_is_var_decl() {
        size_t temp = pos + 1;
        if (temp >= tokens.size()) return false;
        if (tokens[temp].type == T_PLUS) {
            temp++;
            if (temp >= tokens.size()) return false;
            return tokens[temp].type == T_INT || tokens[temp].type == T_FLOAT || tokens[temp].type == T_STRING;
        }
        return false;
    }

    std::shared_ptr<VarDecl> parse_var_decl() {
        std::string name = current().value;
        eat(T_IDENTIFIER);
        eat(T_PLUS);
        TokenType type_tok = current().type;
        eat(type_tok); // int/float/string
        eat(T_EQUAL);

        auto value = parse_expr();

        auto decl = std::make_shared<VarDecl>();
        decl->name = name;
        decl->type_token = type_tok;
        decl->value = value;
        return decl;
    }

    std::shared_ptr<AstNode> parse_expr() {
        if (current().type == T_NUMBER) {
            std::string num = current().value;
            eat(T_NUMBER);
            auto lit = std::make_shared<Literal>();
            if (num.find('.') != std::string::npos || num.find('e') != std::string::npos || num.find('E') != std::string::npos) {
                lit->value = Value(std::stod(num));
            } else {
                lit->value = Value(std::stoi(num));
            }
            return lit;
        } else if (current().type == T_STRING_LITERAL) {
            auto lit = std::make_shared<Literal>();
            lit->value = Value(current().value);
            eat(T_STRING_LITERAL);
            return lit;
        } else if (current().type == T_IDENTIFIER) {
            auto id = std::make_shared<Identifier>();
            id->name = current().value;
            eat(T_IDENTIFIER);
            return id;
        }
        return nullptr;
    }

    std::shared_ptr<PrintStmt> parse_print() {
        bool is_println = current().type == T_PRINTLN;
        eat(current().type);
        eat(T_LPAREN);

        auto print = std::make_shared<PrintStmt>();

        while (pos < tokens.size() && current().type != T_RPAREN) {
            if (current().type == T_STRING_LITERAL || current().type == T_IDENTIFIER) {
                print->args.push_back(parse_expr());
            }

            if (current().type == T_COMMA) {
                eat(T_COMMA);
                if (current().type == T_STRING_LITERAL) {
                    print->formats.push_back(current().value);
                    eat(T_STRING_LITERAL);
                } else {
                    print->formats.emplace_back(); // пустой формат
                }
            }
        }
        eat(T_RPAREN);

        if (is_println) std::cout << std::endl; // println добавляет \n в конце
        return print;
    }

    std::shared_ptr<ConectCall> parse_conect() {
        eat(T_CONECT);
        eat(T_LPAREN);
        std::string name = current().type == T_STRING_LITERAL ? current().value : current().value;
        if (current().type == T_STRING_LITERAL) eat(T_STRING_LITERAL);
        else eat(T_IDENTIFIER);
        eat(T_RPAREN);

        auto call = std::make_shared<ConectCall>();
        call->func_name = name;
        return call;
    }

    std::shared_ptr<ReturnStmt> parse_return() {
        eat(T_RETURN);
        auto ret = std::make_shared<ReturnStmt>();
        if (current().type != T_RBRACE && current().type != T_EOF) {
            ret->value = parse_expr();
        }
        return ret;
    }
};

// Интерпретатор
class Interpreter {
    std::map<std::string, std::shared_ptr<FunctionDef>> functions;
    std::map<std::string, Value> variables; // пока глобальные + локальные через стек потом

public:
    void add_function(std::shared_ptr<FunctionDef> func) {
        functions[func->name] = func;
    }

    void run(const std::vector<std::shared_ptr<AstNode>>& program) {
        // Сначала собираем все функции
        for (const auto& node : program) {
            if (auto func = std::dynamic_pointer_cast<FunctionDef>(node)) {
                functions[func->name] = func;
            }
        }

        // Запускаем main
        if (functions.find("main") != functions.end()) {
            execute_function("main");
        }
    }

private:
    void execute_function(const std::string& name) {
        auto func = functions[name];
        if (!func) {
            std::cerr << "Function not found: " << name << std::endl;
            return;
        }

        std::map<std::string, Value> saved_vars = variables;
        variables.clear();

        for (const auto& stmt : func->body) {
            execute(stmt);
        }

        variables = saved_vars;
    }

    void execute(const std::shared_ptr<AstNode>& node) {
        if (!node) return;

        if (auto decl = std::dynamic_pointer_cast<VarDecl>(node)) {
            Value val = eval(decl->value);
            variables[decl->name] = val;
        } else if (auto print = std::dynamic_pointer_cast<PrintStmt>(node)) {
            size_t fmt_idx = 0;
            for (const auto& arg : print->args) {
                Value val = eval(arg);
                std::string fmt = fmt_idx < print->formats.size() ? print->formats[fmt_idx++] : "";
                std::cout << val.to_string(fmt);
            }
            std::cout << std::endl;
        } else if (auto call = std::dynamic_pointer_cast<ConectCall>(node)) {
            execute_function(call->func_name);
        }
        // return пока игнорируем значение
    }

    Value eval(const std::shared_ptr<AstNode>& node) {
        if (auto lit = std::dynamic_pointer_cast<Literal>(node)) {
            return lit->value;
        } else if (auto id = std::dynamic_pointer_cast<Identifier>(node)) {
            if (variables.find(id->name) != variables.end()) {
                return variables[id->name];
            }
            std::cerr << "Undefined variable: " << id->name << std::endl;
            return Value();
        }
        return Value();
    }
};

// CLI функции
void show_help() {
    std::cout << "Program Generate Time (PGT) Zero Compiler v0.1\n";
    std::cout << "Usage:\n";
    std::cout << "  pgt --help              Show this help\n";
    std::cout << "  pgt --version           Show version\n";
    std::cout << "  pgt run <file.pgt>      Run PGT program\n";
    std::cout << "  pgt build <file.pgt>    (future: compile to executable)\n";
}

void show_version() {
    std::cout << "PGT Zero Compiler v0.1 (December 2025)\n";
}

std::string read_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filename << std::endl;
        return "";
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        show_help();
        return 1;
    }

    std::string command = argv[1];

    if (command == "--help") {
        show_help();
    } else if (command == "--version") {
        show_version();
    } else if (command == "run" && argc == 3) {
        std::string filename = argv[2];
        if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".pgt") {
            std::cerr << "Error: File must have .pgt extension\n";
            return 1;
        }

        std::string source = read_file(filename);
        if (source.empty()) return 1;

        // Лексический анализ
        Lexer lexer(source);
        std::vector<Token> tokens;
        Token token;
        do {
            token = lexer.next_token();
            tokens.push_back(token);
        } while (token.type != T_EOF);

        // Парсинг
        Parser parser;
        parser.load_tokens(tokens);
        auto program = parser.parse_program();

        // Интерпретация
        Interpreter interp;
        interp.run(program);

    } else if (command == "build" && argc == 3) {
        std::cout << "Build mode not implemented yet. Use 'run' for now.\n";
    } else {
        show_help();
        return 1;
    }

    return 0;
}