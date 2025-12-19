#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <cctype>
#include <memory>

bool DEBUG = false;

enum class ValueType { INT, FLOAT, STRING, NONE };

struct Value {
    ValueType type = ValueType::NONE;
    long long int_val = 0;
    double float_val = 0.0;
    std::string str_val;

    Value() = default;
    Value(long long v) : type(ValueType::INT), int_val(v) {}
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
            default: return "";
        }
    }
};

enum TokenType {
    T_PACKAGE, T_FUNCTION, T_PRINT, T_PRINTLN, T_RETURN, T_CONECT,
    T_INT, T_FLOAT, T_STRING,
    T_IDENTIFIER, T_STRING_LITERAL, T_NUMBER,
    T_LBRACE, T_RBRACE, T_LPAREN, T_RPAREN,
    T_COMMA, T_PLUS, T_MINUS, T_STAR, T_SLASH, T_EQUAL,
    T_EOF
};

struct Token {
    TokenType type = T_EOF;
    std::string value;
    int line = 0;
};

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
            if (c == '-') { get(); return {T_MINUS, "-", line}; }
            if (c == '*') { get(); return {T_STAR, "*", line}; }
            if (c == '/') { get(); return {T_SLASH, "/", line}; }
            if (c == '=') { get(); return {T_EQUAL, "=", line}; }

            if (c == '"') {
                get();
                std::string str;
                while (peek() != '"' && peek() != 0) str += get();
                if (peek() == '"') get();
                return {T_STRING_LITERAL, str, line};
            }

            if (std::isdigit(c) || (c == '-' && std::isdigit(peek() + 1)) || c == '.') {
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
                if (id == "print") return {T_PRINT, id, line};
                if (id == "println") return {T_PRINTLN, id, line};
                if (id == "return") return {T_RETURN, id, line};
                if (id == "conect") return {T_CONECT, id, line};
                if (id == "int") return {T_INT, id, line};
                if (id == "float") return {T_FLOAT, id, line};
                if (id == "string") return {T_STRING, id, line};

                return {T_IDENTIFIER, id, line};
            }

            get();
        }
    }
};

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

struct ConectCall : AstNode {
    std::string func_name;
    std::vector<std::shared_ptr<AstNode>> args;
};

class Parser {
    std::vector<Token> tokens;
    size_t pos = 0;

    bool is_eof() const { return pos >= tokens.size(); }
    const Token& current() const { return tokens[pos]; }
    void advance() { if (!is_eof()) ++pos; }

public:
    void load_tokens(std::vector<Token> t) {
        tokens = std::move(t);
        if (tokens.empty() || tokens.back().type != T_EOF) tokens.push_back({T_EOF});
    }

