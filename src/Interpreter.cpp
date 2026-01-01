#include "Interpreter.h"
#include "Utils.h"
#include <iostream>
#include <limits>

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
    auto func = functions[name];
    if (!func) return;

    // Локальные переменные функции (параметры + переменные внутри функции)
    std::map<std::string, Value> locals;

    // Устанавливаем параметры как локальные переменные
    if (DEBUG) std::cout << "[DEBUG] Function " << name << " has " << func->param_names.size() << " params, got " << call_args.size() << " args" << std::endl;
    for (size_t i = 0; i < func->param_names.size() && i < call_args.size(); ++i) {
        locals[func->param_names[i]] = call_args[i];
        if (DEBUG) std::cout << "[DEBUG] Param " << func->param_names[i] << " = " << call_args[i].to_string() << std::endl;
    }

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
            execute_function(call->func_name, args);
        } else if (auto input = std::dynamic_pointer_cast<InputStmt>(stmt)) {
            Value val;

            // Выводим промпт, если он задан
            if (!input->prompt.empty()) {
                std::cout << input->prompt;
            } else {
                std::cout << "> ";
            }

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
                std::cin.ignore();  // очищаем буфер после предыдущих >>
                std::getline(std::cin, x);
                val = Value(x);
            }

            // Сохраняем в локальные переменные, если нет локальной, то в глобальные
            if (globals.count("input")) {
                globals["input"] = val;
            } else {
                locals["input"] = val;
            }
            if (DEBUG) std::cout << "[DEBUG] Input saved to 'input' = " << val.to_string() << std::endl;
        }
    }
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
        return Value();
    }
    if (auto bin = std::dynamic_pointer_cast<BinaryOp>(node)) {
        Value l = eval(bin->left, locals);
        Value r = eval(bin->right, locals);
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