#include "Lexer.h"
#include <cctype>
#include <iostream>

char Lexer::peek() const { return pos < source.size() ? source[pos] : 0; }
char Lexer::get() {
    if (pos >= source.size()) return 0;
    column++;
    return source[pos++];
}

Lexer::Lexer(std::string src) : source(std::move(src)) {}

Token Lexer::next_token() {
    while (true) {
        char c = peek();
        if (c == 0) return {T_EOF, "", line, column};
        if (std::isspace(c)) {
            get();
            if (c == '\n') {
                line++;
                column = 1;
            }
            continue;
        }

        int token_line = line;
        int token_column = column;

        if (c == '{') { get(); return {T_LBRACE, "{", token_line, token_column}; }
        if (c == '}') { get(); return {T_RBRACE, "}", token_line, token_column}; }
        if (c == '(') { get(); return {T_LPAREN, "(", token_line, token_column}; }
        if (c == ')') { get(); return {T_RPAREN, ")", token_line, token_column}; }
        if (c == ',') { get(); return {T_COMMA, ",", token_line, token_column}; }
        if (c == '+') { get(); return {T_PLUS, "+", token_line, token_column}; }
        if (c == '-') { get(); return {T_MINUS, "-", token_line, token_column}; }
        if (c == '*') { get(); return {T_STAR, "*", token_line, token_column}; }
        if (c == '/') { get(); return {T_SLASH, "/", token_line, token_column}; }
        if (c == '=') {
            get();
            if (peek() == '=') {
                get();
                return {T_EQUAL_EQUAL, "==", token_line, token_column};
            }
            return {T_EQUAL, "=", token_line, token_column};
        }
        if (c == '>') {
            get();
            if (peek() == '=') {
                get();
                return {T_GREATER_EQUAL, ">=", token_line, token_column};
            }
            return {T_GREATER, ">", token_line, token_column};
        }
        if (c == '<') {
            get();
            if (peek() == '=') {
                get();
                return {T_LESS_EQUAL, "<=", token_line, token_column};
            }
            return {T_LESS, "<", token_line, token_column};
        }
        if (c == '!') {
            get();
            if (peek() == '=') {
                get();
                return {T_NOT_EQUAL, "!=", token_line, token_column};
            }
            // Если это не !=, возвращаемся назад
            pos--;
            column--;
            // Продолжаем как обычный символ
        }
        if (c == ':') {
            get();
            if (peek() == ':') {
                get();
                return {T_COLON_COLON, "::", token_line, token_column};
            }
            // Если это не ::, возвращаемся назад
            pos--;
            column--;
            // Продолжаем как обычный символ
        }

        if (c == '"') {
            get();
            std::string str;
            bool is_first_char = true;

            // Пропускаем первый перенос строки и пробелы/табуляции после открывающей кавычки
            if (peek() == '\n' || peek() == '\r') {
                char newline = get();
                if (newline == '\n' || newline == '\r') {
                    line++;
                    column = 1;
                }
                // Пропускаем пробелы и табуляции после первого переноса строки
                while (peek() == ' ' || peek() == '\t') {
                    get();
                }
                is_first_char = false;
            }

            size_t iterations = 0;
            size_t last_pos = pos;
            while (peek() != 0) {
                iterations++;
                if (iterations > 100000) {
                    // Защита от бесконечного цикла
                    std::cerr << "Error: Infinite loop in string lexer at line " << line << std::endl;
                    break;
                }

                // Проверяем, не застряли ли мы на одном месте
                if (pos == last_pos && iterations > 1000) {
                    std::cerr << "Error: Lexer stuck at position " << pos << " in string" << std::endl;
                    break;
                }
                last_pos = pos;

                if (peek() == '\\') {
                    get(); // пропускаем backslash
                    char escaped = get();
                    if (escaped == 0) {
                        break;
                    }
                    if (escaped == 'n') {
                        str += '\n';
                    } else if (escaped == 't') {
                        str += '\t';
                    } else if (escaped == 'r') {
                        str += '\r';
                    } else if (escaped == '"') {
                        str += '"';
                    } else if (escaped == '\\') {
                        str += '\\';
                    } else {
                        str += escaped;
                    }
                    continue;
                }

                if (peek() == '"') {
                    get();
                    break;
                } else {
                    char ch = get();
                    // Не добавляем пробелы и табуляции, если они идут перед закрывающей кавычкой на новой строке
                    // (это будет обработано позже при встрече кавычки)
                    if (ch == '\n') {
                        line++;
                        column = 1;
                        str += '\n';
                    } else if (ch == ' ' || ch == '\t') {
                        // Пропускаем пробелы и табуляции, если они идут перед закрывающей кавычкой
                        // Но это сложно определить заранее, поэтому просто добавляем их
                        str += ch;
                    } else {
                        str += ch;
                    }
                    is_first_char = false;
                }
            }
            return {T_STRING_LITERAL, str, token_line, token_column};
        }

        if (std::isdigit(c) || (c == '-' && std::isdigit(peek() + 1)) || c == '.') {
            std::string num;
            if (c == '-' || c == '.') num += get();
            while (std::isdigit(peek()) || peek() == '.') num += get();
            return {T_NUMBER, num, token_line, token_column};
        }

        if (std::isalpha(c) || c == '_') {
            std::string id;
            while (std::isalnum(peek()) || peek() == '_' || peek() == '.') id += get();

            if (id == "package") return {T_PACKAGE, id, token_line, token_column};
            if (id == "function") return {T_FUNCTION, id, token_line, token_column};
            if (id == "class") return {T_CLASS, id, token_line, token_column};
            if (id == "print") return {T_PRINT, id, token_line, token_column};
            if (id == "printg") return {T_PRINTG, id, token_line, token_column};
            if (id == "println") return {T_PRINTLN, id, token_line, token_column};
            if (id == "return") return {T_RETURN, id, token_line, token_column};
            if (id == "call") return {T_CALL, id, token_line, token_column};
            if (id == "input") return {T_INPUT, id, token_line, token_column};
            if (id == "cout") return {T_COUT, id, token_line, token_column};
            if (id == "int") return {T_INT, id, token_line, token_column};
            if (id == "float") return {T_FLOAT, id, token_line, token_column};
            if (id == "string") return {T_STRING, id, token_line, token_column};
            if (id == "bool") return {T_BOOL_TYPE, id, token_line, token_column};
            if (id == "bytes") return {T_BYTES, id, token_line, token_column};
            if (id == "object") return {T_OBJECT, id, token_line, token_column};
            if (id == "array") return {T_ARRAY, id, token_line, token_column};
            if (id == "from") return {T_FROM, id, token_line, token_column};
            if (id == "import") return {T_IMPORT, id, token_line, token_column};
            if (id == "if") return {T_IF, id, token_line, token_column};
            if (id == "else") return {T_ELSE, id, token_line, token_column};
            if (id == "while") return {T_WHILE, id, token_line, token_column};
            if (id == "true" || id == "True") return {T_TRUE, id, token_line, token_column};
            if (id == "false" || id == "False") return {T_FALSE, id, token_line, token_column};
            if (id == "create") return {T_CREATE, id, token_line, token_column};
            if (id == "write") return {T_WRITE, id, token_line, token_column};
            if (id == "read") return {T_READ, id, token_line, token_column};
            if (id == "close") return {T_CLOSE, id, token_line, token_column};
            if (id == "delete") return {T_DELETE, id, token_line, token_column};
            if (id == "file") return {T_FILE, id, token_line, token_column};

            return {T_IDENTIFIER, id, token_line, token_column};
        }

        get();
    }
}
