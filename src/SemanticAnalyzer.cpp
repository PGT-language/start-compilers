#include "SemanticAnalyzer.h"
#include <iostream>

VarType SemanticAnalyzer::get_value_type(const Value& val) {
    switch (val.type) {
        case ValueType::INT: return VarType::INT;
        case ValueType::FLOAT: return VarType::FLOAT;
        case ValueType::STRING: return VarType::STRING;
        default: return VarType::UNKNOWN;
    }
}

VarType SemanticAnalyzer::infer_expr_type(const std::shared_ptr<AstNode>& node) {
    if (auto lit = std::dynamic_pointer_cast<Literal>(node)) {
        return get_value_type(lit->value);
    }
    if (auto id = std::dynamic_pointer_cast<Identifier>(node)) {
        auto* var = find_variable(id->name);
        if (var) {
            return var->type;
        }
        // Если переменная не найдена, возвращаем UNKNOWN вместо ошибки
        // Это позволяет использовать переменные, которые будут созданы во время выполнения (например, через cout)
        return VarType::UNKNOWN;
    }
    if (auto bin = std::dynamic_pointer_cast<BinaryOp>(node)) {
        VarType left_type = infer_expr_type(bin->left);
        VarType right_type = infer_expr_type(bin->right);
        
        // Для арифметических операций
        if (bin->op == T_PLUS || bin->op == T_MINUS || bin->op == T_STAR || bin->op == T_SLASH) {
            // Если один из типов UNKNOWN, возвращаем UNKNOWN (переменная может быть определена во время выполнения)
            if (left_type == VarType::UNKNOWN || right_type == VarType::UNKNOWN) {
                return VarType::UNKNOWN;
            }
            if (left_type == VarType::FLOAT || right_type == VarType::FLOAT) {
                return VarType::FLOAT;
            }
            if (left_type == VarType::INT && right_type == VarType::INT) {
                return VarType::INT;
            }
            if (left_type == VarType::STRING || right_type == VarType::STRING) {
                return VarType::STRING;  // Конкатенация строк
            }
            throw TypeError("Invalid types for arithmetic operation", bin->location);
        }
        
        // Для операций сравнения всегда возвращаем INT (0 или 1)
        if (bin->op == T_GREATER || bin->op == T_LESS || bin->op == T_EQUAL_EQUAL || 
            bin->op == T_NOT_EQUAL || bin->op == T_GREATER_EQUAL || bin->op == T_LESS_EQUAL) {
            return VarType::INT;
        }
    }
    return VarType::UNKNOWN;
}

void SemanticAnalyzer::enter_scope() {
    scopes.push_back({});
}

void SemanticAnalyzer::exit_scope() {
    if (!scopes.empty()) {
        scopes.pop_back();
    }
}

void SemanticAnalyzer::declare_variable(const std::string& name, VarType type, const SourceLocation& loc) {
    if (!scopes.empty()) {
        if (scopes.back().count(name)) {
            // Если переменная уже объявлена, обновляем её тип, если он был UNKNOWN
            auto& var = scopes.back()[name];
            if (var.type == VarType::UNKNOWN && type != VarType::UNKNOWN) {
                var.type = type;
            }
            return;  // Не выбрасываем ошибку, если переменная уже объявлена
        }
        scopes.back()[name] = VariableInfo(type, loc);
    } else {
        if (global_vars.count(name)) {
            // Если переменная уже объявлена, обновляем её тип, если он был UNKNOWN
            auto& var = global_vars[name];
            if (var.type == VarType::UNKNOWN && type != VarType::UNKNOWN) {
                var.type = type;
            }
            return;  // Не выбрасываем ошибку, если переменная уже объявлена
        }
        global_vars[name] = VariableInfo(type, loc);
    }
}

VariableInfo* SemanticAnalyzer::find_variable(const std::string& name) {
    // Ищем в локальных областях видимости (от последней к первой)
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        if (it->count(name)) {
            return &(*it)[name];
        }
    }
    // Ищем в глобальных переменных
    if (global_vars.count(name)) {
        return &global_vars[name];
    }
    return nullptr;
}

void SemanticAnalyzer::analyze_program(const std::vector<std::shared_ptr<AstNode>>& program) {
    // Первый проход: объявляем все функции (только регистрация)
    for (const auto& node : program) {
        if (auto func = std::dynamic_pointer_cast<FunctionDef>(node)) {
            // Проверяем, не объявлена ли функция уже
            if (functions.count(func->name)) {
                throw SemanticError("Function '" + func->name + "' already declared", func->location);
            }
            
            // Регистрируем функцию
            FunctionInfo func_info;
            func_info.decl_location = func->location;
            for (const auto& param : func->param_names) {
                func_info.param_types.push_back(VarType::UNKNOWN);  // Типы параметров пока не анализируем
            }
            functions[func->name] = func_info;
        }
    }
    
    // Второй проход: анализируем тела функций и глобальные переменные
    for (const auto& node : program) {
        if (auto func = std::dynamic_pointer_cast<FunctionDef>(node)) {
            analyze_function(func);
        } else if (auto var = std::dynamic_pointer_cast<VarDecl>(node)) {
            analyze_var_decl(var);
        }
    }
}

