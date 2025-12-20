#include "Interpreter.h"
#include "Utils.h"
#include <iostream>
#include <limits>

void Interpreter::run(const std::vector<std::shared_ptr<AstNode>>& program) {
    for (const auto& node : program) {
        if (auto f = std::dynamic_pointer_cast<FunctionDef>(node)) {
            functions[f->name] = f;
        }
    }

    if (functions.count("main")) {
        execute_function("main", {});
    }
}

void Interpreter::execute_function(const std::string& name, const std::vector<Value>& call_args) {
    auto func = functions[name];
    if (!func) return;

    auto saved = globals;

    for (size_t i = 0; i < func->param_names.size() && i < call_args.size(); ++i) {
        globals[func->param_names[i]] = call_args[i];
        if (DEBUG) std::cout << "[DEBUG] Param " << func->param_names[i] << " = " << call_args[i].to_string() << std::endl;
    }

    for (const auto& stmt : func->body) {
        if (auto decl = std::dynamic_pointer_cast<VarDecl>(stmt)) {
            Value val = eval(decl->expr);
            globals[decl->name] = val;
            if (DEBUG) std::cout << "[DEBUG] Set var " << decl->name << " = " << val.to_string() << std::endl;
        } else if (auto print = std::dynamic_pointer_cast<PrintStmt>(stmt)) {
            size_t fmt_idx = 0;
            for (const auto& arg : print->args) {
                Value v = eval(arg);
                std::string fmt = fmt_idx < print->formats.size() ? print->formats[fmt_idx++] : "";
                std::cout << v.to_string(fmt);
            }
            std::cout << std::endl;
        } else if (auto call = std::dynamic_pointer_cast<ConectCall>(stmt)) {
            std::vector<Value> args;
            for (const auto& a : call->args) args.push_back(eval(a));
            if (DEBUG) std::cout << "[DEBUG] Calling " << call->func_name << " with " << args.size() << " args" << std::endl;
            execute_function(call->func_name, args);
        } else if (auto input = std::dynamic_pointer_cast<InputStmt>(stmt)) {
            Value val;

            if (input->format == "{int}") {
                std::cout << "> ";  // подсказка ввода
                long long x;
                if (std::cin >> x) {
                    val = Value(x);
                } else {
                    val = Value(0LL);  // если ошибка ввода
                    std::cin.clear();
                }
            } else if (input->format == "{float}") {
                std::cout << "> ";
                double x;
                if (std::cin >> x) {
                    val = Value(x);
                } else {
                    val = Value(0.0);
                    std::cin.clear();
                }
            } else if (input->format == "{string}") {
                std::cout << "> ";
                std::string x;
                std::cin.ignore();  // очищаем буфер после предыдущих >>
                std::getline(std::cin, x);
                val = Value(x);
            }

            globals["input"] = val;
            if (DEBUG) std::cout << "[DEBUG] Input saved to 'input' = " << val.to_string() << std::endl;
        }
    }

    globals = saved;
}

Value Interpreter::eval(const std::shared_ptr<AstNode>& node) {
    if (!node) return Value();
    if (auto lit = std::dynamic_pointer_cast<Literal>(node)) return lit->value;
    if (auto id = std::dynamic_pointer_cast<Identifier>(node)) {
        if (id->name == "input" && globals.count("input")) return globals["input"];
        if (globals.count(id->name)) return globals[id->name];
        return Value();
    }
    if (auto bin = std::dynamic_pointer_cast<BinaryOp>(node)) {
        Value l = eval(bin->left);
        Value r = eval(bin->right);
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