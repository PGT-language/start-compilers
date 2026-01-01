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
                    // Пропускаем пробелы, табуляции и переносы строк после кавычки
                    while (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r') {
                        if (peek() == '\n') line++;
                        get();
                    }
                    // Если следующий символ - закрывающая скобка, запятая или конец файла, то это конец строки
                    if (peek() == ')' || peek() == ',' || peek() == ';' || peek() == 0) {
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
                        // Также убираем пробелы и табуляции в конце каждой строки (перед переносом строки)
                        // и пробелы/табуляции в начале строки (после переноса строки), чтобы избежать лишних пустых строк
                        std::string cleaned;
                        bool at_line_start = true;
                        for (size_t i = 0; i < str.length(); ++i) {
                            if (str[i] == '\n' || str[i] == '\r') {
                                // Удаляем пробелы и табуляции перед переносом строки
                                while (!cleaned.empty() && (cleaned.back() == ' ' || cleaned.back() == '\t')) {
                                    cleaned.pop_back();
                                }
                                cleaned += str[i];
                                at_line_start = true;
                            } else if (at_line_start && (str[i] == ' ' || str[i] == '\t')) {
                                // Пропускаем пробелы и табуляции в начале строки (они уже учтены в отступах)
                                // Но сохраняем их, если они идут перед текстом
                                // На самом деле, нужно сохранять отступы, но убирать лишние пробелы
                                // Пока просто пропускаем пробелы в начале строки
                                continue;
                            } else {
                                cleaned += str[i];
                                at_line_start = false;
                            }
                        }
                        str = cleaned;
                        break;
                    }
                    // Иначе это кавычка внутри строки, возвращаемся назад
                    pos = save_pos;
                    str += '"';
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