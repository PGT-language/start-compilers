#include "Interpreter.h"
#include "Utils.h"
#include "Error.h"
#include <iostream>
#include <limits>
#include <fstream>
#include <filesystem>

bool Interpreter::is_truthy(const Value& value) const {
    if (value.type == ValueType::INT) {
        return value.int_val != 0;
    }
    if (value.type == ValueType::FLOAT) {
        return value.float_val != 0.0;
    }
    if (value.type == ValueType::STRING) {
        return !value.str_val.empty();
    }
    return false;
}

void Interpreter::assign_value(const std::string& name, const Value& value, std::map<std::string, Value>& locals) {
    if (globals.count(name)) {
        globals[name] = value;
    } else {
        locals[name] = value;
    }
}

void Interpreter::execute_block(const std::vector<std::shared_ptr<AstNode>>& body, std::map<std::string, Value>& locals) {
    for (const auto& stmt : body) {
        execute_statement(stmt, locals);
    }
}

void Interpreter::execute_statement(const std::shared_ptr<AstNode>& stmt, std::map<std::string, Value>& locals) {
    if (auto decl = std::dynamic_pointer_cast<VarDecl>(stmt)) {
        Value val = eval(decl->expr, locals);
        assign_value(decl->name, val, locals);
        if (DEBUG) {
            std::cout << "[DEBUG] Set " << (globals.count(decl->name) ? "global" : "local")
                      << " var " << decl->name << " = " << val.to_string() << std::endl;
        }
        return;
    }

    if (auto print = std::dynamic_pointer_cast<PrintStmt>(stmt)) {
        size_t fmt_idx = 0;
        for (const auto& arg : print->args) {
            Value v = eval(arg, locals);
            std::string fmt = fmt_idx < print->formats.size() ? print->formats[fmt_idx++] : "";
            std::cout << v.to_string(fmt);
        }
        if (!print->is_printg) {
            std::cout << std::endl;
        }
        return;
    }

    if (auto call = std::dynamic_pointer_cast<CallStmt>(stmt)) {
        std::vector<Value> args;
        for (const auto& arg : call->args) {
            args.push_back(eval(arg, locals));
        }
        if (DEBUG) {
            std::cout << "[DEBUG] Calling " << call->func_name << " with " << args.size() << " args" << std::endl;
        }
        try {
            execute_function(call->func_name, args);
        } catch (CompilerError& e) {
            e.traceback.push_back(call->location);
            throw;
        }
        return;
    }

    if (auto if_stmt = std::dynamic_pointer_cast<IfStmt>(stmt)) {
        const auto& body_to_execute = is_truthy(eval(if_stmt->condition, locals))
            ? if_stmt->then_body
            : if_stmt->else_body;
        execute_block(body_to_execute, locals);
        return;
    }

    if (auto while_stmt = std::dynamic_pointer_cast<WhileStmt>(stmt)) {
        while (is_truthy(eval(while_stmt->condition, locals))) {
            execute_block(while_stmt->body, locals);
        }
        return;
    }

    if (auto input = std::dynamic_pointer_cast<InputStmt>(stmt)) {
        Value val;

        if (!input->prompt.empty()) {
            std::cout << input->prompt;
        } else {
            std::cout << "> ";
        }
        std::cout.flush();

        if (input->format == "{int}") {
            long long x;
            if (std::cin >> x) {
                val = Value(x);
            } else {
                val = Value(0LL);
                std::cin.clear();
                std::cin.ignore(10000, '\n');
            }
        } else if (input->format == "{float}") {
            double x;
            if (std::cin >> x) {
                val = Value(x);
            } else {
                val = Value(0.0);
                std::cin.clear();
                std::cin.ignore(10000, '\n');
            }
        } else if (input->format == "{string}") {
            std::string x;
            if (std::cin.peek() == '\n') {
                std::cin.ignore();
            }
            std::getline(std::cin, x);
            val = Value(x);
        }

        std::string var_name = input->var_name.empty() ? "input" : input->var_name;
        assign_value(var_name, val, locals);
        if (DEBUG) {
            std::cout << "[DEBUG] Input saved to " << (globals.count(var_name) ? "global" : "local")
                      << " '" << var_name << "' = " << val.to_string() << std::endl;
        }
        return;
    }

    if (auto file_op = std::dynamic_pointer_cast<FileOp>(stmt)) {
        Value file_path_val = eval(file_op->file_path, locals);
        if (file_path_val.type != ValueType::STRING) {
            throw TypeError("File path must be a string", file_op->location);
        }
        std::string file_path = file_path_val.str_val;

        switch (file_op->operation) {
            case T_CREATE: {
                std::ofstream file(file_path, std::ios::out);
                if (!file.is_open()) {
                    throw RuntimeError("Failed to create file: " + file_path, file_op->location);
                }
                file.close();
                if (DEBUG) std::cout << "[DEBUG] Created file: " << file_path << std::endl;
                break;
            }
            case T_WRITE: {
                if (!file_op->data) {
                    throw SemanticError("Write operation requires data argument", file_op->location);
                }
                Value data_val = eval(file_op->data, locals);
                std::ofstream file(file_path, std::ios::out | std::ios::app);
                if (!file.is_open()) {
                    throw RuntimeError("Failed to open file for writing: " + file_path, file_op->location);
                }
                file << data_val.to_string();
                file.close();
                if (DEBUG) std::cout << "[DEBUG] Wrote to file: " << file_path << std::endl;
                break;
            }
            case T_READ: {
                std::ifstream file(file_path);
                if (!file.is_open()) {
                    throw RuntimeError("Failed to open file for reading: " + file_path, file_op->location);
                }
                std::string content;
                std::string line;
                while (std::getline(file, line)) {
                    if (!content.empty()) content += "\n";
                    content += line;
                }
                file.close();
                if (DEBUG) {
                    std::cout << "[DEBUG] Read from file: " << file_path << " (" << content.size() << " bytes)" << std::endl;
                }
                std::cout << content << std::endl;
                break;
            }
            case T_CLOSE: {
                if (open_files.count(file_path)) {
                    open_files[file_path]->close();
                    open_files.erase(file_path);
                    if (DEBUG) std::cout << "[DEBUG] Closed file: " << file_path << std::endl;
                }
                break;
            }
            case T_DELETE: {
                if (std::filesystem::exists(file_path)) {
                    if (open_files.count(file_path)) {
                        open_files[file_path]->close();
                        open_files.erase(file_path);
                    }
                    std::filesystem::remove(file_path);
                    if (DEBUG) std::cout << "[DEBUG] Deleted file: " << file_path << std::endl;
                } else {
                    throw RuntimeError("File does not exist: " + file_path, file_op->location);
                }
                break;
            }
            default:
                throw SemanticError("Unknown file operation", file_op->location);
        }
    }
}

