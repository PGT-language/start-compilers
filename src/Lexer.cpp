#include "Lexer.h"
#include <cctype>
#include <iostream>

char Lexer::peek() const { return pos < source.size() ? source[pos] : 0; }
char Lexer::get() { return pos < source.size() ? source[pos++] : 0; }

Lexer::Lexer(std::string src) : source(std::move(src)) {}

Token Lexer::next_token() {
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
        if (c == '=') {
            get();
            if (peek() == '=') {
                get();
                return {T_EQUAL_EQUAL, "==", line};
            }
            return {T_EQUAL, "=", line};
        }
        if (c == '>') {
            get();
            if (peek() == '=') {
                get();
                return {T_GREATER_EQUAL, ">=", line};
            }
            return {T_GREATER, ">", line};
        }
        if (c == '<') {
            get();
            if (peek() == '=') {
                get();
                return {T_LESS_EQUAL, "<=", line};
            }
            return {T_LESS, "<", line};
        }
        if (c == '!') {
            get();
            if (peek() == '=') {
                get();
                return {T_NOT_EQUAL, "!=", line};
            }
            // Если это не !=, возвращаемся назад
            pos--;
            // Продолжаем как обычный символ
        }
        if (c == ':') {
            get();
            if (peek() == ':') {
                get();
                return {T_COLON_COLON, "::", line};
            }
            // Если это не ::, возвращаемся назад
            pos--;
            // Продолжаем как обычный символ
        }

        if (c == '"') {
            get();
            std::string str;
            bool is_first_char = true;
            
            // Пропускаем первый перенос строки и пробелы/табуляции после открывающей кавычки
            if (peek() == '\n' || peek() == '\r') {
                if (peek() == '\n') line++;
                get();
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
                
                if (peek() == '"') {
                    // Проверяем контекст после закрывающей кавычки
                    // Сохраняем текущую позицию
                    size_t save_pos = pos;
                    get(); // съедаем кавычку
                    size_t after_quote_pos = pos;
                    int saved_line = line;
                    // Пропускаем пробелы, табуляции и переносы строк после кавычки
                    while (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r') {
                        if (peek() == '\n') line++;
                        get();
                    }
                    // Если следующий символ - закрывающая скобка, запятая, идентификатор или конец файла, то это конец строки
                    if (peek() == ')' || peek() == ',' || peek() == ';' || peek() == 0 || isalpha(peek())) {
                        // Возвращаемся к позиции сразу после кавычки, чтобы не пропустить символы
                        pos = after_quote_pos;
                        line = saved_line;
                        // Убираем пробелы, табуляции и переносы строк в конце строки
                        // Удаляем все пробельные символы с конца, включая последний перенос строки
                        while (!str.empty()) {
                            char last = str.back();
                            if (last == ' ' || last == '\t' || last == '\n' || last == '\r') {
                                str.pop_back();
                            } else {
                                break;
                            }
                        }
                        // Убираем пробелы и табуляции в конце каждой строки (перед переносом строки)
                        // и удаляем пустые строки (переносы строк, после которых идут только пробелы до следующего переноса)
                        // Также убираем отступы в начале строк после переноса строки
                        std::string cleaned;
                        bool after_newline = false;
                        for (size_t i = 0; i < str.length(); ++i) {
                            if (str[i] == '\n' || str[i] == '\r') {
                                // Удаляем пробелы и табуляции перед переносом строки
                                while (!cleaned.empty() && (cleaned.back() == ' ' || cleaned.back() == '\t')) {
                                    cleaned.pop_back();
                                }
                                // Проверяем, что идет после этого переноса строки
                                size_t j = i + 1;
                                // Пропускаем пробелы и табуляции после переноса (отступы)
                                while (j < str.length() && (str[j] == ' ' || str[j] == '\t')) {
                                    j++;
                                }
                                // Если после пробелов идет еще один перенос строки или конец строки, пропускаем этот перенос
                                if (j >= str.length() || str[j] == '\n' || str[j] == '\r') {
                                    // Пропускаем этот перенос строки
                                    i = j - 1;
                                    continue;
                                }
                                // Иначе сохраняем перенос строки и продолжаем (пробелы уже пропущены)
                                cleaned += str[i];
                                after_newline = true;
                            } else {
                                // Если мы после переноса строки и это пробел/табуляция, пропускаем (отступы уже убраны)
                                if (after_newline && (str[i] == ' ' || str[i] == '\t')) {
                                    // Пропускаем пробелы в начале строки
                                    continue;
                                }
                                cleaned += str[i];
                                after_newline = false;
                            }
                        }
                        str = cleaned;
                        break;
                    }
                    // Иначе это кавычка внутри строки, возвращаемся назад и добавляем её в строку
                    // Но нужно продвинуться вперед, чтобы не зациклиться
                    pos = save_pos;
                    str += '"';
                    // Продвигаемся вперед, пропуская кавычку, чтобы не зациклиться
                    if (pos < source.size()) {
                        pos++; // Пропускаем кавычку
                    }
                } else {
                    char ch = get();
                    // Не добавляем пробелы и табуляции, если они идут перед закрывающей кавычкой на новой строке
                    // (это будет обработано позже при встрече кавычки)
                    if (ch == '\n') {
                        line++;
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
            while (std::isalnum(peek()) || peek() == '_') id += get();

            if (id == "package") return {T_PACKAGE, id, line};
            if (id == "function") return {T_FUNCTION, id, line};
            if (id == "print") return {T_PRINT, id, line};
            if (id == "printg") return {T_PRINTG, id, line};
            if (id == "println") return {T_PRINTLN, id, line};
            if (id == "return") return {T_RETURN, id, line};
            if (id == "conect") return {T_CONECT, id, line};
            if (id == "input") return {T_INPUT, id, line};
            if (id == "cout") return {T_COUT, id, line};
            if (id == "int") return {T_INT, id, line};
            if (id == "float") return {T_FLOAT, id, line};
            if (id == "string") return {T_STRING, id, line};
            if (id == "from") return {T_FROM, id, line};
            if (id == "import") return {T_IMPORT, id, line};
            if (id == "if") return {T_IF, id, line};
            if (id == "else") return {T_ELSE, id, line};
            if (id == "create") return {T_CREATE, id, line};
            if (id == "write") return {T_WRITE, id, line};
            if (id == "read") return {T_READ, id, line};
            if (id == "close") return {T_CLOSE, id, line};
            if (id == "delete") return {T_DELETE, id, line};
            if (id == "file") return {T_FILE, id, line};

            return {T_IDENTIFIER, id, line};
        }

        get();
    }
}