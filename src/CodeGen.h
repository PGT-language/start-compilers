#pragma once

#include "Ast.h"
#include "Utils.h"
#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <memory>

// Генератор C кода из AST
class CodeGen {
    std::stringstream code;  // Генерируемый код
    std::map<std::string, std::string> function_signatures;  // Сигнатуры функций
    int indent_level = 0;  // Уровень отступов
    int temp_var_counter = 0;  // Счетчик временных переменных
    
    // Вспомогательные функции
    void write_indent();
    std::string get_temp_var();
    std::string generate_expr(const std::shared_ptr<AstNode>& expr);
    std::string get_c_type(const std::string& pgt_type);
    void generate_function(const std::shared_ptr<FunctionDef>& func);
    void generate_statement(const std::shared_ptr<AstNode>& stmt);
    void generate_print(const std::shared_ptr<PrintStmt>& print);
    void generate_input(const std::shared_ptr<InputStmt>& input);
    void generate_if(const std::shared_ptr<IfStmt>& if_stmt);
    void generate_file_op(const std::shared_ptr<FileOp>& file_op);
    void generate_var_decl(const std::shared_ptr<VarDecl>& decl);
    void generate_call(const std::shared_ptr<ConectCall>& call);
    
public:
    std::string generate(const std::vector<std::shared_ptr<AstNode>>& program);
    void save_to_file(const std::string& filename);
};

