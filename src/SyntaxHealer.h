#pragma once

#include "Token.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
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
            Erase,
            Replace
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

    static TextEdit make_replace(const Token& token,
                                 const std::string& replacement,
                                 const std::string& message,
                                 size_t order) {
        TextEdit edit;
        edit.kind = TextEdit::Kind::Replace;
        edit.line = token.line;
        edit.column = token.column;
        edit.text = replacement;
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
        bool ends_with_newline = !source.empty() && source.back() == '\n';
        int line = static_cast<int>(starts.empty() ? 1 : starts.size());
        int column = static_cast<int>(line_end_column(source, line));
        if (ends_with_newline) {
            line++;
            column = 1;
        }
        return make_insert(line, column, text, message, order);
    }

    static TextEdit replace_gap_after_token(const std::string& source,
                                            const Token& previous,
                                            const Token& current,
                                            const std::string& text,
                                            const std::string& message,
                                            size_t order) {
        TextEdit edit;
        edit.kind = TextEdit::Kind::Replace;
        edit.line = previous.line;
        edit.column = previous.column + static_cast<int>(source_token_length(previous));
        edit.text = text;
        edit.order = order;
        edit.message = message;

        size_t start = offset_for(source, edit.line, edit.column);
        size_t end = offset_for(source, current.line, current.column);
        edit.length = end > start ? end - start : 0;
        return edit;
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
            if (current.type == T_RETURN &&
                previous.type == T_NUMBER &&
                previous.value == "1" &&
                current.line > previous.line) {
                return replace_gap_after_token(source,
                                               previous,
                                               current,
                                               "\n" + text + "\n\n",
                                               message,
                                               order);
            }
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
        std::vector<TextEdit> typo_edits = plan_typo_edits(tokens);
        if (!typo_edits.empty()) {
            return typo_edits;
        }

        std::vector<TextEdit> edits;
        std::vector<HealingToken> normalized = normalize(tokens, &edits);
        balance(normalized, source, &edits);
        return edits;
    }

    static std::string lower_ascii(const std::string& value) {
        std::string lowered;
        lowered.reserve(value.size());
        for (char raw_ch : value) {
            lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(raw_ch)));
        }
        return lowered;
    }

    static size_t edit_distance(const std::string& left, const std::string& right) {
        std::vector<size_t> previous(right.size() + 1);
        std::vector<size_t> current(right.size() + 1);

        for (size_t j = 0; j <= right.size(); ++j) {
            previous[j] = j;
        }

        for (size_t i = 1; i <= left.size(); ++i) {
            current[0] = i;
            for (size_t j = 1; j <= right.size(); ++j) {
                size_t cost = left[i - 1] == right[j - 1] ? 0 : 1;
                current[j] = std::min({
                    previous[j] + 1,
                    current[j - 1] + 1,
                    previous[j - 1] + cost
                });
            }
            previous.swap(current);
        }

        return previous[right.size()];
    }

    static size_t typo_threshold(const std::string& word,
                                 const std::string& candidate,
                                 bool strong_context) {
        size_t length_delta = word.size() > candidate.size()
            ? word.size() - candidate.size()
            : candidate.size() - word.size();
        if (strong_context) {
            size_t threshold = std::max<size_t>(2, length_delta + 1);
            if (candidate.size() <= 3) {
                threshold = std::max<size_t>(threshold, 3);
            }
            return threshold;
        }
        return candidate.size() <= 4 ? 1 : 2;
    }

    static size_t typo_score(const std::string& word,
                             const std::string& candidate,
                             bool strong_context) {
        std::string lowered_word = lower_ascii(word);
        std::string lowered_candidate = lower_ascii(candidate);
        if (lowered_word == lowered_candidate) return 0;

        size_t distance = edit_distance(lowered_word, lowered_candidate);
        if (distance <= typo_threshold(lowered_word, lowered_candidate, strong_context)) {
            return distance;
        }
        return impossible_score();
    }

    static size_t impossible_score() {
        return static_cast<size_t>(-1) / 4;
    }

    static bool is_statement_start(const std::vector<Token>& tokens, size_t index) {
        if (index == 0) return true;
        const Token& previous = tokens[index - 1];
        const Token& current = tokens[index];
        return previous.line < current.line ||
               previous.type == T_LBRACE ||
               previous.type == T_RBRACE;
    }

    static bool is_identifier_token(const Token& token) {
        return token.type == T_IDENTIFIER ||
               token.type == T_READ ||
               token.type == T_WRITE ||
               token.type == T_FILE ||
               token.type == T_OBJECT;
    }

    static bool contains_word(const std::vector<std::string>& values,
                              const std::string& word) {
        for (const auto& value : values) {
            if (value == word) return true;
        }
        return false;
    }

    static std::string best_word(const std::string& word,
                                 const std::vector<std::string>& candidates,
                                 bool strong_context) {
        std::string best;
        size_t best_score = impossible_score();
        bool ambiguous = false;

        for (const auto& candidate : candidates) {
            size_t score = typo_score(word, candidate, strong_context);
            if (score == 0 || score >= impossible_score()) {
                continue;
            }
            if (score < best_score) {
                best = candidate;
                best_score = score;
                ambiguous = false;
            } else if (score == best_score) {
                ambiguous = true;
            }
        }

        return ambiguous ? "" : best;
    }

    static std::vector<std::string> type_words() {
        return {"int", "float", "string", "bool", "bytes", "object", "array"};
    }

    static std::vector<std::string> statement_words() {
        return {
            "package", "from", "import", "function", "class", "return",
            "if", "else", "while", "call", "cout", "print", "printg", "println"
        };
    }

    static std::vector<std::string> plain_builtin_words() {
        return {
            "protocol", "json_parse", "json_stringify", "read_file",
            "open_log", "request_method", "request_path", "request_body",
            "request_json", "log", "log_output", "set_log_output",
            "log_console", "log_file", "log_trace", "log_debug", "log_info",
            "log_notice", "log_warn", "log_warning", "log_error",
            "log_critical", "log_critecal", "log_fatal"
        };
    }

    struct BuiltinPair {
        const char* root;
        const char* member;
    };

    static std::vector<BuiltinPair> builtin_pairs() {
        return {
            {"web", "route"}, {"web", "get"}, {"web", "post"},
            {"web", "serve"}, {"web", "run"},
            {"net", "get"}, {"net", "post"}, {"net", "serve"}, {"net", "run"},
            {"log", "output"}, {"log", "set_output"}, {"log", "console"},
            {"log", "file"}, {"log", "trace"}, {"log", "debug"},
            {"log", "info"}, {"log", "notice"}, {"log", "warn"},
            {"log", "warning"}, {"log", "error"}, {"log", "critical"},
            {"log", "critecal"}, {"log", "fatal"},
            {"json", "parse"}, {"json", "decode"}, {"json", "unmarshal"},
            {"json", "stringify"}, {"json", "encode"}, {"json", "marshal"},
            {"json", "write"}, {"json", "save"}, {"json", "read"},
            {"json", "get"}, {"json", "object"},
            {"auth", "hash_password"}, {"auth", "verify_password"},
            {"jwt", "sign"}, {"jwt", "verify"},
            {"sql", "open"}, {"sql", "connect"}, {"sql", "exec"},
            {"sql", "table"}, {"sql", "insert"}, {"sql", "find"},
            {"orm", "table"}, {"orm", "migrate"}, {"orm", "save"},
            {"orm", "find"},
            {"request", "method"}, {"request", "path"}, {"request", "body"},
            {"request", "json"},
            {"create", "file"}, {"write", "file"}, {"read", "file"},
            {"close", "file"}, {"delete", "file"}
        };
    }

    static bool is_exact_builtin_pair(const std::string& root,
                                      const std::string& member) {
        for (const auto& pair : builtin_pairs()) {
            if (root == pair.root && member == pair.member) {
                return true;
            }
        }
        return false;
    }

    static std::string correction_message(const std::string& original,
                                          const std::string& replacement) {
        return "syntax typo '" + original + "' was corrected to '" + replacement + "'";
    }

    static void add_replace_if_needed(std::vector<TextEdit>& edits,
                                      const Token& token,
                                      const std::string& replacement) {
        if (replacement.empty() || token.value == replacement) return;
        edits.push_back(make_replace(token,
                                     replacement,
                                     correction_message(token.value, replacement),
                                     edits.size()));
    }

    static bool tokens_are_adjacent(const Token& left, const Token& right) {
        return left.line == right.line &&
               right.column == left.column + static_cast<int>(source_token_length(left));
    }

    static bool can_follow_number_suffix_removal(TokenType type) {
        return type == T_EOF ||
               type == T_RBRACE || type == T_RPAREN || type == T_COMMA ||
               type == T_PLUS || type == T_MINUS || type == T_STAR || type == T_SLASH ||
               type == T_GREATER || type == T_LESS ||
               type == T_EQUAL_EQUAL || type == T_NOT_EQUAL ||
               type == T_GREATER_EQUAL || type == T_LESS_EQUAL;
    }

    static void add_numeric_suffix_edits(std::vector<TextEdit>& edits,
                                         const std::vector<Token>& tokens,
                                         size_t index) {
        if (index == 0 || index >= tokens.size()) return;
        const Token& token = tokens[index];
        const Token& previous = tokens[index - 1];
        if (token.type != T_IDENTIFIER || previous.type != T_NUMBER) return;
        if (!tokens_are_adjacent(previous, token)) return;

        TokenType next_type = index + 1 < tokens.size() ? tokens[index + 1].type : T_EOF;
        if (!can_follow_number_suffix_removal(next_type) &&
            !(index + 1 < tokens.size() && tokens[index + 1].line > token.line)) {
            return;
        }

        edits.push_back(make_erase(token,
                                   "invalid numeric suffix '" + token.value + "' was removed",
                                   edits.size()));
    }

    static bool has_package_main(const std::vector<Token>& tokens) {
        for (size_t i = 0; i + 1 < tokens.size(); ++i) {
            if (tokens[i].type == T_PACKAGE &&
                tokens[i + 1].type == T_IDENTIFIER &&
                tokens[i + 1].value == "main") {
                return true;
            }
        }
        return false;
    }

    static int brace_depth_before(const std::vector<Token>& tokens, size_t index) {
        int depth = 0;
        for (size_t i = 0; i < index; ++i) {
            if (tokens[i].type == T_LBRACE) {
                depth++;
            } else if (tokens[i].type == T_RBRACE && depth > 0) {
                depth--;
            }
        }
        return depth;
    }

    static bool only_closing_braces_after(const std::vector<Token>& tokens,
                                          size_t index) {
        for (size_t i = index + 1; i < tokens.size(); ++i) {
            if (is_eof(tokens[i].type)) return true;
            if (tokens[i].type != T_RBRACE) return false;
        }
        return true;
    }

    static bool previous_statement_is_return_one(const std::vector<Token>& tokens,
                                                 size_t index) {
        if (index < 2) return false;
        size_t value_index = index - 1;
        return tokens[value_index].type == T_NUMBER &&
               tokens[value_index].value == "1" &&
               tokens[value_index - 1].type == T_RETURN;
    }

    static void add_main_exit_return_edits(std::vector<TextEdit>& edits,
                                           const std::vector<Token>& tokens,
                                           size_t index,
                                           bool package_main) {
        if (!package_main || index + 1 >= tokens.size()) return;
        const Token& token = tokens[index];
        const Token& value = tokens[index + 1];
        if (token.type != T_RETURN || value.type != T_NUMBER) return;
        if (value.value == "0" || !only_closing_braces_after(tokens, index + 1)) {
            return;
        }

        int depth = brace_depth_before(tokens, index);
        if (depth > 0 && !previous_statement_is_return_one(tokens, index)) {
            return;
        }

        edits.push_back(make_replace(value,
                                     "0",
                                     "main exit code '" + value.value + "' was corrected to '0'",
                                     edits.size()));
    }

    static void add_namespace_typo_edits(std::vector<TextEdit>& edits,
                                         const std::vector<Token>& tokens,
                                         size_t index) {
        if (index + 2 >= tokens.size()) return;
        const Token& root = tokens[index];
        const Token& separator = tokens[index + 1];
        const Token& member = tokens[index + 2];
        if (!is_identifier_token(root) || separator.type != T_COLON_COLON ||
            !is_identifier_token(member)) {
            return;
        }
        if (is_exact_builtin_pair(root.value, member.value)) {
            return;
        }

        std::string best_root;
        std::string best_member;
        size_t best_score = impossible_score();
        bool ambiguous = false;

        for (const auto& pair : builtin_pairs()) {
            size_t member_score = typo_score(member.value, pair.member, true);
            if (member_score >= impossible_score()) {
                continue;
            }
            bool strong_root = member_score == 0;
            size_t root_score = typo_score(root.value, pair.root, strong_root);
            if (root_score >= impossible_score()) {
                continue;
            }
            size_t total_score = root_score + member_score;
            bool anchored = root_score == 0 || member_score == 0 || total_score <= 3;
            if (!anchored || total_score == 0) {
                continue;
            }

            if (total_score < best_score) {
                best_root = pair.root;
                best_member = pair.member;
                best_score = total_score;
                ambiguous = false;
            } else if (total_score == best_score) {
                ambiguous = true;
            }
        }

        if (ambiguous || best_score >= impossible_score()) {
            return;
        }

        add_replace_if_needed(edits, root, best_root);
        add_replace_if_needed(edits, member, best_member);
    }

    static bool should_correct_statement_word(const std::vector<Token>& tokens,
                                              size_t index,
                                              const std::string& candidate) {
        const Token& token = tokens[index];
        bool statement_start = is_statement_start(tokens, index);
        TokenType next_type = index + 1 < tokens.size() ? tokens[index + 1].type : T_EOF;
        TokenType previous_type = index > 0 ? tokens[index - 1].type : T_EOF;

        if (candidate == "import") {
            return previous_type == T_STRING_LITERAL || previous_type == T_IDENTIFIER;
        }
        if (candidate == "package" || candidate == "from" ||
            candidate == "return" || candidate == "else") {
            return statement_start;
        }
        if (candidate == "function" || candidate == "class" ||
            candidate == "if" || candidate == "while" ||
            candidate == "call" || candidate == "cout" ||
            candidate == "print" || candidate == "printg" ||
            candidate == "println") {
            return statement_start && (next_type == T_LPAREN || next_type == T_IDENTIFIER);
        }
        return statement_start && token.column > 0;
    }

    static std::vector<std::string> strong_statement_words(const std::vector<Token>& tokens,
                                                           size_t index) {
        if (!is_statement_start(tokens, index) || index + 1 >= tokens.size()) {
            return {};
        }

        TokenType next_type = tokens[index + 1].type;
        if (next_type == T_LPAREN) {
            return {
                "function", "if", "while", "call",
                "cout", "print", "printg", "println"
            };
        }
        if (next_type == T_IDENTIFIER) {
            if (index == 0) {
                return {"package", "function", "class"};
            }
            return {"function", "class"};
        }
        return {};
    }

    static bool has_keyword_prefix_damage(const std::string& word,
                                          const std::string& candidate) {
        std::string lowered_word = lower_ascii(word);
        std::string lowered_candidate = lower_ascii(candidate);
        size_t prefix = 0;
        while (prefix < lowered_word.size() &&
               prefix < lowered_candidate.size() &&
               lowered_word[prefix] == lowered_candidate[prefix]) {
            prefix++;
        }
        return prefix >= std::min<size_t>(5, lowered_candidate.size() - 1);
    }

    static bool has_repeated_extra_suffix(const std::string& word,
                                          const std::string& candidate) {
        std::string lowered_word = lower_ascii(word);
        std::string lowered_candidate = lower_ascii(candidate);
        if (lowered_word.size() <= lowered_candidate.size()) return false;
        if (lowered_word.compare(0, lowered_candidate.size(), lowered_candidate) != 0) {
            return false;
        }

        std::string suffix = lowered_word.substr(lowered_candidate.size());
        if (suffix.empty() || suffix.size() > 3) return false;
        return std::all_of(suffix.begin(), suffix.end(), [&](char ch) {
            return ch == suffix.front();
        });
    }

    static size_t strong_statement_score(const std::string& word,
                                         const std::string& candidate) {
        size_t normal_score = typo_score(word, candidate, false);
        if (normal_score < impossible_score()) return normal_score;
        if (has_repeated_extra_suffix(word, candidate)) {
            return edit_distance(lower_ascii(word), lower_ascii(candidate));
        }
        if (candidate == "package" && has_keyword_prefix_damage(word, candidate)) {
            size_t package_score = typo_score(word, candidate, true);
            if (package_score < impossible_score()) return package_score;
        }
        return impossible_score();
    }

    static std::string best_strong_statement_word(const std::string& word,
                                                  const std::vector<std::string>& candidates) {
        std::string best;
        size_t best_score = impossible_score();
        bool ambiguous = false;

        for (const auto& candidate : candidates) {
            size_t score = strong_statement_score(word, candidate);
            if (score == 0 || score >= impossible_score()) {
                continue;
            }
            if (score < best_score) {
                best = candidate;
                best_score = score;
                ambiguous = false;
            } else if (score == best_score) {
                ambiguous = true;
            }
        }

        return ambiguous ? "" : best;
    }

    static void add_word_typo_edits(std::vector<TextEdit>& edits,
                                    const std::vector<Token>& tokens,
                                    size_t index) {
        const Token& token = tokens[index];
        if (token.type != T_IDENTIFIER) return;
        if ((index > 0 && tokens[index - 1].type == T_COLON_COLON) ||
            (index + 1 < tokens.size() && tokens[index + 1].type == T_COLON_COLON)) {
            return;
        }

        if (index > 0 && tokens[index - 1].type == T_PLUS) {
            std::string type_replacement = best_word(token.value, type_words(), false);
            add_replace_if_needed(edits, token, type_replacement);
            if (!type_replacement.empty()) return;
        }

        std::vector<std::string> strong_statement_replacements =
            strong_statement_words(tokens, index);
        std::string strong_statement_replacement =
            best_strong_statement_word(token.value, strong_statement_replacements);
        if (!strong_statement_replacement.empty() &&
            should_correct_statement_word(tokens, index, strong_statement_replacement)) {
            add_replace_if_needed(edits, token, strong_statement_replacement);
            return;
        }

        std::string statement_replacement = best_word(token.value, statement_words(), false);
        if (!statement_replacement.empty() &&
            should_correct_statement_word(tokens, index, statement_replacement)) {
            add_replace_if_needed(edits, token, statement_replacement);
            return;
        }

        if (index + 1 < tokens.size() && tokens[index + 1].type == T_LPAREN) {
            std::vector<std::string> builtin_words = plain_builtin_words();
            builtin_words.push_back("print");
            builtin_words.push_back("printg");
            builtin_words.push_back("println");
            builtin_words.push_back("cout");
            std::string builtin_replacement = best_word(token.value, builtin_words, false);
            add_replace_if_needed(edits, token, builtin_replacement);
        }
    }

    static std::vector<TextEdit> plan_typo_edits(const std::vector<Token>& tokens) {
        std::vector<TextEdit> edits;
        bool package_main = has_package_main(tokens);
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (is_eof(tokens[i].type)) break;
            add_numeric_suffix_edits(edits, tokens, i);
            add_main_exit_return_edits(edits, tokens, i, package_main);
            add_namespace_typo_edits(edits, tokens, i);
            add_word_typo_edits(edits, tokens, i);
        }
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
        for (auto& edit : edits) {
            edit.offset = offset_for(source, edit.line, edit.column);
        }

        std::stable_sort(edits.begin(), edits.end(),
                         [](const TextEdit& left, const TextEdit& right) {
                             if (left.offset != right.offset) return left.offset > right.offset;
                             return left.order > right.order;
                         });

        std::string result = source;
        for (const auto& edit : edits) {
            size_t offset = std::min(edit.offset, result.size());
            if (edit.kind == TextEdit::Kind::Erase) {
                size_t length = std::min(edit.length, result.size() - offset);
                result.erase(offset, length);
            } else if (edit.kind == TextEdit::Kind::Replace) {
                size_t length = std::min(edit.length, result.size() - offset);
                result.replace(offset, length, edit.text);
            } else {
                result.insert(offset, edit.text);
            }
        }
        return result;
    }
};
