#pragma once

#include "Ast.h"
#include "Error.h"
#include <map>
#include <string>
#include <vector>
#include <memory>

// Информация о типе переменной
enum class VarType {
    INT,
    FLOAT,
    STRING,
    UNKNOWN  // Тип еще не определен
};

struct VariableInfo {
    VarType type;
    SourceLocation decl_location;
    bool is_initialized;
    
    VariableInfo(VarType t = VarType::UNKNOWN, const SourceLocation& loc = SourceLocation())
        : type(t), decl_location(loc), is_initialized(false) {}
};

struct FunctionInfo {
    std::vector<VarType> param_types;
    VarType return_type;
    SourceLocation decl_location;
    
    FunctionInfo(const std::vector<VarType>& params = {}, VarType ret = VarType::UNKNOWN, 
                 const SourceLocation& loc = SourceLocation())
        : param_types(params), return_type(ret), decl_location(loc) {}
};

// Семантический анализатор
class SemanticAnalyzer {
    std::map<std::string, VariableInfo> global_vars;
    std::map<std::string, FunctionInfo> functions;
    std::vector<std::map<std::string, VariableInfo>> scopes;  // Стек областей видимости
    
    // Вспомогательные функции
    VarType get_value_type(const Value& val);
    VarType infer_expr_type(const std::shared_ptr<AstNode>& node);
    void enter_scope();
    void exit_scope();
    void declare_variable(const std::string& name, VarType type, const SourceLocation& loc);
    VariableInfo* find_variable(const std::string& name);
    
    // Анализ узлов AST
    void analyze_program(const std::vector<std::shared_ptr<AstNode>>& program);
    void analyze_function(const std::shared_ptr<FunctionDef>& func);
    void analyze_statement(const std::shared_ptr<AstNode>& stmt);
    void analyze_var_decl(const std::shared_ptr<VarDecl>& decl);
    void analyze_expr(const std::shared_ptr<AstNode>& expr);
    void analyze_print(const std::shared_ptr<PrintStmt>& print);
    void analyze_input(const std::shared_ptr<InputStmt>& input);
    void analyze_if(const std::shared_ptr<IfStmt>& if_stmt);
    void analyze_call(const std::shared_ptr<ConectCall>& call);
    
public:
    void analyze(const std::vector<std::shared_ptr<AstNode>>& program);
};