void Interpreter::run(const std::vector<std::shared_ptr<AstNode>>& program) {
    // Сначала обрабатываем все функции и глобальные переменные
    for (const auto& node : program) {
        if (auto f = std::dynamic_pointer_cast<FunctionDef>(node)) {
            functions[f->name] = f;
        } else if (auto var = std::dynamic_pointer_cast<VarDecl>(node)) {
            // Обрабатываем глобальные переменные
            Value val = eval(var->expr);
            globals[var->name] = val;
            if (DEBUG) std::cout << "[DEBUG] Global var " << var->name << " = " << val.to_string() << std::endl;
        }
    }

    if (functions.count("main")) {
        execute_function("main", {});
    }
}

void Interpreter::execute_function(const std::string& name, const std::vector<Value>& call_args) {
    if (!functions.count(name)) {
        throw UndefinedError(name, "function", SourceLocation());
    }
    auto func = functions[name];
    
    // Добавляем текущую функцию в стек вызовов
    call_stack.push_back(func->location);

    // Локальные переменные функции (параметры + переменные внутри функции)
    std::map<std::string, Value> locals;

    // Устанавливаем параметры как локальные переменные
    if (DEBUG) std::cout << "[DEBUG] Function " << name << " has " << func->param_names.size() << " params, got " << call_args.size() << " args" << std::endl;
    for (size_t i = 0; i < func->param_names.size() && i < call_args.size(); ++i) {
        locals[func->param_names[i]] = call_args[i];
        if (DEBUG) std::cout << "[DEBUG] Param " << func->param_names[i] << " = " << call_args[i].to_string() << std::endl;
    }

    try {
        execute_block(func->body, locals);
    } catch (CompilerError& e) {
        if (e.traceback.empty()) {
            e.traceback = call_stack;
        }
        call_stack.pop_back();
        throw;
    }
    // Удаляем функцию из стека вызовов
    call_stack.pop_back();
}

