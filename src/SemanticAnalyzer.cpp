#include "SemanticAnalyzer.h"
#include <iostream>

namespace {
bool is_log_builtin_name(const std::string& name) {
    return name == "log" ||
           name == "log_output" ||
           name == "set_log_output" ||
           name == "log_console" ||
           name == "log_file" ||
           name == "log::output" ||
           name == "log::set_output" ||
           name == "log::console" ||
           name == "log::file" ||
           name == "log::trace" ||
           name == "log::debug" ||
           name == "log::info" ||
           name == "log::notice" ||
           name == "log::warn" ||
           name == "log::warning" ||
           name == "log::error" ||
           name == "log::critical" ||
           name == "log::critecal" ||
           name == "log::fatal" ||
           name == "log_trace" ||
           name == "log_debug" ||
           name == "log_info" ||
           name == "log_notice" ||
           name == "log_warn" ||
           name == "log_warning" ||
           name == "log_error" ||
           name == "log_critical" ||
           name == "log_critecal" ||
           name == "log_fatal";
}

bool is_log_config_builtin_name(const std::string& name) {
    return name == "log_output" ||
           name == "set_log_output" ||
           name == "log_console" ||
           name == "log_file" ||
           name == "log::output" ||
           name == "log::set_output" ||
           name == "log::console" ||
           name == "log::file";
}

bool is_json_builtin_name(const std::string& name) {
    return name.rfind("json::", 0) == 0;
}

bool is_sql_builtin_name(const std::string& name) {
    return name.rfind("sql::", 0) == 0 ||
           name.rfind("orm::", 0) == 0;
}

bool is_request_builtin_name(const std::string& name) {
    return name.rfind("request::", 0) == 0;
}

bool is_auth_builtin_name(const std::string& name) {
    return name.rfind("auth::", 0) == 0 ||
           name.rfind("jwt::", 0) == 0;
}

bool is_string_like(VarType type) {
    return type == VarType::STRING ||
           type == VarType::BYTES ||
           type == VarType::UNKNOWN;
}
}

VarType SemanticAnalyzer::get_value_type(const Value& val) {
    switch (val.type) {
        case ValueType::INT: return VarType::INT;
        case ValueType::FLOAT: return VarType::FLOAT;
        case ValueType::STRING: return VarType::STRING;
        case ValueType::BOOL: return VarType::BOOL;
        case ValueType::BYTES: return VarType::BYTES;
        default: return VarType::UNKNOWN;
    }
}

VarType SemanticAnalyzer::type_from_name(const std::string& type_name) {
    if (type_name == "int") return VarType::INT;
    if (type_name == "float") return VarType::FLOAT;
    if (type_name == "string") return VarType::STRING;
    if (type_name == "bool") return VarType::BOOL;
    if (type_name == "bytes") return VarType::BYTES;
    if (type_name == "object") return VarType::OBJECT;
    if (type_name == "array") return VarType::ARRAY;
    return VarType::UNKNOWN;
}

std::string SemanticAnalyzer::type_to_string(VarType type) {
    switch (type) {
        case VarType::INT: return "int";
        case VarType::FLOAT: return "float";
        case VarType::STRING: return "string";
        case VarType::BOOL: return "bool";
        case VarType::BYTES: return "bytes";
        case VarType::OBJECT: return "object";
        case VarType::ARRAY: return "array";
        default: return "unknown";
    }
}

bool SemanticAnalyzer::is_assignable(VarType expected, VarType actual) {
    if (expected == VarType::UNKNOWN || actual == VarType::UNKNOWN) {
        return true;
    }
    if (expected == actual) {
        return true;
    }
    if (expected == VarType::FLOAT && actual == VarType::INT) {
        return true;
    }
    if (expected == VarType::INT && actual == VarType::BOOL) {
        return true;
    }
    if (expected == VarType::FLOAT && actual == VarType::BOOL) {
        return true;
    }
    if (expected == VarType::BOOL &&
        (actual == VarType::INT || actual == VarType::FLOAT ||
         actual == VarType::STRING || actual == VarType::BYTES)) {
        return true;
    }
    if (expected == VarType::BYTES && actual == VarType::STRING) {
        return true;
    }
    if (expected == VarType::STRING && actual == VarType::BYTES) {
        return true;
    }
    return false;
}

