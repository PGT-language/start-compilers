#include "CodeGen.h"
#include <iostream>
#include <fstream>

void CodeGen::write_indent() {
    for (int i = 0; i < indent_level; ++i) {
        code << "    ";
    }
}

std::string CodeGen::get_temp_var() {
    return "_tmp" + std::to_string(temp_var_counter++);
}

std::string CodeGen::get_c_type(const std::string& pgt_type) {
    if (pgt_type == "int") return "long long";
    if (pgt_type == "float") return "double";
    if (pgt_type == "string") return "char*";
    return "void*";
}

std::string CodeGen::generate_expr(const std::shared_ptr<AstNode>& expr) {
    if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
        if (lit->value.type == ValueType::INT) {
            return std::to_string(lit->value.int_val) + "LL";
        } else if (lit->value.type == ValueType::FLOAT) {
            return std::to_string(lit->value.float_val);
        } else if (lit->value.type == ValueType::STRING) {
            // Экранируем строки для C
            std::string escaped = "\"";
            for (char c : lit->value.str_val) {
                if (c == '"') escaped += "\\\"";
                else if (c == '\n') escaped += "\\n";
                else if (c == '\t') escaped += "\\t";
                else if (c == '\\') escaped += "\\\\";
                else escaped += c;
            }
            escaped += "\"";
            return escaped;
        }
    }
    
    if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
        return id->name;
    }
    
    if (auto bin = std::dynamic_pointer_cast<BinaryOp>(expr)) {
        std::string left = generate_expr(bin->left);
        std::string right = generate_expr(bin->right);
        std::string op;
        
        switch (bin->op) {
            case T_PLUS: op = "+"; break;
            case T_MINUS: op = "-"; break;
            case T_STAR: op = "*"; break;
            case T_SLASH: op = "/"; break;
            case T_GREATER: op = ">"; break;
            case T_LESS: op = "<"; break;
            case T_EQUAL_EQUAL: op = "=="; break;
            case T_NOT_EQUAL: op = "!="; break;
            case T_GREATER_EQUAL: op = ">="; break;
            case T_LESS_EQUAL: op = "<="; break;
            default: op = "+";
        }
        
        return "(" + left + " " + op + " " + right + ")";
    }
    
    return "0";
}

void CodeGen::generate_var_decl(const std::shared_ptr<VarDecl>& decl) {
    write_indent();
    // Пока используем void* для всех переменных, позже можно добавить определение типов
    code << "void* " << decl->name << " = (void*)(" << generate_expr(decl->expr) << ");\n";
    write_indent();
    code << "gc_add_root(&" << decl->name << ");\n";  // Добавляем в корни GC
}

void CodeGen::generate_print(const std::shared_ptr<PrintStmt>& print) {
    write_indent();
    for (size_t i = 0; i < print->args.size(); ++i) {
        std::string expr = generate_expr(print->args[i]);
        std::string format = i < print->formats.size() ? print->formats[i] : "";
        
        if (format == "{int}") {
            code << "printf(\"%lld\", (long long)" << expr << ");\n";
        } else if (format == "{float}") {
            code << "printf(\"%f\", (double)" << expr << ");\n";
        } else if (format == "{string}") {
            code << "printf(\"%s\", (char*)" << expr << ");\n";
        } else {
            code << "printf(\"%s\", (char*)" << expr << ");\n";
        }
        
        if (i < print->args.size() - 1) {
            write_indent();
        }
    }
    
    if (!print->is_printg) {
        write_indent();
        code << "printf(\"\\n\");\n";
    }
}

void CodeGen::generate_input(const std::shared_ptr<InputStmt>& input) {
    write_indent();
    std::string var_name = input->var_name.empty() ? "input" : input->var_name;
    
    if (input->format == "{int}") {
        code << "long long " << var_name << ";\n";
        write_indent();
        code << "scanf(\"%lld\", &" << var_name << ");\n";
    } else if (input->format == "{float}") {
        code << "double " << var_name << ";\n";
        write_indent();
        code << "scanf(\"%lf\", &" << var_name << ");\n";
    } else if (input->format == "{string}") {
        code << "char " << var_name << "[1024];\n";
        write_indent();
        code << "fgets(" << var_name << ", 1024, stdin);\n";
    }
}

void CodeGen::generate_if(const std::shared_ptr<IfStmt>& if_stmt) {
    write_indent();
    code << "if (" << generate_expr(if_stmt->condition) << ") {\n";
    indent_level++;
    
    for (const auto& stmt : if_stmt->then_body) {
        generate_statement(stmt);
    }
    
    indent_level--;
    write_indent();
    code << "}";
    
    if (!if_stmt->else_body.empty()) {
        code << " else {\n";
        indent_level++;
        
        for (const auto& stmt : if_stmt->else_body) {
            generate_statement(stmt);
        }
        
        indent_level--;
        write_indent();
        code << "}";
    }
    
    code << "\n";
}

