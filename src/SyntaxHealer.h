#pragma once

#include "Token.h"
#include <vector>

class SyntaxHealer {
public:
    static std::vector<Token> heal(const std::vector<Token>& tokens) {
        return balance(normalize(tokens));
    }

private:
    static bool is_eof(TokenType type) {
        return type == T_EOF;
    }

    static bool is_type(TokenType type) {
        return type == T_INT || type == T_FLOAT || type == T_STRING ||
               type == T_BOOL_TYPE || type == T_BYTES || type == T_OBJECT ||
               type == T_ARRAY;
    }

    static bool is_print(TokenType type) {
        return type == T_PRINT || type == T_PRINTG || type == T_PRINTLN;
    }

    static bool is_file_operation(TokenType type) {
        return type == T_CREATE || type == T_WRITE || type == T_READ ||
               type == T_CLOSE || type == T_DELETE;
    }

    static bool is_open(TokenType type) {
        return type == T_LPAREN || type == T_LBRACE;
    }

    static bool is_close(TokenType type) {
        return type == T_RPAREN || type == T_RBRACE;
    }

    static TokenType close_for(TokenType type) {
        if (type == T_LPAREN) return T_RPAREN;
        if (type == T_LBRACE) return T_RBRACE;
        return T_EOF;
    }

    static bool matches(TokenType open, TokenType close) {
        return (open == T_LPAREN && close == T_RPAREN) ||
               (open == T_LBRACE && close == T_RBRACE);
    }

    static const char* token_value(TokenType type) {
        switch (type) {
            case T_LPAREN: return "(";
            case T_RPAREN: return ")";
            case T_LBRACE: return "{";
            case T_RBRACE: return "}";
            case T_COMMA: return ",";
            case T_EQUAL: return "=";
            default: return "";
        }
    }

    static Token synthetic(TokenType type, int line) {
        return {type, token_value(type), line};
    }

    static bool can_end_expression(TokenType type) {
        return type == T_IDENTIFIER || type == T_INPUT ||
               type == T_STRING_LITERAL || type == T_NUMBER ||
               type == T_TRUE || type == T_FALSE ||
               type == T_RPAREN;
    }

    static bool can_end_statement(TokenType type) {
        return can_end_expression(type) || type == T_RBRACE;
    }

    static bool can_start_expression(TokenType type) {
        return type == T_IDENTIFIER || type == T_INPUT ||
               type == T_STRING_LITERAL || type == T_NUMBER ||
               type == T_TRUE || type == T_FALSE ||
               type == T_LPAREN ||
               type == T_READ;
    }

    static bool is_namespaced_root(const Token& token) {
        if (token.type == T_READ) return true;
        if (token.type != T_IDENTIFIER) return false;
        return token.value == "web" ||
               token.value == "net" ||
               token.value == "log" ||
               token.value == "json" ||
               token.value == "auth" ||
               token.value == "jwt" ||
               token.value == "sql" ||
               token.value == "orm" ||
               token.value == "request";
    }

    static bool expects_paren_after(const std::vector<Token>& tokens, size_t index) {
        const Token& token = tokens[index];
        if (token.type == T_FUNCTION || token.type == T_IF ||
            token.type == T_CALL || token.type == T_COUT ||
            is_print(token.type)) {
            return true;
        }

        if (index >= 2 && tokens[index - 1].type == T_COLON_COLON) {
            const Token& root = tokens[index - 2];
            if ((is_namespaced_root(root) && token.type == T_IDENTIFIER) ||
                (is_file_operation(root.type) && token.type == T_FILE)) {
                return true;
            }
        }

        return false;
    }

    static bool should_insert_equal_after_type(const std::vector<Token>& out,
                                               const std::vector<Token>& tokens,
                                               size_t index) {
        if (out.size() < 3 || !is_type(out.back().type)) return false;
        if (out[out.size() - 2].type != T_PLUS) return false;
        if (out[out.size() - 3].type != T_IDENTIFIER) return false;
        if (index + 1 >= tokens.size()) return false;

        TokenType next_type = tokens[index + 1].type;
        if (next_type == T_EQUAL || next_type == T_COMMA ||
            next_type == T_RPAREN || next_type == T_RBRACE ||
            next_type == T_EOF) {
            return false;
        }
        return can_start_expression(next_type);
    }

    static bool starts_statement_at(const std::vector<Token>& tokens, size_t index) {
        const Token& token = tokens[index];
        if (token.type == T_PACKAGE || token.type == T_FROM ||
            token.type == T_CLASS || token.type == T_FUNCTION ||
            token.type == T_RETURN || token.type == T_IF ||
            token.type == T_ELSE || token.type == T_WHILE ||
            token.type == T_CALL || token.type == T_COUT ||
            is_print(token.type) || is_file_operation(token.type)) {
            return true;
        }

        if (token.type == T_IDENTIFIER) {
            if (index + 1 < tokens.size() &&
                (tokens[index + 1].type == T_COLON_COLON ||
                 tokens[index + 1].type == T_LPAREN ||
                 tokens[index + 1].type == T_PLUS)) {
                return true;
            }
            return token.value == "web" || token.value == "net";
        }

        return token.type == T_RBRACE;
    }

