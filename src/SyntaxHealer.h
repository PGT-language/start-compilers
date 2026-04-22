#pragma once

#include "Token.h"
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

class SyntaxHealer {
public:
    struct Diagnostic {
        int line = 0;
        int column = 0;
        std::string message;
    };

    struct RepairResult {
        std::string source;
        std::vector<Diagnostic> diagnostics;
        bool changed = false;
    };

    static RepairResult repair_source(const std::string& source,
                                      const std::vector<Token>& tokens) {
        std::vector<TextEdit> edits = plan_edits(source, tokens);
        RepairResult result;
        result.source = source;
        result.changed = !edits.empty();

        for (const auto& edit : edits) {
            result.diagnostics.push_back({edit.line, edit.column, edit.message});
        }

        if (!edits.empty()) {
            result.source = apply_edits(source, edits);
            result.changed = result.source != source;
        }

        return result;
    }

    static std::vector<Token> heal(const std::vector<Token>& tokens) {
        std::vector<HealingToken> normalized = normalize(tokens, nullptr);
        return balance(normalized, "", nullptr);
    }

private:
    struct HealingToken {
        Token token;
        bool synthetic = false;
    };

    struct TextEdit {
        enum class Kind {
            Insert,
            Erase
        };

        Kind kind = Kind::Insert;
        int line = 0;
        int column = 0;
        std::string text;
        size_t length = 0;
        size_t order = 0;
        size_t offset = 0;
        std::string message;
    };

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

    static Token synthetic(TokenType type, int line, int column) {
        return {type, token_value(type), line, column};
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

    static bool should_insert_equal_after_type(const std::vector<HealingToken>& out,
                                               const std::vector<Token>& tokens,
                                               size_t index) {
        if (out.size() < 3 || !is_type(out.back().token.type)) return false;
        if (out[out.size() - 2].token.type != T_PLUS) return false;
        if (out[out.size() - 3].token.type != T_IDENTIFIER) return false;
        if (index + 1 >= tokens.size()) return false;

        TokenType next_type = tokens[index + 1].type;
        if (next_type == T_EQUAL || next_type == T_COMMA ||
            next_type == T_RPAREN || next_type == T_RBRACE ||
            next_type == T_EOF) {
            return false;
        }
        return can_start_expression(next_type);
    }

    static bool starts_statement_at(const std::vector<HealingToken>& tokens, size_t index) {
        const Token& token = tokens[index].token;
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
                (tokens[index + 1].token.type == T_COLON_COLON ||
                 tokens[index + 1].token.type == T_LPAREN ||
                 tokens[index + 1].token.type == T_PLUS)) {
                return true;
            }
            return token.value == "web" || token.value == "net";
        }