    std::vector<std::shared_ptr<AstNode>> parse_program() {
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

private:
    std::shared_ptr<FunctionDef> parse_function() {
        advance(); // function
        advance(); // (

        std::string name = current().value;
        advance(); // имя функции

        auto func = std::make_shared<FunctionDef>();
        func->name = name;

        // Параметры (если есть)
        while (!is_eof() && current().type == T_IDENTIFIER) {
            if (pos + 1 >= tokens.size() || tokens[pos + 1].type != T_PLUS) break;

            std::string param_name = current().value;
            advance(); // имя параметра
            advance(); // +
            advance(); // тип

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

        if (DEBUG) {
            std::cout << "[DEBUG] Defined function: " << func->name << " with " << func->param_names.size() << " params" << std::endl;
        }

        return func;
    }

    std::shared_ptr<AstNode> parse_statement() {
        if (current().type == T_IDENTIFIER && pos + 1 < tokens.size() && tokens[pos + 1].type == T_PLUS) {
            return parse_var_decl();
        }
        if (current().type == T_PRINT || current().type == T_PRINTLN) return parse_print();
        if (current().type == T_CONECT) return parse_conect();
        if (current().type == T_RETURN) { advance(); return nullptr; }
        return nullptr;
    }

    std::shared_ptr<VarDecl> parse_var_decl() {
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

    std::shared_ptr<AstNode> parse_expr() { return parse_add_sub(); }

    std::shared_ptr<AstNode> parse_add_sub() {
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

    std::shared_ptr<AstNode> parse_mul_div() {
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

    std::shared_ptr<AstNode> parse_primary() {
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

    std::shared_ptr<PrintStmt> parse_print() {
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

    std::shared_ptr<ConectCall> parse_conect() {
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
};

class Interpreter {
    std::map<std::string, std::shared_ptr<FunctionDef>> functions;
    std::map<std::string, Value> globals;

public:
    void run(const std::vector<std::shared_ptr<AstNode>>& program) {
        for (const auto& node : program) {
            if (auto f = std::dynamic_pointer_cast<FunctionDef>(node)) {
                functions[f->name] = f;
            }
        }

        if (functions.count("main")) {
            execute_function("main", {});
        }
    }

private:
    void execute_function(const std::string& name, const std::vector<Value>& call_args) {
        auto func = functions[name];
        if (!func) return;

        auto saved = globals;

        for (size_t i = 0; i < func->param_names.size() && i < call_args.size(); ++i) {
            globals[func->param_names[i]] = call_args[i];
            if (DEBUG) std::cout << "[DEBUG] Param " << func->param_names[i] << " = " << call_args[i].to_string() << std::endl;
        }

        for (const auto& stmt : func->body) {
            if (auto decl = std::dynamic_pointer_cast<VarDecl>(stmt)) {
                Value val = eval(decl->expr);
                globals[decl->name] = val;
                if (DEBUG) std::cout << "[DEBUG] Set var " << decl->name << " = " << val.to_string() << std::endl;
            } else if (auto print = std::dynamic_pointer_cast<PrintStmt>(stmt)) {
                size_t fmt_idx = 0;
                for (const auto& arg : print->args) {
                    Value v = eval(arg);
                    std::string fmt = fmt_idx < print->formats.size() ? print->formats[fmt_idx++] : "";
                    std::cout << v.to_string(fmt);
                }
                std::cout << std::endl;
            } else if (auto call = std::dynamic_pointer_cast<ConectCall>(stmt)) {
                std::vector<Value> args;
                for (const auto& a : call->args) args.push_back(eval(a));
                if (DEBUG) std::cout << "[DEBUG] Calling " << call->func_name << " with " << args.size() << " args" << std::endl;
                execute_function(call->func_name, args);
            }
        }

        globals = saved;
    }

    Value eval(const std::shared_ptr<AstNode>& node) {
        if (!node) return Value();
        if (auto lit = std::dynamic_pointer_cast<Literal>(node)) return lit->value;
        if (auto id = std::dynamic_pointer_cast<Identifier>(node)) {
            if (globals.count(id->name)) return globals[id->name];
            return Value();
        }
        if (auto bin = std::dynamic_pointer_cast<BinaryOp>(node)) {
            Value l = eval(bin->left);
            Value r = eval(bin->right);
            if (l.type == ValueType::INT && r.type == ValueType::INT) {
                switch (bin->op) {
                    case T_PLUS: return Value(l.int_val + r.int_val);
                    case T_MINUS: return Value(l.int_val - r.int_val);
                    case T_STAR: return Value(l.int_val * r.int_val);
                    case T_SLASH: return r.int_val != 0 ? Value(l.int_val / r.int_val) : Value(0LL);
                    default: return Value();
                }
            }
            return Value();
        }
        return Value();
    }
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Program Generate Time (PGT) Compiler v0.1\n";
        std::cout << "Usage:\n";
        std::cout << "  pgt help                — Show this help\n";
        std::cout << "  pgt version             — Show version\n";
        std::cout << "  pgt run <file.pgt>      — Run PGT program\n";
        std::cout << "  pgt run <file.pgt> --debug — Run with debug output\n";
        return 0;
    }

    std::string command = argv[1];

    if (command == "help" || command == "--help" || command == "-h") {
        std::cout << "Program Generate Time (PGT) Compiler v0.1\n";
        std::cout << "Commands:\n";
        std::cout << "  help                    — Show this help message\n";
        std::cout << "  version                 — Show compiler version\n";
        std::cout << "  run <file.pgt>          — Execute .pgt file\n";
        std::cout << "  run <file.pgt> --debug  — Execute with debug info (tokens, params, calls)\n\n";
        std::cout << "Example:\n";
        std::cout << "  ./pgt run test.pgt\n";
        std::cout << "  ./pgt run test.pgt --debug\n";
        return 0;
    }

    if (command == "version" || command == "--version" || command == "-v") {
        std::cout << "PGT Compiler v0.1\n";
        std::cout << "Built on December 18, 2025\n";
        std::cout << "Author: pabla\n";
        return 0;
    }

    if (command == "run") {
        if (argc < 3) {
            std::cerr << "Error: No input file specified.\n";
            std::cerr << "Usage: pgt run <file.pgt> [--debug]\n";
            return 1;
        }

        std::string filename = argv[2];

        // Проверка --debug
        if (argc == 4 && std::string(argv[3]) == "--debug") {
            DEBUG = true;
        } else if (argc > 3) {
            std::cerr << "Unknown argument: " << argv[3] << "\n";
            std::cerr << "Use 'pgt help' for usage.\n";
            return 1;
        }

        std::ifstream f(filename);
        if (!f) {
            std::cerr << "Error: Cannot open file '" << filename << "'\n";
            return 1;
        }

        std::string source((std::istreambuf_iterator<char>(f)), {});

        if (DEBUG) std::cout << "[DEBUG] File loaded: " << filename << " (" << source.size() << " bytes)\n";

        Lexer lexer(source);
        std::vector<Token> tokens;
        Token t;
        do {
            t = lexer.next_token();
            tokens.push_back(t);
        } while (t.type != T_EOF);

        Parser parser;
        parser.load_tokens(tokens);
        auto program = parser.parse_program();

        Interpreter interp;
        interp.run(program);

        return 0;
    }

    // Если команда неизвестна
    std::cerr << "Unknown command: " << command << "\n";
    std::cerr << "Use 'pgt help' for available commands.\n";
    return 1;
}