    static bool should_close_paren_before(const std::vector<Token>& tokens,
                                          size_t index,
                                          const std::vector<Token>& stack,
                                          const Token& previous) {
        if (stack.empty() || stack.back().type != T_LPAREN) return false;
        const Token& current = tokens[index];
        if (current.type == T_RPAREN || current.type == T_COMMA) return false;
        if (!can_end_expression(previous.type)) return false;
        if (current.type == T_LBRACE || current.type == T_RBRACE) return true;
        if (previous.line <= 0 || current.line <= previous.line) return false;
        return starts_statement_at(tokens, index);
    }

    static bool should_close_brace_before(const std::vector<Token>& tokens,
                                          size_t index,
                                          const std::vector<Token>& stack,
                                          const Token& previous) {
        if (stack.empty() || stack.back().type != T_LBRACE) return false;
        const Token& current = tokens[index];
        if (current.type == T_ELSE) {
            return previous.type != T_RBRACE && can_end_statement(previous.type);
        }
        if (current.type != T_FUNCTION && current.type != T_CLASS &&
            current.type != T_FROM && current.type != T_PACKAGE) {
            return false;
        }
        if (previous.line <= 0 || current.line <= previous.line) return false;
        return can_end_statement(previous.type);
    }

    static bool should_insert_comma_before(const std::vector<Token>& tokens,
                                           size_t index,
                                           const std::vector<Token>& stack,
                                           const Token& previous) {
        if (stack.empty() || stack.back().type != T_LPAREN) return false;
        const Token& current = tokens[index];
        if (current.type == T_LPAREN) return false;
        if (!can_end_expression(previous.type) || !can_start_expression(current.type)) {
            return false;
        }
        if (starts_statement_at(tokens, index) && current.line > previous.line) {
            return false;
        }
        return true;
    }

    static std::vector<Token> normalize(const std::vector<Token>& tokens) {
        std::vector<Token> out;
        out.reserve(tokens.size() + 16);

        for (size_t i = 0; i < tokens.size(); ++i) {
            const Token& token = tokens[i];
            if (is_eof(token.type)) break;

            out.push_back(token);

            if (expects_paren_after(tokens, i) &&
                (i + 1 >= tokens.size() || tokens[i + 1].type != T_LPAREN)) {
                out.push_back(synthetic(T_LPAREN, token.line));
            }

            if (should_insert_equal_after_type(out, tokens, i)) {
                out.push_back(synthetic(T_EQUAL, token.line));
            }
        }

        int eof_line = tokens.empty() ? 0 : tokens.back().line;
        out.push_back({T_EOF, "", eof_line});
        return out;
    }

    static void close_top(std::vector<Token>& out, std::vector<Token>& stack) {
        Token open = stack.back();
        stack.pop_back();
        out.push_back(synthetic(close_for(open.type), open.line));
    }

    static std::vector<Token> balance(const std::vector<Token>& tokens) {
        std::vector<Token> out;
        std::vector<Token> stack;
        Token previous;

        out.reserve(tokens.size() + 16);
        stack.reserve(16);

        for (size_t i = 0; i < tokens.size(); ++i) {
            const Token& token = tokens[i];
            if (is_eof(token.type)) break;

            while (should_close_brace_before(tokens, i, stack, previous)) {
                close_top(out, stack);
                previous = out.back();
            }

            while (should_close_paren_before(tokens, i, stack, previous)) {
                close_top(out, stack);
                previous = out.back();
            }

            if (should_insert_comma_before(tokens, i, stack, previous)) {
                out.push_back(synthetic(T_COMMA, previous.line));
                previous = out.back();
            }

            if (is_close(token.type)) {
                bool skip_current = false;
                while (!stack.empty() && !matches(stack.back().type, token.type)) {
                    if (stack.back().type == T_LPAREN) {
                        close_top(out, stack);
                        previous = out.back();
                        continue;
                    }
                    skip_current = true;
                    break;
                }

                if (skip_current || stack.empty()) {
                    continue;
                }

                stack.pop_back();
                out.push_back(token);
                previous = token;
                continue;
            }

            out.push_back(token);
            previous = token;

            if (is_open(token.type)) {
                stack.push_back(token);
            }
        }

        while (!stack.empty()) {
            close_top(out, stack);
        }

        int eof_line = tokens.empty() ? 0 : tokens.back().line;
        out.push_back({T_EOF, "", eof_line});
        return out;
    }
};