void CodeGen::generate_file_op(const std::shared_ptr<FileOp>& file_op) {
    write_indent();
    std::string file_path = generate_expr(file_op->file_path);
    
    switch (file_op->operation) {
        case T_CREATE:
            code << "{ FILE* f = fopen(" << file_path << ", \"w\"); if (f) fclose(f); }\n";
            break;
        case T_WRITE:
            if (file_op->data) {
                std::string data = generate_expr(file_op->data);
                code << "{ FILE* f = fopen(" << file_path << ", \"a\"); if (f) { fprintf(f, \"%s\", (char*)" << data << "); fclose(f); } }\n";
            }
            break;
        case T_READ:
            code << "{ FILE* f = fopen(" << file_path << ", \"r\"); if (f) { char buf[4096]; while (fgets(buf, 4096, f)) printf(\"%s\", buf); fclose(f); } }\n";
            break;
        case T_DELETE:
            code << "remove(" << file_path << ");\n";
            break;
        default:
            break;
    }
}

void CodeGen::generate_call(const std::shared_ptr<ConectCall>& call) {
    write_indent();
    std::string func_name = call->func_name;
    if (func_name == "main") {
        func_name = "pgt_main";
    }
    code << func_name << "(";
    for (size_t i = 0; i < call->args.size(); ++i) {
        if (i > 0) code << ", ";
        code << generate_expr(call->args[i]);
    }
    code << ");\n";
}

void CodeGen::generate_statement(const std::shared_ptr<AstNode>& stmt) {
    if (auto decl = std::dynamic_pointer_cast<VarDecl>(stmt)) {
        generate_var_decl(decl);
    } else if (auto print = std::dynamic_pointer_cast<PrintStmt>(stmt)) {
        generate_print(print);
    } else if (auto input = std::dynamic_pointer_cast<InputStmt>(stmt)) {
        generate_input(input);
    } else if (auto if_stmt = std::dynamic_pointer_cast<IfStmt>(stmt)) {
        generate_if(if_stmt);
    } else if (auto call = std::dynamic_pointer_cast<ConectCall>(stmt)) {
        generate_call(call);
    } else if (auto file_op = std::dynamic_pointer_cast<FileOp>(stmt)) {
        generate_file_op(file_op);
    }
}

void CodeGen::generate_function(const std::shared_ptr<FunctionDef>& func) {
    std::string func_name = func->name;
    if (func_name == "main") {
        func_name = "pgt_main";
    }

    // Генерируем сигнатуру функции
    code << "void " << func_name << "(";
    for (size_t i = 0; i < func->param_names.size(); ++i) {
        if (i > 0) code << ", ";
        code << "void* " << func->param_names[i];
    }
    code << ") {\n";
    
    indent_level++;
    
    // Генерируем тело функции
    for (const auto& stmt : func->body) {
        generate_statement(stmt);
    }
    
    indent_level--;
    code << "}\n\n";
}

std::string CodeGen::generate(const std::vector<std::shared_ptr<AstNode>>& program) {
    code.str("");  // Очищаем буфер
    
    // Генерируем заголовок с включениями
    code << "// Generated by PGT Compiler\n";
    code << "#include <stdio.h>\n";
    code << "#include <stdlib.h>\n";
    code << "#include <string.h>\n";
    code << "#include \"runtime/gc.h\"\n\n";
    
    // Первый проход: объявляем все функции (forward declarations)
    for (const auto& node : program) {
        if (auto func = std::dynamic_pointer_cast<FunctionDef>(node)) {
            std::string func_name = func->name;
            if (func_name == "main") {
                func_name = "pgt_main";
            }
            code << "void " << func_name << "(";
            for (size_t i = 0; i < func->param_names.size(); ++i) {
                if (i > 0) code << ", ";
                code << "void* " << func->param_names[i];
            }
            code << ");\n";
        }
    }
    code << "\n";
    
    // Второй проход: генерируем реализации функций
    for (const auto& node : program) {
        if (auto func = std::dynamic_pointer_cast<FunctionDef>(node)) {
            generate_function(func);
        } else if (auto var = std::dynamic_pointer_cast<VarDecl>(node)) {
            // Глобальные переменные
            code << "void* " << var->name << " = NULL;\n";
        }
    }
    
    // Генерируем main функцию
    code << "int main(int argc, char** argv) {\n";
    code << "    gc_init();\n";
    
    // Ищем и выполняем conect(main)
    for (const auto& node : program) {
        if (auto call = std::dynamic_pointer_cast<ConectCall>(node)) {
            if (call->func_name == "main") {
                code << "    pgt_main();\n";
            }
        }
    }
    
    code << "    gc_cleanup();\n";
    code << "    return 0;\n";
    code << "}\n";
    
    return code.str();
}

void CodeGen::save_to_file(const std::string& filename) {
    std::ofstream out(filename);
    if (!out) {
        throw std::runtime_error("Failed to open file for writing: " + filename);
    }
    out << code.str();
    out.close();
}