VarType SemanticAnalyzer::infer_expr_type(const std::shared_ptr<AstNode>& node) {
    if (auto lit = std::dynamic_pointer_cast<Literal>(node)) {
        return get_value_type(lit->value);
    }
    if (auto builtin = std::dynamic_pointer_cast<BuiltinCallExpr>(node)) {
        if (builtin->name == "read::file") {
            if (builtin->args.size() != 1) {
                throw SemanticError("Builtin 'read::file' expects 1 argument", builtin->location);
            }
            VarType arg_type = infer_expr_type(builtin->args[0]);
            if (!is_string_like(arg_type)) {
                throw TypeError("Builtin 'read::file' expects a string path", builtin->location);
            }
            return VarType::STRING;
        }
        if (is_request_builtin_name(builtin->name)) {
            if (!builtin->args.empty()) {
                throw SemanticError("Builtin '" + builtin->name + "' expects 0 arguments", builtin->location);
            }
            if (builtin->name == "request::json") {
                return VarType::OBJECT;
            }
            return VarType::STRING;
        }
        if (is_json_builtin_name(builtin->name)) {
            if (builtin->name == "json::parse" || builtin->name == "json::decode" ||
                builtin->name == "json::unmarshal" || builtin->name == "json::read") {
                if (builtin->args.size() != 1) {
                    throw SemanticError("Builtin '" + builtin->name + "' expects 1 argument", builtin->location);
                }
                VarType arg_type = infer_expr_type(builtin->args[0]);
                if (!is_string_like(arg_type)) {
                    throw TypeError("Builtin '" + builtin->name + "' expects a string argument", builtin->location);
                }
                return VarType::OBJECT;
            }
            if (builtin->name == "json::stringify" || builtin->name == "json::encode" ||
                builtin->name == "json::marshal") {
                if (builtin->args.size() != 1) {
                    throw SemanticError("Builtin '" + builtin->name + "' expects 1 argument", builtin->location);
                }
                analyze_expr(builtin->args[0]);
                return VarType::STRING;
            }
            if (builtin->name == "json::write" || builtin->name == "json::save") {
                if (builtin->args.size() != 2) {
                    throw SemanticError("Builtin '" + builtin->name + "' expects path and value", builtin->location);
                }
                VarType path_type = infer_expr_type(builtin->args[0]);
                if (!is_string_like(path_type)) {
                    throw TypeError("JSON file path must be a string", builtin->location);
                }
                analyze_expr(builtin->args[1]);
                return VarType::BOOL;
            }
            if (builtin->name == "json::object") {
                if (builtin->args.size() % 2 != 0) {
                    throw SemanticError("Builtin 'json::object' expects key/value pairs", builtin->location);
                }
                for (size_t i = 0; i < builtin->args.size(); i += 2) {
                    VarType key_type = infer_expr_type(builtin->args[i]);
                    if (!is_string_like(key_type)) {
                        throw TypeError("JSON object keys must be strings", builtin->location);
                    }
                    analyze_expr(builtin->args[i + 1]);
                }
                return VarType::OBJECT;
            }
            if (builtin->name == "json::get") {
                if (builtin->args.size() != 2) {
                    throw SemanticError("Builtin 'json::get' expects object and key", builtin->location);
                }
                analyze_expr(builtin->args[0]);
                VarType key_type = infer_expr_type(builtin->args[1]);
                if (!is_string_like(key_type)) {
                    throw TypeError("JSON object key must be a string", builtin->location);
                }
                return VarType::UNKNOWN;
            }
        }
        if (is_sql_builtin_name(builtin->name)) {
            if (builtin->name == "sql::open" || builtin->name == "sql::connect" ||
                builtin->name == "sql::exec" || builtin->name == "orm::migrate") {
                if (builtin->args.size() != 1) {
                    throw SemanticError("Builtin '" + builtin->name + "' expects 1 argument", builtin->location);
                }
                VarType arg_type = infer_expr_type(builtin->args[0]);
                if (!is_string_like(arg_type)) {
                    throw TypeError("Builtin '" + builtin->name + "' expects a string argument", builtin->location);
                }
                return builtin->name == "sql::exec" || builtin->name == "orm::migrate" ? VarType::STRING : VarType::BOOL;
            }
            if (builtin->name == "sql::table" || builtin->name == "orm::table" ||
                builtin->name == "sql::insert" || builtin->name == "orm::save") {
                if (builtin->args.size() != 2) {
                    throw SemanticError("Builtin '" + builtin->name + "' expects 2 arguments", builtin->location);
                }
                VarType table_type = infer_expr_type(builtin->args[0]);
                if (!is_string_like(table_type)) {
                    throw TypeError("SQL table name must be a string", builtin->location);
                }
                analyze_expr(builtin->args[1]);
                return VarType::STRING;
            }
            if (builtin->name == "orm::find" || builtin->name == "sql::find") {
                if (builtin->args.size() != 3) {
                    throw SemanticError("Builtin '" + builtin->name + "' expects 3 arguments", builtin->location);
                }
                VarType table_type = infer_expr_type(builtin->args[0]);
                VarType field_type = infer_expr_type(builtin->args[1]);
                if (!is_string_like(table_type) || !is_string_like(field_type)) {
                    throw TypeError("SQL table and field names must be strings", builtin->location);
                }
                analyze_expr(builtin->args[2]);
                return VarType::OBJECT;
            }
        }
        if (is_auth_builtin_name(builtin->name)) {
            if (builtin->name == "auth::hash_password") {
                if (builtin->args.size() != 1) {
                    throw SemanticError("Builtin 'auth::hash_password' expects 1 argument", builtin->location);
                }
                VarType arg_type = infer_expr_type(builtin->args[0]);
                if (!is_string_like(arg_type)) {
                    throw TypeError("Password must be a string", builtin->location);
                }
                return VarType::STRING;
            }
            if (builtin->name == "auth::verify_password" || builtin->name == "jwt::verify") {
                if (builtin->args.size() != 2) {
                    throw SemanticError("Builtin '" + builtin->name + "' expects 2 arguments", builtin->location);
                }
                for (const auto& arg : builtin->args) {
                    VarType arg_type = infer_expr_type(arg);
                    if (!is_string_like(arg_type)) {
                        throw TypeError("Builtin '" + builtin->name + "' expects string arguments", builtin->location);
                    }
                }
                return VarType::BOOL;
            }
            if (builtin->name == "jwt::sign") {
                if (builtin->args.size() != 2) {
                    throw SemanticError("Builtin 'jwt::sign' expects payload and secret", builtin->location);
                }
                analyze_expr(builtin->args[0]);
                VarType secret_type = infer_expr_type(builtin->args[1]);
                if (!is_string_like(secret_type)) {
                    throw TypeError("JWT secret must be a string", builtin->location);
                }
                return VarType::STRING;
            }
        }
        if (is_log_config_builtin_name(builtin->name)) {
            if (builtin->name == "log_console" || builtin->name == "log::console") {
                if (!builtin->args.empty()) {
                    throw SemanticError("Builtin 'log_console' expects 0 arguments", builtin->location);
                }
                return VarType::BOOL;
            }
            if (builtin->args.size() != 1) {
                throw SemanticError("Builtin '" + builtin->name + "' expects 1 argument", builtin->location);
            }
            VarType arg_type = infer_expr_type(builtin->args[0]);
            if (arg_type != VarType::STRING && arg_type != VarType::BYTES && arg_type != VarType::UNKNOWN) {
                throw TypeError("Builtin '" + builtin->name + "' expects a string argument", builtin->location);
            }
            return VarType::BOOL;
        }
        if (is_log_builtin_name(builtin->name)) {
            if (builtin->args.empty()) {
                throw SemanticError("Builtin '" + builtin->name + "' expects at least 1 argument", builtin->location);
            }
            if (builtin->name == "log" && builtin->args.size() > 1) {
                VarType first_type = infer_expr_type(builtin->args[0]);
                VarType last_type = infer_expr_type(builtin->args.back());
                bool first_can_be_level = first_type == VarType::STRING ||
                                          first_type == VarType::BYTES ||
                                          first_type == VarType::UNKNOWN;
                bool last_can_be_level = last_type == VarType::STRING ||
                                         last_type == VarType::BYTES ||
                                         last_type == VarType::UNKNOWN;
                if (!first_can_be_level && !last_can_be_level) {
                    throw TypeError("Builtin 'log' level must be a string as first or last argument", builtin->location);
                }
            }
            for (const auto& arg : builtin->args) {
                analyze_expr(arg);
            }
            return VarType::BOOL;
        }
        if (builtin->name == "protocol") {
            if (builtin->args.size() != 1) {
                throw SemanticError("Builtin 'protocol' expects 1 argument", builtin->location);
            }
            VarType arg_type = infer_expr_type(builtin->args[0]);
            if (arg_type != VarType::STRING && arg_type != VarType::UNKNOWN) {
                throw TypeError("Builtin 'protocol' expects a string URL", builtin->location);
            }
            return VarType::STRING;
        }
        if (builtin->name == "json_parse") {
            if (builtin->args.size() != 1) {
                throw SemanticError("Builtin 'json_parse' expects 1 argument", builtin->location);
            }
            VarType arg_type = infer_expr_type(builtin->args[0]);
            if (arg_type != VarType::STRING && arg_type != VarType::UNKNOWN) {
                throw TypeError("Builtin 'json_parse' expects a string", builtin->location);
            }
            return VarType::OBJECT; // or UNKNOWN, but let's say OBJECT
        }
        if (builtin->name == "json_stringify") {
            if (builtin->args.size() != 1) {
                throw SemanticError("Builtin 'json_stringify' expects 1 argument", builtin->location);
            }
            return VarType::STRING;
        }
        if (builtin->name == "read_file") {
            if (builtin->args.size() != 1) {
                throw SemanticError("Builtin 'read_file' expects 1 argument", builtin->location);
            }
            VarType arg_type = infer_expr_type(builtin->args[0]);
            if (arg_type != VarType::STRING && arg_type != VarType::UNKNOWN) {
                throw TypeError("Builtin 'read_file' expects a string path", builtin->location);
            }
            return VarType::STRING;
        }
        if (builtin->name == "open_log") {
            if (builtin->args.size() != 1) {
                throw SemanticError("Builtin 'open_log' expects 1 argument", builtin->location);
            }
            VarType arg_type = infer_expr_type(builtin->args[0]);
            if (arg_type != VarType::STRING && arg_type != VarType::UNKNOWN) {
                throw TypeError("Builtin 'open_log' expects a string path", builtin->location);
            }
            return VarType::BOOL;
        }
        if (builtin->name == "request_method" || builtin->name == "request_path" ||
            builtin->name == "request_body") {
            if (!builtin->args.empty()) {
                throw SemanticError("Builtin '" + builtin->name + "' expects 0 arguments", builtin->location);
            }
            return VarType::STRING;
        }
        if (builtin->name == "request_json") {
            if (!builtin->args.empty()) {
                throw SemanticError("Builtin 'request_json' expects 0 arguments", builtin->location);
            }
            return VarType::OBJECT;
        }
        throw SemanticError("Unknown builtin expression: '" + builtin->name + "'", builtin->location);
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
            if ((left_type == VarType::INT || left_type == VarType::BOOL) &&
                (right_type == VarType::INT || right_type == VarType::BOOL)) {
                return VarType::INT;
            }
            if (left_type == VarType::STRING || right_type == VarType::STRING) {
                return VarType::STRING;  // Конкатенация строк
            }
            if (left_type == VarType::BYTES || right_type == VarType::BYTES) {
                return VarType::BYTES;
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
            for (size_t i = 0; i < func->param_names.size(); ++i) {
                VarType param_type = i < func->param_types.size()
                    ? type_from_name(func->param_types[i])
                    : VarType::UNKNOWN;
                func_info.param_types.push_back(param_type);
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
        VarType param_type = i < func->param_types.size()
            ? type_from_name(func->param_types[i])
            : VarType::UNKNOWN;
        declare_variable(func->param_names[i], param_type, func->location);
    }

    // Первый проход: объявляем все переменные (включая те, что создаются через cout)
    for (const auto& stmt : func->body) {
        if (auto input = std::dynamic_pointer_cast<InputStmt>(stmt)) {
            analyze_input(input);
        } else if (auto decl = std::dynamic_pointer_cast<VarDecl>(stmt)) {
            declare_variable(decl->name, type_from_name(decl->type_name), decl->location);
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
    } else if (auto while_stmt = std::dynamic_pointer_cast<WhileStmt>(stmt)) {
        analyze_while(while_stmt);
    } else if (auto call = std::dynamic_pointer_cast<CallStmt>(stmt)) {
        analyze_call(call);
    } else if (auto ret = std::dynamic_pointer_cast<ReturnStmt>(stmt)) {
        analyze_return(ret);
    } else if (auto builtin = std::dynamic_pointer_cast<BuiltinCallExpr>(stmt)) {
        analyze_expr(builtin);
    } else if (auto net_op = std::dynamic_pointer_cast<NetOp>(stmt)) {
        analyze_net_op(net_op);
    } else if (auto file_op = std::dynamic_pointer_cast<FileOp>(stmt)) {
        analyze_file_op(file_op);
    }
}

void SemanticAnalyzer::analyze_var_decl(const std::shared_ptr<VarDecl>& decl) {
    // Анализируем выражение (переменная уже объявлена в первом проходе)
    // Не выбрасываем ошибку для неопределенных переменных - они будут проверяться во время выполнения
    try {
        VarType expr_type = infer_expr_type(decl->expr);
        VarType declared_type = type_from_name(decl->type_name);
        if (!is_assignable(declared_type, expr_type)) {
            throw TypeError("Cannot assign " + type_to_string(expr_type) +
                            " to variable '" + decl->name + "' of type " +
                            type_to_string(declared_type),
                            decl->location);
        }

        // Обновляем тип переменной, если он был UNKNOWN
        auto* var = find_variable(decl->name);
        if (var && var->type == VarType::UNKNOWN && declared_type != VarType::UNKNOWN) {
            var->type = declared_type;
        } else if (var && var->type == VarType::UNKNOWN && expr_type != VarType::UNKNOWN) {
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
    } else if (input->format == "{bool}") {
        var_type = VarType::BOOL;
    } else if (input->format == "{bytes}") {
        var_type = VarType::BYTES;
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
        if (cond_type != VarType::INT && cond_type != VarType::BOOL && cond_type != VarType::UNKNOWN &&
            cond_type != VarType::FLOAT && cond_type != VarType::STRING && cond_type != VarType::BYTES) {
            throw TypeError("Condition must be a boolean-compatible expression", if_stmt->condition->location);
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

void SemanticAnalyzer::analyze_while(const std::shared_ptr<WhileStmt>& while_stmt) {
    try {
        VarType cond_type = infer_expr_type(while_stmt->condition);
        if (cond_type != VarType::INT && cond_type != VarType::BOOL && cond_type != VarType::UNKNOWN &&
            cond_type != VarType::FLOAT && cond_type != VarType::STRING && cond_type != VarType::BYTES) {
            throw TypeError("While condition must be a boolean-compatible expression", while_stmt->condition->location);
        }
    } catch (const UndefinedError&) {
        // Пропускаем ошибки неопределенных переменных в условиях - они будут проверяться во время выполнения
    }

    enter_scope();
    for (const auto& stmt : while_stmt->body) {
        analyze_statement(stmt);
    }
    exit_scope();
}

void SemanticAnalyzer::analyze_call(const std::shared_ptr<CallStmt>& call) {
    if (is_log_config_builtin_name(call->func_name)) {
        if (call->func_name == "log_console" || call->func_name == "log::console") {
            if (!call->args.empty()) {
                throw SemanticError("Builtin 'log_console' expects 0 arguments", call->location);
            }
            return;
        }
        if (call->args.size() != 1) {
            throw SemanticError("Builtin '" + call->func_name + "' expects 1 argument", call->location);
        }
        VarType arg_type = infer_expr_type(call->args[0]);
        if (arg_type != VarType::STRING && arg_type != VarType::BYTES && arg_type != VarType::UNKNOWN) {
            throw TypeError("Builtin '" + call->func_name + "' expects a string argument", call->location);
        }
        return;
    }

    if (is_log_builtin_name(call->func_name)) {
        if (call->args.empty()) {
            throw SemanticError("Builtin '" + call->func_name + "' expects at least 1 argument", call->location);
        }
        if (call->func_name == "log" && call->args.size() > 1) {
            VarType first_type = infer_expr_type(call->args[0]);
            VarType last_type = infer_expr_type(call->args.back());
            bool first_can_be_level = first_type == VarType::STRING ||
                                      first_type == VarType::BYTES ||
                                      first_type == VarType::UNKNOWN;
            bool last_can_be_level = last_type == VarType::STRING ||
                                     last_type == VarType::BYTES ||
                                     last_type == VarType::UNKNOWN;
            if (!first_can_be_level && !last_can_be_level) {
                throw TypeError("Builtin 'log' level must be a string as first or last argument", call->location);
            }
        }
        for (const auto& arg : call->args) {
            analyze_expr(arg);
        }
        return;
    }

    if (call->func_name == "open_log") {
        if (call->args.size() != 1) {
            throw SemanticError("Builtin 'open_log' expects 1 argument", call->location);
        }
        VarType arg_type = infer_expr_type(call->args[0]);
        if (arg_type != VarType::STRING && arg_type != VarType::BYTES && arg_type != VarType::UNKNOWN) {
            throw TypeError("Builtin 'open_log' expects a string path", call->location);
        }
        return;
    }

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
    for (size_t i = 0; i < call->args.size(); ++i) {
        const auto& arg = call->args[i];
        analyze_expr(arg);
        VarType arg_type = infer_expr_type(arg);
        VarType expected_type = func_info.param_types[i];
        if (!is_assignable(expected_type, arg_type)) {
            throw TypeError("Function '" + call->func_name + "' argument " +
                            std::to_string(i + 1) + " expects " +
                            type_to_string(expected_type) + ", got " +
                            type_to_string(arg_type),
                            call->location);
        }
    }
}

void SemanticAnalyzer::analyze_return(const std::shared_ptr<ReturnStmt>& ret) {
    if (ret->expr) {
        analyze_expr(ret->expr);
    }
}

void SemanticAnalyzer::analyze_net_op(const std::shared_ptr<NetOp>& net_op) {
    if (!net_op->transport.empty() && net_op->transport != "http" && net_op->transport != "https") {
        throw SemanticError("Unsupported network transport: '" + net_op->transport + "'", net_op->location);
    }
    if (net_op->method != "get" && net_op->method != "post" &&
        net_op->method != "serve" && net_op->method != "run" &&
        net_op->method != "route") {
        throw SemanticError("Unsupported network method: '" + net_op->method + "'", net_op->location);
    }
    if ((net_op->method == "serve" || net_op->method == "run") && net_op->transport == "https") {
        throw SemanticError("Local server currently supports HTTP only", net_op->location);
    }

    analyze_expr(net_op->url);
    VarType url_type = infer_expr_type(net_op->url);
    if (url_type != VarType::STRING && url_type != VarType::UNKNOWN) {
        throw TypeError("Network URL must be a string", net_op->location);
    }

    if (net_op->method == "route") {
        if (!net_op->path) {
            throw SemanticError("Network route requires a handler argument", net_op->location);
        }
        analyze_expr(net_op->path);
        VarType path_type = infer_expr_type(net_op->path);
        if (path_type != VarType::STRING && path_type != VarType::UNKNOWN) {
            throw TypeError("Network route argument must be a string", net_op->location);
        }
        if (net_op->data) {
            analyze_expr(net_op->data);
            VarType data_type = infer_expr_type(net_op->data);
            if (data_type != VarType::STRING && data_type != VarType::BYTES && data_type != VarType::UNKNOWN) {
                throw TypeError("Network route handler must be a string", net_op->location);
            }
        }
    } else if (net_op->method == "serve" || net_op->method == "run") {
        if (!net_op->port) {
            throw SemanticError("Network server requires a port argument", net_op->location);
        }
        analyze_expr(net_op->port);
        VarType port_type = infer_expr_type(net_op->port);
        if (port_type != VarType::INT && port_type != VarType::UNKNOWN) {
            throw TypeError("Network server port must be an int", net_op->location);
        }
        if (net_op->data) {
            analyze_expr(net_op->data);
            VarType data_type = infer_expr_type(net_op->data);
            if (data_type != VarType::STRING && data_type != VarType::BYTES && data_type != VarType::UNKNOWN) {
                throw TypeError("Network server response body must be a string or bytes", net_op->location);
            }
        }
    } else if (net_op->method == "get" && net_op->data) {
        analyze_expr(net_op->data);
        VarType data_type = infer_expr_type(net_op->data);
        if (data_type != VarType::STRING && data_type != VarType::BYTES && data_type != VarType::UNKNOWN) {
            throw TypeError("Network GET route handler must be a string or bytes", net_op->location);
        }
    } else if (net_op->method == "post") {
        if (!net_op->data) {
            throw SemanticError("Network POST requires a body argument", net_op->location);
        }
        analyze_expr(net_op->data);
        VarType data_type = infer_expr_type(net_op->data);
        if (data_type != VarType::STRING && data_type != VarType::BYTES && data_type != VarType::UNKNOWN) {
            throw TypeError("Network POST body must be a string or bytes", net_op->location);
        }
    } else if (net_op->data) {
        throw SemanticError("Network GET does not accept a body argument", net_op->location);
    }
}

void SemanticAnalyzer::analyze_file_op(const std::shared_ptr<FileOp>& file_op) {
    // Анализируем путь к файлу (должен быть строкой)
    analyze_expr(file_op->file_path);

    // Для write проверяем данные
    if (file_op->operation == T_WRITE && file_op->data) {
        analyze_expr(file_op->data);
    }

    // Проверяем, что операция валидна
    if (file_op->operation != T_CREATE && file_op->operation != T_WRITE &&
        file_op->operation != T_READ && file_op->operation != T_CLOSE &&
        file_op->operation != T_DELETE) {
        throw SemanticError("Invalid file operation", file_op->location);
    }
}

void SemanticAnalyzer::analyze(const std::vector<std::shared_ptr<AstNode>>& program) {
    analyze_program(program);
}