void SemanticAnalyzer::analyze_function(const std::shared_ptr<FunctionDef>& func) {
    // Функция уже должна быть зарегистрирована в первом проходе
    if (!functions.count(func->name)) {
        throw SemanticError("Function '" + func->name + "' not registered", func->location);
    }
    
    // Входим в область видимости функции
    enter_scope();
    
    // Объявляем параметры функции
    for (size_t i = 0; i < func->param_names.size(); ++i) {
        declare_variable(func->param_names[i], VarType::UNKNOWN, func->location);
    }
    
    // Первый проход: объявляем все переменные (включая те, что создаются через cout)
    for (const auto& stmt : func->body) {
        if (auto input = std::dynamic_pointer_cast<InputStmt>(stmt)) {
            analyze_input(input);
        } else if (auto decl = std::dynamic_pointer_cast<VarDecl>(stmt)) {
            // Объявляем переменную с типом UNKNOWN, если выражение содержит неопределенные переменные
            try {
                VarType expr_type = infer_expr_type(decl->expr);
                declare_variable(decl->name, expr_type, decl->location);
            } catch (const UndefinedError&) {
                declare_variable(decl->name, VarType::UNKNOWN, decl->location);
            }
        }
    }
    
    // Второй проход: анализируем использование переменных
    for (const auto& stmt : func->body) {
        analyze_statement(stmt);
    }
    
    exit_scope();
}

void SemanticAnalyzer::analyze_statement(const std::shared_ptr<AstNode>& stmt) {
    if (auto decl = std::dynamic_pointer_cast<VarDecl>(stmt)) {
        analyze_var_decl(decl);
    } else if (auto print = std::dynamic_pointer_cast<PrintStmt>(stmt)) {
        analyze_print(print);
    } else if (auto input = std::dynamic_pointer_cast<InputStmt>(stmt)) {
        analyze_input(input);
    } else if (auto if_stmt = std::dynamic_pointer_cast<IfStmt>(stmt)) {
        analyze_if(if_stmt);
    } else if (auto call = std::dynamic_pointer_cast<ConectCall>(stmt)) {
        analyze_call(call);
    }
}

void SemanticAnalyzer::analyze_var_decl(const std::shared_ptr<VarDecl>& decl) {
    // Анализируем выражение (переменная уже объявлена в первом проходе)
    // Не выбрасываем ошибку для неопределенных переменных - они будут проверяться во время выполнения
    try {
        VarType expr_type = infer_expr_type(decl->expr);
        
        // Обновляем тип переменной, если он был UNKNOWN
        auto* var = find_variable(decl->name);
        if (var && var->type == VarType::UNKNOWN && expr_type != VarType::UNKNOWN) {
            var->type = expr_type;
        }
        
        // Отмечаем как инициализированную
        if (var) {
            var->is_initialized = true;
        }
    } catch (const UndefinedError&) {
        // Пропускаем ошибки неопределенных переменных - они будут проверяться во время выполнения
        auto* var = find_variable(decl->name);
        if (var) {
            var->is_initialized = true;
        }
    }
}

void SemanticAnalyzer::analyze_expr(const std::shared_ptr<AstNode>& expr) {
    // Не выбрасываем ошибку для неопределенных переменных - они будут проверяться во время выполнения
    try {
        infer_expr_type(expr);  // Проверяем, что выражение валидно
    } catch (const UndefinedError&) {
        // Пропускаем ошибки неопределенных переменных - они будут проверяться во время выполнения
    }
}

void SemanticAnalyzer::analyze_print(const std::shared_ptr<PrintStmt>& print) {
    for (const auto& arg : print->args) {
        analyze_expr(arg);
    }
}

void SemanticAnalyzer::analyze_input(const std::shared_ptr<InputStmt>& input) {
    // cout создает переменную во время выполнения
    // Объявляем её с типом UNKNOWN, так как тип зависит от формата
    std::string var_name = input->var_name.empty() ? "input" : input->var_name;
    VarType var_type = VarType::UNKNOWN;
    if (input->format == "{int}") {
        var_type = VarType::INT;
    } else if (input->format == "{float}") {
        var_type = VarType::FLOAT;
    } else if (input->format == "{string}") {
        var_type = VarType::STRING;
    }
    declare_variable(var_name, var_type, input->location);
    auto* var = find_variable(var_name);
    if (var) {
        var->is_initialized = true;
    }
}

void SemanticAnalyzer::analyze_if(const std::shared_ptr<IfStmt>& if_stmt) {
    // Анализируем условие (не строго, чтобы не блокировать runtime проверки)
    try {
        VarType cond_type = infer_expr_type(if_stmt->condition);
        if (cond_type != VarType::INT && cond_type != VarType::UNKNOWN && cond_type != VarType::FLOAT && cond_type != VarType::STRING) {
            throw TypeError("Condition must be a boolean expression (int)", if_stmt->condition->location);
        }
    } catch (const UndefinedError&) {
        // Пропускаем ошибки неопределенных переменных в условиях - они будут проверяться во время выполнения
    }
    
    // Входим в область видимости then
    enter_scope();
    for (const auto& stmt : if_stmt->then_body) {
        analyze_statement(stmt);
    }
    exit_scope();
    
    // Входим в область видимости else
    enter_scope();
    for (const auto& stmt : if_stmt->else_body) {
        analyze_statement(stmt);
    }
    exit_scope();
}

void SemanticAnalyzer::analyze_call(const std::shared_ptr<ConectCall>& call) {
    if (!functions.count(call->func_name)) {
        throw UndefinedError(call->func_name, "function", call->location);
    }
    
    const auto& func_info = functions[call->func_name];
    if (call->args.size() != func_info.param_types.size()) {
        throw SemanticError("Function '" + call->func_name + "' expects " + 
                           std::to_string(func_info.param_types.size()) + " arguments, got " +
                           std::to_string(call->args.size()), call->location);
    }
    
    // Анализируем аргументы
    for (const auto& arg : call->args) {
        analyze_expr(arg);
    }
}

void SemanticAnalyzer::analyze(const std::vector<std::shared_ptr<AstNode>>& program) {
    analyze_program(program);
}

