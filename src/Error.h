#pragma once

#include <string>
#include <vector>
#include <stdexcept>

// Позиция в исходном коде
struct SourceLocation {
    int line = 0;
    int column = 0;
    std::string file_path;
    
    SourceLocation() = default;
    SourceLocation(int l, int c, const std::string& f = "") : line(l), column(c), file_path(f) {}
    
    std::string to_string() const {
        if (file_path.empty()) {
            return "line " + std::to_string(line) + ", column " + std::to_string(column);
        }
        return file_path + ":" + std::to_string(line) + ":" + std::to_string(column);
    }
};

// Базовый класс для ошибок компиляции/выполнения
class CompilerError : public std::runtime_error {
public:
    SourceLocation location;
    std::vector<SourceLocation> traceback;  // Стек вызовов для traceback
    
    CompilerError(const std::string& msg, const SourceLocation& loc = SourceLocation())
        : std::runtime_error(msg), location(loc) {}
    
    std::string get_traceback() const {
        std::string result = "Error: " + std::string(what()) + "\n";
        if (location.line > 0) {
            result += "  at " + location.to_string() + "\n";
        }
        if (!traceback.empty()) {
            result += "Traceback (most recent call last):\n";
            for (size_t i = traceback.size(); i > 0; --i) {
                result += "  " + std::to_string(traceback.size() - i + 1) + ". " + traceback[i-1].to_string() + "\n";
            }
        }
        return result;
    }
};

// Ошибки семантического анализа
class SemanticError : public CompilerError {
public:
    SemanticError(const std::string& msg, const SourceLocation& loc = SourceLocation())
        : CompilerError("Semantic error: " + msg, loc) {}
};

// Ошибки выполнения
class RuntimeError : public CompilerError {
public:
    RuntimeError(const std::string& msg, const SourceLocation& loc = SourceLocation())
        : CompilerError("Runtime error: " + msg, loc) {}
};

// Ошибки типов
class TypeError : public SemanticError {
public:
    TypeError(const std::string& msg, const SourceLocation& loc = SourceLocation())
        : SemanticError("Type error: " + msg, loc) {}
};

// Ошибки неопределенных переменных/функций
class UndefinedError : public SemanticError {
public:
    UndefinedError(const std::string& name, const std::string& kind, const SourceLocation& loc = SourceLocation())
        : SemanticError("Undefined " + kind + ": " + name, loc) {}
};