        return token.type == T_RBRACE;
    }

    static bool should_close_paren_before(const std::vector<HealingToken>& tokens,
                                          size_t index,
                                          const std::vector<Token>& stack,
                                          const Token& previous) {
        if (stack.empty() || stack.back().type != T_LPAREN) return false;
        const Token& current = tokens[index].token;
        if (current.type == T_RPAREN || current.type == T_COMMA) return false;
        if (!can_end_expression(previous.type)) return false;
        if (current.type == T_LBRACE || current.type == T_RBRACE) return true;
        if (previous.line <= 0 || current.line <= previous.line) return false;
        return starts_statement_at(tokens, index);
    }

    static bool should_close_brace_before(const std::vector<HealingToken>& tokens,
                                          size_t index,
                                          const std::vector<Token>& stack,
                                          const Token& previous) {
        if (stack.empty() || stack.back().type != T_LBRACE) return false;
        const Token& current = tokens[index].token;
        if (current.type == T_ELSE) {
            return previous.type != T_RBRACE && can_end_statement(previous.type);
        }
        if (current.type == T_RETURN &&
            index + 1 < tokens.size() &&
            tokens[index + 1].token.type == T_NUMBER &&
            tokens[index + 1].token.value == "0" &&
            previous.type == T_NUMBER &&
            previous.value == "1" &&
            current.line > previous.line) {
            return true;
        }
        if (current.type != T_FUNCTION && current.type != T_CLASS &&
            current.type != T_FROM && current.type != T_PACKAGE) {
            return false;
        }
        if (previous.line <= 0 || current.line <= previous.line) return false;
        return can_end_statement(previous.type);
    }

    static bool should_insert_comma_before(const std::vector<HealingToken>& tokens,
                                           size_t index,
                                           const std::vector<Token>& stack,
                                           const Token& previous) {
        if (stack.empty() || stack.back().type != T_LPAREN) return false;
        const Token& current = tokens[index].token;
        if (current.type == T_LPAREN) return false;
        if (!can_end_expression(previous.type) || !can_start_expression(current.type)) {
            return false;
        }
        if (starts_statement_at(tokens, index) && current.line > previous.line) {
            return false;
        }
        return true;
    }

    static size_t source_token_length(const Token& token) {
        switch (token.type) {
            case T_LBRACE:
            case T_RBRACE:
            case T_LPAREN:
            case T_RPAREN:
            case T_COMMA:
            case T_PLUS:
            case T_MINUS:
            case T_STAR:
            case T_SLASH:
            case T_EQUAL:
            case T_GREATER:
            case T_LESS:
                return 1;
            case T_EQUAL_EQUAL:
            case T_NOT_EQUAL:
            case T_GREATER_EQUAL:
            case T_LESS_EQUAL:
            case T_COLON_COLON:
                return 2;
            case T_STRING_LITERAL:
                return token.value.size() + 2;
            default:
                return token.value.size();
        }
    }

    static TextEdit make_insert(int line,
                                int column,
                                const std::string& text,
                                const std::string& message,
                                size_t order) {
        TextEdit edit;
        edit.kind = TextEdit::Kind::Insert;
        edit.line = line;
        edit.column = column;
        edit.text = text;
        edit.order = order;
        edit.message = message;
        return edit;
    }

    static TextEdit make_erase(const Token& token,
                               const std::string& message,
                               size_t order) {
        TextEdit edit;
        edit.kind = TextEdit::Kind::Erase;
        edit.line = token.line;
        edit.column = token.column;
        edit.length = source_token_length(token);
        edit.order = order;
        edit.message = message;
        return edit;
    }

    static size_t line_end_column(const std::string& source, int line) {
        std::vector<size_t> starts = line_starts(source);
        if (line <= 0 || static_cast<size_t>(line) > starts.size()) {
            return source.empty() ? 1 : source.size() + 1;
        }

        size_t start = starts[static_cast<size_t>(line - 1)];
        size_t end = source.size();
        if (static_cast<size_t>(line) < starts.size()) {
            end = starts[static_cast<size_t>(line)] - 1;
        }
        if (end > start && source[end - 1] == '\n') {
            end--;
        }
        if (end > start && source[end - 1] == '\r') {
            end--;
        }
        return end - start + 1;
    }

    static TextEdit insert_after_token(const Token& token,
                                       const std::string& text,
                                       const std::string& message,
                                       size_t order) {
        return make_insert(token.line,
                           token.column + static_cast<int>(source_token_length(token)),
                           text,
                           message,
                           order);
    }

    static TextEdit insert_before_token(const Token& token,
                                        const std::string& text,
                                        const std::string& message,
                                        size_t order) {
        return make_insert(token.line, token.column, text, message, order);
    }

    static TextEdit insert_at_line_end(const std::string& source,
                                       const Token& token,
                                       const std::string& text,
                                       const std::string& message,
                                       size_t order) {
        return make_insert(token.line,
                           static_cast<int>(line_end_column(source, token.line)),
                           text,
                           message,
                           order);
    }

    static TextEdit insert_line_before_token(const Token& token,
                                             const std::string& text,
                                             const std::string& message,
                                             size_t order) {
        return make_insert(token.line, 1, text, message, order);
    }

    static TextEdit append_to_source(const std::string& source,
                                     const std::string& text,
                                     const std::string& message,
                                     size_t order) {
        std::vector<size_t> starts = line_starts(source);
        int line = static_cast<int>(starts.empty() ? 1 : starts.size());
        int column = static_cast<int>(line_end_column(source, line));
        return make_insert(line, column, text, message, order);
    }

    static std::string close_message(TokenType type) {
        if (type == T_RPAREN) return "missing ')' was inserted";
        if (type == T_RBRACE) return "missing '}' was inserted";
        return "missing token was inserted";
    }

    static TextEdit close_edit(const std::string& source,
                               TokenType close_type,
                               const Token& current,
                               bool at_eof,
                               const Token& previous,
                               size_t order) {
        std::string text = token_value(close_type);
        std::string message = close_message(close_type);

        if (at_eof) {
            if (close_type == T_RBRACE) {
                std::string prefix = source.empty() || source.back() == '\n' ? "" : "\n";
                return append_to_source(source, prefix + text, message, order);
            }
            return append_to_source(source, text, message, order);
        }

        if (close_type == T_RBRACE) {
            return insert_line_before_token(current, text + "\n", message, order);
        }

        if (current.line == previous.line && current.column > 0) {
            return insert_before_token(current, text, message, order);
        }

        return insert_at_line_end(source, previous, text, message, order);
    }

    static std::vector<HealingToken> normalize(const std::vector<Token>& tokens,
                                               std::vector<TextEdit>* edits) {
        std::vector<HealingToken> out;
        out.reserve(tokens.size() + 16);

        for (size_t i = 0; i < tokens.size(); ++i) {
            const Token& token = tokens[i];
            if (is_eof(token.type)) break;

            out.push_back({token, false});

            if (expects_paren_after(tokens, i) &&
                (i + 1 >= tokens.size() || tokens[i + 1].type != T_LPAREN)) {
                Token inserted = synthetic(T_LPAREN,
                                           token.line,
                                           token.column + static_cast<int>(source_token_length(token)));
                out.push_back({inserted, true});
                if (edits) {
                    edits->push_back(insert_after_token(token,
                                                        "(",
                                                        "missing '(' was inserted",
                                                        edits->size()));
                }
            }

            if (should_insert_equal_after_type(out, tokens, i)) {
                Token inserted = synthetic(T_EQUAL,
                                           token.line,
                                           token.column + static_cast<int>(source_token_length(token)));
                out.push_back({inserted, true});
                if (edits) {
                    edits->push_back(insert_after_token(token,
                                                        "=",
                                                        "missing '=' was inserted",
                                                        edits->size()));
                }
            }
        }

        int eof_line = tokens.empty() ? 0 : tokens.back().line;
        int eof_column = tokens.empty() ? 0 : tokens.back().column;
        out.push_back({{T_EOF, "", eof_line, eof_column}, false});
        return out;
    }

    static void close_top(std::vector<Token>& out,
                          std::vector<Token>& stack,
                          const std::string& source,
                          std::vector<TextEdit>* edits,
                          const Token& current,
                          bool at_eof,
                          const Token& previous) {
        Token open = stack.back();
        stack.pop_back();
        TokenType close_type = close_for(open.type);
        Token inserted = synthetic(close_type, previous.line, previous.column);
        out.push_back(inserted);

        if (edits) {
            edits->push_back(close_edit(source,
                                        close_type,
                                        current,
                                        at_eof,
                                        previous,
                                        edits->size()));
        }
    }

    static std::vector<Token> balance(const std::vector<HealingToken>& tokens,
                                      const std::string& source,
                                      std::vector<TextEdit>* edits) {
        std::vector<Token> out;
        std::vector<Token> stack;
        Token previous;

        out.reserve(tokens.size() + 16);
        stack.reserve(16);

        for (size_t i = 0; i < tokens.size(); ++i) {
            const Token& token = tokens[i].token;
            bool at_eof = is_eof(token.type);

            while (should_close_brace_before(tokens, i, stack, previous)) {
                close_top(out, stack, source, edits, token, at_eof, previous);
                previous = out.back();
            }

            while (should_close_paren_before(tokens, i, stack, previous)) {
                close_top(out, stack, source, edits, token, at_eof, previous);
                previous = out.back();
            }

            if (at_eof) break;

            if (should_insert_comma_before(tokens, i, stack, previous)) {
                Token inserted = synthetic(T_COMMA, previous.line, previous.column);
                out.push_back(inserted);
                if (edits) {
                    if (token.line == previous.line && token.column > 0) {
                        edits->push_back(insert_before_token(token,
                                                            ",",
                                                            "missing ',' was inserted",
                                                            edits->size()));
                    } else {
                        edits->push_back(insert_at_line_end(source,
                                                            previous,
                                                            ",",
                                                            "missing ',' was inserted",
                                                            edits->size()));
                    }
                }
                previous = out.back();
            }

            if (is_close(token.type)) {
                bool skip_current = false;
                while (!stack.empty() && !matches(stack.back().type, token.type)) {
                    if (stack.back().type == T_LPAREN) {
                        close_top(out, stack, source, edits, token, false, previous);
                        previous = out.back();
                        continue;
                    }
                    skip_current = true;
                    break;
                }

                if (skip_current || stack.empty()) {
                    if (edits && !tokens[i].synthetic) {
                        edits->push_back(make_erase(token,
                                                     "extra closing token was removed",
                                                     edits->size()));
                    }
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
            close_top(out, stack, source, edits, previous, true, previous);
            previous = out.back();
        }

        int eof_line = tokens.empty() ? 0 : tokens.back().token.line;
        int eof_column = tokens.empty() ? 0 : tokens.back().token.column;
        out.push_back({T_EOF, "", eof_line, eof_column});
        return out;
    }

    static std::vector<TextEdit> plan_edits(const std::string& source,
                                            const std::vector<Token>& tokens) {
        std::vector<TextEdit> edits;
        std::vector<HealingToken> normalized = normalize(tokens, &edits);
        balance(normalized, source, &edits);
        return edits;
    }

    static std::vector<size_t> line_starts(const std::string& source) {
        std::vector<size_t> starts;
        starts.push_back(0);
        for (size_t i = 0; i < source.size(); ++i) {
            if (source[i] == '\n' && i + 1 < source.size()) {
                starts.push_back(i + 1);
            }
        }
        return starts;
    }

    static size_t offset_for(const std::string& source, int line, int column) {
        std::vector<size_t> starts = line_starts(source);
        if (line <= 0 || starts.empty()) return source.size();
        if (static_cast<size_t>(line) > starts.size()) return source.size();

        size_t start = starts[static_cast<size_t>(line - 1)];
        size_t end = source.size();
        if (static_cast<size_t>(line) < starts.size()) {
            end = starts[static_cast<size_t>(line)] - 1;
        }
        if (end > start && source[end - 1] == '\n') {
            end--;
        }
        if (end > start && source[end - 1] == '\r') {
            end--;
        }

        size_t safe_column = column <= 0 ? 0 : static_cast<size_t>(column - 1);
        size_t line_length = end - start;
        if (safe_column > line_length) {
            safe_column = line_length;
        }
        return start + safe_column;
    }

    static std::string apply_edits(const std::string& source,
                                   std::vector<TextEdit> edits) {
        std::stable_sort(edits.begin(), edits.end(),
                         [&source](const TextEdit& left, const TextEdit& right) {
                             size_t left_offset = offset_for(source, left.line, left.column);
                             size_t right_offset = offset_for(source, right.line, right.column);
                             if (left_offset != right_offset) return left_offset > right_offset;
                             return left.order > right.order;
                         });

        std::string result = source;
        for (const auto& edit : edits) {
            size_t offset = offset_for(result, edit.line, edit.column);
            if (edit.kind == TextEdit::Kind::Erase) {
                size_t length = std::min(edit.length, result.size() - offset);
                result.erase(offset, length);
            } else {
                result.insert(offset, edit.text);
            }
        }
        return result;
    }
};
