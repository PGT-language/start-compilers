#pragma once

#include "Token.h"
#include <string>
#include <vector>

class Lexer {
    std::string source;
    size_t pos = 0;
    int line = 1;

    char peek() const;
    char get();

public:
    explicit Lexer(std::string src);
    Token next_token();
};