Value Interpreter::eval(const std::shared_ptr<AstNode>& node, const std::map<std::string, Value>& locals) {
    if (!node) return Value();
    if (auto lit = std::dynamic_pointer_cast<Literal>(node)) return lit->value;
    if (auto id = std::dynamic_pointer_cast<Identifier>(node)) {
        // Сначала ищем в локальных переменных, потом в глобальных
        if (locals.count(id->name)) {
            return locals.at(id->name);
        }
        if (globals.count(id->name)) {
            return globals[id->name];
        }
        // Если переменная не найдена, выбрасываем ошибку с traceback
        UndefinedError err(id->name, "variable", id->location);
        err.traceback = call_stack;
        throw err;
    }
    if (auto bin = std::dynamic_pointer_cast<BinaryOp>(node)) {
        Value l = eval(bin->left, locals);
        Value r = eval(bin->right, locals);
        
        // Операторы сравнения
        if (bin->op == T_GREATER || bin->op == T_LESS || bin->op == T_GREATER_EQUAL || 
            bin->op == T_LESS_EQUAL || bin->op == T_EQUAL_EQUAL || bin->op == T_NOT_EQUAL) {
            bool result = false;
            
            // Сравнение строк
            if (l.type == ValueType::STRING && r.type == ValueType::STRING) {
                int cmp = l.str_val.compare(r.str_val);
                switch (bin->op) {
                    case T_GREATER: result = (cmp > 0); break;
                    case T_LESS: result = (cmp < 0); break;
                    case T_GREATER_EQUAL: result = (cmp >= 0); break;
                    case T_LESS_EQUAL: result = (cmp <= 0); break;
                    case T_EQUAL_EQUAL: result = (cmp == 0); break;
                    case T_NOT_EQUAL: result = (cmp != 0); break;
                }
            } else {
                // Сравнение чисел или смешанных типов
                // Если один из операндов - строка, а другой - нет, конвертируем в строки
                if (l.type == ValueType::STRING || r.type == ValueType::STRING || 
                    l.type == ValueType::NONE || r.type == ValueType::NONE) {
                    std::string l_str = (l.type == ValueType::STRING) ? l.str_val : l.to_string();
                    std::string r_str = (r.type == ValueType::STRING) ? r.str_val : r.to_string();
                    if (DEBUG) std::cout << "[DEBUG] Comparing: '" << l_str << "' " << (bin->op == T_GREATER ? ">" : bin->op == T_LESS ? "<" : bin->op == T_EQUAL_EQUAL ? "==" : "?") << " '" << r_str << "'" << std::endl;
                    int cmp = l_str.compare(r_str);
                    switch (bin->op) {
                        case T_GREATER: result = (cmp > 0); break;
                        case T_LESS: result = (cmp < 0); break;
                        case T_GREATER_EQUAL: result = (cmp >= 0); break;
                        case T_LESS_EQUAL: result = (cmp <= 0); break;
                        case T_EQUAL_EQUAL: result = (cmp == 0); break;
                        case T_NOT_EQUAL: result = (cmp != 0); break;
                    }
                    if (DEBUG) std::cout << "[DEBUG] Comparison result: " << (result ? "true" : "false") << std::endl;
                } else {
                    // Сравнение чисел
                    double lv, rv;
                    if (l.type == ValueType::FLOAT) lv = l.float_val;
                    else if (l.type == ValueType::INT) lv = l.int_val;
                    else lv = 0.0;
                    
                    if (r.type == ValueType::FLOAT) rv = r.float_val;
                    else if (r.type == ValueType::INT) rv = r.int_val;
                    else rv = 0.0;
                
                    switch (bin->op) {
                        case T_GREATER: result = (lv > rv); break;
                        case T_LESS: result = (lv < rv); break;
                        case T_GREATER_EQUAL: result = (lv >= rv); break;
                        case T_LESS_EQUAL: result = (lv <= rv); break;
                        case T_EQUAL_EQUAL: result = (lv == rv); break;
                        case T_NOT_EQUAL: result = (lv != rv); break;
                    }
                }
            }
            return Value(result ? 1LL : 0LL);
        }
        
        // Арифметические операции
        if (l.type == ValueType::FLOAT || r.type == ValueType::FLOAT) {
            double lv = (l.type == ValueType::FLOAT) ? l.float_val : l.int_val;
            double rv = (r.type == ValueType::FLOAT) ? r.float_val : r.int_val;
            switch (bin->op) {
                case T_PLUS: return Value(lv + rv);
                case T_MINUS: return Value(lv - rv);
                case T_STAR: return Value(lv * rv);
                case T_SLASH: return Value(rv != 0.0 ? lv / rv : 0.0);
            }
        }
        if (l.type == ValueType::INT && r.type == ValueType::INT) {
            switch (bin->op) {
                case T_PLUS: return Value(l.int_val + r.int_val);
                case T_MINUS: return Value(l.int_val - r.int_val);
                case T_STAR: return Value(l.int_val * r.int_val);
                case T_SLASH: return Value(r.int_val != 0 ? l.int_val / r.int_val : 0LL);
            }
        }
        return Value();
    }
    return Value();
}
