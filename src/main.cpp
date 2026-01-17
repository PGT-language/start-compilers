#include "Lexer.h"
#include "Parser.h"
#include "Interpreter.h"
#include "Utils.h"
#include "Ast.h"
#include "SemanticAnalyzer.h"
#include "Error.h"
#include "CodeGen.h"
#include "GarbageCollector.h"
#include "Global.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cctype>
#include <set>
#include <map>

bool DEBUG = false;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Program Generate Time (PGT) Compiler v0.1\n";
        std::cout << "Usage:\n";
        std::cout << "  pgt help                — Show this help\n";
        std::cout << "  pgt version             — Show version\n";
        std::cout << "  pgt run <file.pgt>      — Run PGT program\n";
        std::cout << "  pgt run <file.pgt> --debug — Run with debug output\n";
        std::cout << "  pgt build <file.pgt>     — Build PGT program\n";
        return 0;
    }

    std::string command = argv[1];

    if (command == "help" || command == "--help" || command == "-h") {
        std::cout << "Program Generate Time (PGT) Compiler v0.1\n";
        std::cout << "Commands:\n";
        std::cout << "  help                    — Show this help message\n";
        std::cout << "  version                 — Show compiler version\n";
        std::cout << "  run <file.pgt>          — Execute .pgt file\n";
        std::cout << "  run <file.pgt> --debug  — Execute with debug info\n";
        std::cout << "  build <file.pgt>        — Compile to binary executable\n";
        std::cout << "  build <file.pgt> -o <output> — Compile with custom output name\n\n";
        std::cout << "  history                 — Show history of commands\n";
        std::cout << "Example:\n";
        std::cout << "  ./pgt run test.pgt\n";
        std::cout << "  ./pgt build test.pgt -o program\n";
        return 0;
    }

    if (command == "version" || command == "--version" || command == "-v") {
        std::cout << "PGT Compiler v0.1\n";
        std::cout << "Built on December 20, 2025\n";
        std::cout << "Author: pabla\n";
        return 0;
    }

    if (command == "run") {
        if (argc < 3) {
            std::cerr << "Error: No input file specified.\n";
            std::cerr << "Usage: pgt run <file.pgt> [--debug]\n";
            return 1;
        }

        std::string filename = argv[2];

        // Проверка --debug
        if (argc == 4 && std::string(argv[3]) == "--debug") {
            DEBUG = true;
        } else if (argc > 3) {
            std::cerr << "Unknown argument: " << argv[3] << "\n";
            std::cerr << "Use 'pgt help' for usage.\n";
            return 1;
        }

        // Система загрузки мультифайлов
        std::set<std::string> loaded_files;
        std::map<std::string, std::vector<std::shared_ptr<AstNode>>> file_asts;
        std::vector<std::string> files_to_load = {filename};

        // Функция для получения директории файла
        auto get_directory = [](const std::string& filepath) -> std::string {
            size_t last_slash = filepath.find_last_of("/\\");
            if (last_slash == std::string::npos) return ".";
            return filepath.substr(0, last_slash);
        };

        // Функция для разрешения пути к импортируемому файлу
        auto resolve_import_path = [&](const std::string& base_dir, const std::string& import_path) -> std::string {
            // Если путь уже абсолютный или начинается с ./, используем как есть
            if (import_path[0] == '/' || (import_path.size() > 1 && import_path[0] == '.' && import_path[1] == '/')) {
                return import_path;
            }
            
            // Если путь не заканчивается на .pgt, добавляем
            std::string full_path = import_path;
            if (full_path.size() < 4 || full_path.substr(full_path.size() - 4) != ".pgt") {
                full_path += ".pgt";
            }
            
            // Пробуем относительно директории базового файла
            std::string relative_path = base_dir + "/" + full_path;
            std::ifstream test(relative_path);
            if (test) {
                test.close();
                return relative_path;
            }
            
            // Пробуем относительно текущей директории
            test.open(full_path);
            if (test) {
                test.close();
                return full_path;
            }
            
            return full_path; // Возвращаем даже если не нашли (ошибка будет позже)
        };

        // Загружаем все файлы рекурсивно
        while (!files_to_load.empty()) {
            std::string current_file = files_to_load.back();
            files_to_load.pop_back();

            // Пропускаем уже загруженные файлы
            if (loaded_files.count(current_file)) {
                continue;
            }

            std::ifstream f(current_file);
            if (!f) {
                std::cerr << "Error: Cannot open file '" << current_file << "'\n";
                continue;
            }

            std::string source((std::istreambuf_iterator<char>(f)), {});
            f.close();

            if (DEBUG) std::cout << "[DEBUG] Loading file: " << current_file << std::endl;
            if (DEBUG) std::cout << "[DEBUG] File size: " << source.size() << " bytes" << std::endl;

            Lexer lexer(source);
            std::vector<Token> tokens;
            Token t;
            size_t token_count = 0;
            do {
                t = lexer.next_token();
                tokens.push_back(t);
                token_count++;
                if (token_count > 10000) {
                    std::cerr << "Error: Too many tokens, possible infinite loop in lexer" << std::endl;
                    break;
                }
            } while (t.type != T_EOF);
            
            if (DEBUG) std::cout << "[DEBUG] Tokenized " << tokens.size() << " tokens" << std::endl;

            Parser parser;
            parser.load_tokens(tokens);
            if (DEBUG) std::cout << "[DEBUG] Starting parse_program..." << std::endl;
            std::vector<std::shared_ptr<AstNode>> program;
            try {
                program = parser.parse_program();
            } catch (const CompilerError& e) {
                std::cerr << e.get_traceback();
                return 1;
            }
            if (DEBUG) std::cout << "[DEBUG] Parsed " << program.size() << " nodes" << std::endl;

            // Проверяем обязательные элементы для главного файла
            if (current_file == filename) {
                if (!parser.found_package_main()) {
                    std::cerr << "Error: Missing 'package main' declaration in main file\n";
                    return 1;
                }
                if (!parser.found_return_zero()) {
                    std::cerr << "Error: Missing 'return 0' at the end of main file\n";
                    return 1;
                }
            }

            // Сохраняем AST для этого файла
            file_asts[current_file] = program;

            // Ищем импорты и добавляем их в очередь загрузки
            std::string base_dir = get_directory(current_file);
            for (const auto& node : program) {
                if (auto import = std::dynamic_pointer_cast<ImportStmt>(node)) {
                    std::string import_path = resolve_import_path(base_dir, import->file_path);
                    if (DEBUG) {
                        std::cout << "[DEBUG] Found import: ";
                        for (size_t i = 0; i < import->import_names.size(); ++i) {
                            if (i > 0) std::cout << ", ";
                            std::cout << import->import_names[i];
                        }
                        std::cout << " from " << import->file_path 
                                 << " -> resolved to " << import_path << std::endl;
                    }
                    files_to_load.push_back(import_path);
                }
            }

            loaded_files.insert(current_file);
        }

        // Проверяем, что все импортированные функции существуют в импортируемых файлах
        std::map<std::string, std::set<std::string>> file_functions;  // файл -> множество функций
        for (const auto& [file, ast] : file_asts) {
            for (const auto& node : ast) {
                if (auto func = std::dynamic_pointer_cast<FunctionDef>(node)) {
                    file_functions[file].insert(func->name);
                }
            }
        }
        
        // Проверяем импорты
        for (const auto& [file, ast] : file_asts) {
            for (const auto& node : ast) {
                if (auto import = std::dynamic_pointer_cast<ImportStmt>(node)) {
                    std::string import_path = resolve_import_path(get_directory(file), import->file_path);
                    if (!file_functions.count(import_path)) {
                        std::cerr << "Error: Cannot find imported file: " << import_path << "\n";
                        return 1;
                    }
                    const auto& available_funcs = file_functions[import_path];
                    for (const auto& func_name : import->import_names) {
                        if (!available_funcs.count(func_name)) {
                            SemanticError err("Function '" + func_name + "' not found in imported file '" + import->file_path + "'", 
                                             import->location);
                            std::cerr << err.get_traceback();
                            return 1;
                        }
                    }
                }
            }
        }

        // Объединяем все AST в один
        std::vector<std::shared_ptr<AstNode>> combined_program;
        for (const auto& [file, ast] : file_asts) {
            for (const auto& node : ast) {
                // Пропускаем импорты, они уже обработаны
                if (!std::dynamic_pointer_cast<ImportStmt>(node)) {
                    combined_program.push_back(node);
                }
            }
        }

        // Проверяем наличие return 1 в функции main
        bool main_has_return_one = false;
        for (const auto& node : combined_program) {
            if (auto func = std::dynamic_pointer_cast<FunctionDef>(node)) {
                if (func->name == "main") {
                    main_has_return_one = func->has_return_one;
                    break;
                }
            }
        }
        if (!main_has_return_one) {
            std::cerr << "Error: Function 'main' must contain 'return 1'\n";
            return 1;
        }

        // Семантический анализ
        try {
            SemanticAnalyzer analyzer;
            analyzer.analyze(combined_program);
        } catch (const CompilerError& e) {
            std::cerr << e.get_traceback();
            return 1;
        }

        // Выполнение
        try {
            Interpreter interp;
            interp.run(combined_program);
        } catch (const CompilerError& e) {
            std::cerr << e.get_traceback();
            return 1;
        }

        return 0;
    }

    if (command == "build") {
        if (argc < 3) {
            std::cerr << "Error: No input file specified.\n";
            std::cerr << "Usage: pgt build <file.pgt> [-o output]\n";
            return 1;
        }

        std::string filename = argv[2];
        std::string output_name = "a.out";
        
        // Проверка -o для указания имени выходного файла
        if (argc >= 5 && std::string(argv[3]) == "-o") {
            output_name = argv[4];
        }

        // Используем ту же систему загрузки файлов, что и для run
        std::set<std::string> loaded_files;
        std::map<std::string, std::vector<std::shared_ptr<AstNode>>> file_asts;
        std::vector<std::string> files_to_load = {filename};

        // Функция для получения директории файла
        auto get_directory = [](const std::string& filepath) -> std::string {
            size_t last_slash = filepath.find_last_of("/\\");
            if (last_slash == std::string::npos) return ".";
            return filepath.substr(0, last_slash);
        };

        // Функция для разрешения пути к импортируемому файлу
        auto resolve_import_path = [&](const std::string& base_dir, const std::string& import_path) -> std::string {
            if (import_path[0] == '/' || (import_path.size() > 1 && import_path[0] == '.' && import_path[1] == '/')) {
                return import_path;
            }
            
            std::string full_path = import_path;
            if (full_path.size() < 4 || full_path.substr(full_path.size() - 4) != ".pgt") {
                full_path += ".pgt";
            }
            
            std::string relative_path = base_dir + "/" + full_path;
            std::ifstream test(relative_path);
            if (test) {
                test.close();
                return relative_path;
            }
            
            test.open(full_path);
            if (test) {
                test.close();
                return full_path;
            }
            
            return full_path;
        };

        // Загружаем все файлы
        while (!files_to_load.empty()) {
            std::string current_file = files_to_load.back();
            files_to_load.pop_back();

            if (loaded_files.count(current_file)) continue;

            std::ifstream f(current_file);
            if (!f) {
                std::cerr << "Error: Cannot open file '" << current_file << "'\n";
                continue;
            }

            std::string source((std::istreambuf_iterator<char>(f)), {});
            f.close();

            Lexer lexer(source);
            std::vector<Token> tokens;
            Token t;
            do {
                t = lexer.next_token();
                tokens.push_back(t);
            } while (t.type != T_EOF);
            
            Parser parser;
            parser.load_tokens(tokens);
            std::vector<std::shared_ptr<AstNode>> program;
            try {
                program = parser.parse_program();
            } catch (const CompilerError& e) {
                std::cerr << e.get_traceback();
                return 1;
            }

            file_asts[current_file] = program;

            // Ищем импорты
            std::string base_dir = get_directory(current_file);
            for (const auto& node : program) {
                if (auto import = std::dynamic_pointer_cast<ImportStmt>(node)) {
                    std::string import_path = resolve_import_path(base_dir, import->file_path);
                    files_to_load.push_back(import_path);
                }
            }

            loaded_files.insert(current_file);
        }

        // Объединяем все AST
        std::vector<std::shared_ptr<AstNode>> combined_program;
        for (const auto& [file, ast] : file_asts) {
            for (const auto& node : ast) {
                if (!std::dynamic_pointer_cast<ImportStmt>(node)) {
                    combined_program.push_back(node);
                }
            }
        }

        // Семантический анализ
        try {
            SemanticAnalyzer analyzer;
            analyzer.analyze(combined_program);
        } catch (const CompilerError& e) {
            std::cerr << e.get_traceback();
            return 1;
        }

        // Генерация C кода
        CodeGen codegen;
        std::string c_code = codegen.generate(combined_program);
        
        // Сохраняем C код во временный файл
        std::string temp_c_file = output_name + ".c";
        std::ofstream out(temp_c_file);
        if (!out) {
            std::cerr << "Error: Cannot create output file\n";
            return 1;
        }
        out << c_code;
        out.close();
        
        std::cout << "Generated C code: " << temp_c_file << "\n";
        
        // Компилируем C код в бинарник
        std::string compile_cmd = "g++ -I. " + temp_c_file + " src/GarbageCollector.cpp src/Utils.cpp -lstdc++ -o " + output_name;
        std::cout << "Compiling: " << compile_cmd << "\n";
        int result = system(compile_cmd.c_str());
        
        if (result == 0) {
            std::cout << "Successfully compiled to: " << output_name << "\n";
            // Удаляем временный C файл
            remove(temp_c_file.c_str());
        } else {
            std::cerr << "Compilation failed\n";
            return 1;
        }

        return 0;
    }

    if (command == "history") {
        std::cout << "Hello, my name is Pabla\n";
        std::cout << "I'm a programmer and a developer\n";
        std::cout << "I'm a student of the 11th grade of the school\n";
        std::cout << "I'm from Ukraine, Lutsk\n";
        std::cout << "I'm 17 years old\n";
        std::cout << "now i'm living Sweden, Malmö\n";
        std::cout << "I'm studying at the Malmö University\n";
        std::cout << "I'm learning programming and developing languages\n";
        return 0;
    }

    std::cerr << "Unknown command: " << command << "\n";
    std::cerr << "Use 'pgt help' for available commands.\n";
    return 1;
}