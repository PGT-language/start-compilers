#include "Interpreter.h"
#include "Utils.h"
#include "Error.h"
#include <iostream>
#include <limits>
#include <fstream>
#include <filesystem>

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
        for (const auto& stmt : func->body) {
            if (auto decl = std::dynamic_pointer_cast<VarDecl>(stmt)) {
            Value val = eval(decl->expr, locals);
            // Если переменная уже существует глобально, изменяем глобальную
            // Иначе создаем локальную переменную
            if (globals.count(decl->name)) {
                globals[decl->name] = val;
                if (DEBUG) std::cout << "[DEBUG] Set global var " << decl->name << " = " << val.to_string() << std::endl;
            } else {
                locals[decl->name] = val;
                if (DEBUG) std::cout << "[DEBUG] Set local var " << decl->name << " = " << val.to_string() << std::endl;
            }
            } else if (auto print = std::dynamic_pointer_cast<PrintStmt>(stmt)) {
            size_t fmt_idx = 0;
            for (const auto& arg : print->args) {
                Value v = eval(arg, locals);
                std::string fmt = fmt_idx < print->formats.size() ? print->formats[fmt_idx++] : "";
                std::string output = v.to_string(fmt);
                std::cout << output;
            }
            // printg не добавляет перенос строки в конце, но добавляем его для корректного отображения
            if (!print->is_printg) {
                std::cout << std::endl;
            } else {
                // Для printg тоже добавляем перенос строки, чтобы курсор был на новой строке
                std::cout << std::endl;
            }
            } else if (auto call = std::dynamic_pointer_cast<ConectCall>(stmt)) {
            std::vector<Value> args;
            for (const auto& a : call->args) args.push_back(eval(a, locals));
            if (DEBUG) std::cout << "[DEBUG] Calling " << call->func_name << " with " << args.size() << " args" << std::endl;
            try {
                execute_function(call->func_name, args);
            } catch (CompilerError& e) {
                e.traceback.push_back(call->location);
                throw;
            }
            } else if (auto if_stmt = std::dynamic_pointer_cast<IfStmt>(stmt)) {
            Value cond_val = eval(if_stmt->condition, locals);
            bool condition_true = false;
            
            // Проверяем условие
            if (cond_val.type == ValueType::INT) {
                condition_true = (cond_val.int_val != 0);
            } else if (cond_val.type == ValueType::FLOAT) {
                condition_true = (cond_val.float_val != 0.0);
            } else if (cond_val.type == ValueType::STRING) {
                condition_true = !cond_val.str_val.empty();
            }
            
            // Выполняем соответствующее тело
            const auto& body_to_execute = condition_true ? if_stmt->then_body : if_stmt->else_body;
            for (const auto& s : body_to_execute) {
                // Рекурсивно выполняем операторы из тела if/else
                if (auto decl = std::dynamic_pointer_cast<VarDecl>(s)) {
                    Value val = eval(decl->expr, locals);
                    if (globals.count(decl->name)) {
                        globals[decl->name] = val;
                    } else {
                        locals[decl->name] = val;
                    }
                } else if (auto print = std::dynamic_pointer_cast<PrintStmt>(s)) {
                    size_t fmt_idx = 0;
                    for (const auto& arg : print->args) {
                        Value v = eval(arg, locals);
                        std::string fmt = fmt_idx < print->formats.size() ? print->formats[fmt_idx++] : "";
                        std::cout << v.to_string(fmt);
                    }
                    if (!print->is_printg) {
                        std::cout << std::endl;
                    } else {
                        std::cout << std::endl;
                    }
                } else if (auto call = std::dynamic_pointer_cast<ConectCall>(s)) {
                    std::vector<Value> args;
                    for (const auto& a : call->args) args.push_back(eval(a, locals));
                    try {
                        execute_function(call->func_name, args);
                    } catch (CompilerError& e) {
                        e.traceback.push_back(call->location);
                        throw;
                    }
                } else if (auto input = std::dynamic_pointer_cast<InputStmt>(s)) {
                    Value val;
                    if (!input->prompt.empty()) {
                        std::cout << input->prompt;
                    } else {
                        std::cout << "> ";
                    }
                    std::cout.flush();
                    if (input->format == "{int}") {
                        long long x;
                        if (std::cin.peek() == '\n') std::cin.ignore();
                        std::cin >> x;
                        val = Value(x);
                    } else if (input->format == "{float}") {
                        double x;
                        if (std::cin.peek() == '\n') std::cin.ignore();
                        std::cin >> x;
                        val = Value(x);
                    } else if (input->format == "{string}") {
                        std::string x;
                        if (std::cin.peek() == '\n') std::cin.ignore();
                        std::getline(std::cin, x);
                        val = Value(x);
                    }
                    std::string var_name = input->var_name.empty() ? "input" : input->var_name;
                    if (globals.count(var_name)) {
                        globals[var_name] = val;
                    } else {
                        locals[var_name] = val;
                    }
                } else if (auto nested_if = std::dynamic_pointer_cast<IfStmt>(s)) {
                    // Рекурсивно обрабатываем вложенные if
                    Value nested_cond = eval(nested_if->condition, locals);
                    bool nested_true = false;
                    if (nested_cond.type == ValueType::INT) {
                        nested_true = (nested_cond.int_val != 0);
                    } else if (nested_cond.type == ValueType::FLOAT) {
                        nested_true = (nested_cond.float_val != 0.0);
                    } else if (nested_cond.type == ValueType::STRING) {
                        nested_true = !nested_cond.str_val.empty();
                    }
                    const auto& nested_body = nested_true ? nested_if->then_body : nested_if->else_body;
                    for (const auto& ns : nested_body) {
                        // Рекурсивно выполняем вложенные операторы
                        if (auto decl = std::dynamic_pointer_cast<VarDecl>(ns)) {
                            Value val = eval(decl->expr, locals);
                            if (globals.count(decl->name)) {
                                globals[decl->name] = val;
                            } else {
                                locals[decl->name] = val;
                            }
                        } else if (auto print = std::dynamic_pointer_cast<PrintStmt>(ns)) {
                            size_t fmt_idx = 0;
                            for (const auto& arg : print->args) {
                                Value v = eval(arg, locals);
                                std::string fmt = fmt_idx < print->formats.size() ? print->formats[fmt_idx++] : "";
                                std::cout << v.to_string(fmt);
                            }
                            if (!print->is_printg) {
                                std::cout << std::endl;
                            } else {
                                std::cout << std::endl;
                            }
                        } else if (auto call = std::dynamic_pointer_cast<ConectCall>(ns)) {
                            std::vector<Value> args;
                            for (const auto& a : call->args) args.push_back(eval(a, locals));
                            try {
                                execute_function(call->func_name, args);
                            } catch (CompilerError& e) {
                                e.traceback.push_back(call->location);
                                throw;
                            }
                        } else if (auto file_op = std::dynamic_pointer_cast<FileOp>(ns)) {
                            // Обработка файловых операций в вложенных if
                            Value file_path_val = eval(file_op->file_path, locals);
                            if (file_path_val.type != ValueType::STRING) {
                                throw TypeError("File path must be a string", file_op->location);
                            }
                            std::string file_path = file_path_val.str_val;
                            
                            // Простая обработка (полная реализация уже есть выше)
                            if (file_op->operation == T_CREATE) {
                                std::ofstream file(file_path, std::ios::out);
                                if (!file.is_open()) {
                                    throw RuntimeError("Failed to create file: " + file_path, file_op->location);
                                }
                                file.close();
                            } else if (file_op->operation == T_WRITE && file_op->data) {
                                Value data_val = eval(file_op->data, locals);
                                std::ofstream file(file_path, std::ios::out | std::ios::app);
                                if (!file.is_open()) {
                                    throw RuntimeError("Failed to open file for writing: " + file_path, file_op->location);
                                }
                                file << data_val.to_string();
                                file.close();
                            } else if (file_op->operation == T_READ) {
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
                                std::cout << content << std::endl;
                            } else if (file_op->operation == T_DELETE) {
                                if (std::filesystem::exists(file_path)) {
                                    if (open_files.count(file_path)) {
                                        open_files[file_path]->close();
                                        open_files.erase(file_path);
                                    }
                                    std::filesystem::remove(file_path);
                                } else {
                                    throw RuntimeError("File does not exist: " + file_path, file_op->location);
                                }
                            }
                        }
                    }
                }
            }
            } else if (auto input = std::dynamic_pointer_cast<InputStmt>(stmt)) {
            Value val;

            // Выводим промпт, если он задан
            if (!input->prompt.empty()) {
                std::cout << input->prompt;
            } else {
                std::cout << "> ";
            }
            std::cout.flush();  // Сбрасываем буфер, чтобы промпт сразу отобразился

            if (input->format == "{int}") {
                long long x;
                if (std::cin >> x) {
                    val = Value(x);
                } else {
                    val = Value(0LL);  // если ошибка ввода
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
                // Очищаем буфер только если есть данные
                if (std::cin.peek() == '\n') {
                    std::cin.ignore();  // пропускаем оставшийся перенос строки
                }
                std::getline(std::cin, x);
                val = Value(x);
            }

            // Сохраняем в указанную переменную (локальную или глобальную)
            std::string var_name = input->var_name.empty() ? "input" : input->var_name;
            if (globals.count(var_name)) {
                globals[var_name] = val;
                if (DEBUG) std::cout << "[DEBUG] Input saved to global '" << var_name << "' = " << val.to_string() << std::endl;
            } else {
                locals[var_name] = val;
                if (DEBUG) std::cout << "[DEBUG] Input saved to local '" << var_name << "' = " << val.to_string() << std::endl;
            }
            } else if (auto file_op = std::dynamic_pointer_cast<FileOp>(stmt)) {
            // Обработка файловых операций
            Value file_path_val = eval(file_op->file_path, locals);
            if (file_path_val.type != ValueType::STRING) {
                throw TypeError("File path must be a string", file_op->location);
            }
            std::string file_path = file_path_val.str_val;
            
            switch (file_op->operation) {
                case T_CREATE: {
                    // Создание файла
                    std::ofstream file(file_path, std::ios::out);
                    if (!file.is_open()) {
                        throw RuntimeError("Failed to create file: " + file_path, file_op->location);
                    }
                    file.close();
                    if (DEBUG) std::cout << "[DEBUG] Created file: " << file_path << std::endl;
                    break;
                }
                case T_WRITE: {
                    // Запись в файл
                    if (!file_op->data) {
                        throw SemanticError("Write operation requires data argument", file_op->location);
                    }
                    Value data_val = eval(file_op->data, locals);
                    std::string data_str = data_val.to_string();
                    
                    std::ofstream file(file_path, std::ios::out | std::ios::app);
                    if (!file.is_open()) {
                        throw RuntimeError("Failed to open file for writing: " + file_path, file_op->location);
                    }
                    file << data_str;
                    file.close();
                    if (DEBUG) std::cout << "[DEBUG] Wrote to file: " << file_path << std::endl;
                    break;
                }
                case T_READ: {
                    // Чтение из файла
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
                    // Сохраняем результат в переменную (можно расширить для сохранения в переменную)
                    if (DEBUG) std::cout << "[DEBUG] Read from file: " << file_path << " (" << content.size() << " bytes)" << std::endl;
                    // Пока просто выводим содержимое (можно расширить для сохранения в переменную)
                    std::cout << content << std::endl;
                    break;
                }
                case T_CLOSE: {
                    // Закрытие файла (если он был открыт через другой механизм)
                    if (open_files.count(file_path)) {
                        open_files[file_path]->close();
                        open_files.erase(file_path);
                        if (DEBUG) std::cout << "[DEBUG] Closed file: " << file_path << std::endl;
                    }
                    break;
                }
                case T_DELETE: {
                    // Удаление файла
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
    } catch (CompilerError& e) {
        // Добавляем traceback
        e.traceback = call_stack;
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