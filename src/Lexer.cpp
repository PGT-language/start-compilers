#include "Lexer.h"
#include <cctype>

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
        if (c == '=') { get(); return {T_EQUAL, "=", line}; }

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
            
            while (peek() != 0) {
                if (peek() == '"') {
                    // Проверяем контекст после закрывающей кавычки
                    // Сохраняем текущую позицию
                    size_t save_pos = pos;
                    get(); // съедаем кавычку
                    // Пропускаем пробелы, табуляции и переносы строк
                    while (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r') {
                        if (peek() == '\n') line++;
                        get();
                    }
                    // Если следующий символ - закрывающая скобка, запятая или конец файла, то это конец строки
                    if (peek() == ')' || peek() == ',' || peek() == ';' || peek() == 0) {
                        // Убираем последний перенос строки из строки, если он есть
                        if (!str.empty() && str.back() == '\n') {
                            str.pop_back();
                        }
                        break;
                    }
                    // Иначе это кавычка внутри строки, возвращаемся назад
                    pos = save_pos;
                    str += '"';
                } else {
                    char ch = get();
                    if (ch == '\n') {
                        line++;
                        str += '\n';
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
            while (std::isalnum(peek()) || peek() == '_' || peek() == ':') id += get();

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

            return {T_IDENTIFIER, id, line};
        }

        get();
    }
}