#include "Lexer.h"
#include "Parser.h"
#include "Interpreter.h"
#include "Utils.h"
#include "Ast.h"
#include "SemanticAnalyzer.h"
#include "Error.h"
#include "PackageResolver.h"
#include "gen/Generator.h"
#include "init/ProjectInit.h"

#include <iostream>
#include <fstream>
#include <set>
#include <map>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Program Generate Time (PGT) Compiler v0.1\n";
        std::cout << "Usage:\n";
        std::cout << "  pgt help                — Show this help\n";
        std::cout << "  pgt version             — Show version\n";
        std::cout << "  pgt run <file.pgt>      — Run PGT program\n";
        std::cout << "  pgt run <file.pgt> --debug — Run with debug output\n";
        std::cout << "  pgt init [template] [name] — Initialize a project from template\n";
        std::cout << "  pgt generate component <name> — Generate a PGT component\n";
        std::cout << "  pgt generate file <path> [package] — Generate a PGT file\n";
        std::cout << "  pgt generate model <name> [field:type ...] — Generate an ORM model\n";
        std::cout << "  pgt generate class <name> [field:type ...] — Alias for model\n";
        return 0;
    }

    std::string command = argv[1];

    if (command == "help" || command == "--help" || command == "-h") {
        std::cout << "Program Generate Time (PGT) Compiler v0.1\n";
        std::cout << "Commands:\n";
        std::cout << "  help                    — Show this help message\n";
        std::cout << "  version                 — Show compiler version\n";
        std::cout << "  run <file.pgt>          — Execute .pgt file\n";
        std::cout << "  run <file.pgt> --debug  — Execute with debug info\n\n";
        std::cout << "  init [template] [name]  — Initialize a project from template\n";
        std::cout << "  init backend test       — Create backend project named test\n\n";
        std::cout << "  generate component <name> — Generate a PGT component\n";
        std::cout << "  generate file <path> [package] — Generate a PGT file\n";
        std::cout << "  generate model <name> [field:type ...] — Generate an ORM model\n";
        std::cout << "  generate class <name> [field:type ...] — Alias for model\n";
        std::cout << "  history                 — Show history of commands\n";
        std::cout << "Example:\n";
        std::cout << "  ./pgt run test.pgt\n";
        std::cout << "  ./pgt init backend test\n";
        std::cout << "  ./pgt generate component logging\n";
        std::cout << "  ./pgt generate model user name:string email:string\n";
        return 0;
    }

    if (command == "version" || command == "--version" || command == "-v") {
        std::cout << "PGT Compiler v0.1\n";
        std::cout << "Built on Aprel 21 2026\n";
        std::cout << "Author: pabla\n";
        return 0;
    }

    if (command == "generate" || command == "g") {
        return run_generator_command(argc, argv);
    }

    if (command == "init") {
        return run_project_init_command(argc, argv);
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
        std::map<std::string, std::string> file_packages;
        std::map<std::string, std::string> directory_packages;
        std::map<std::string, std::string> directory_package_sources;
        std::vector<std::string> files_to_load = {filename};
        PackageResolver package_resolver(filename, argv[0]);

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
                return 1;
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

            if (!parser.found_package_decl()) {
                SemanticError err("Missing package declaration: expected 'package <name>' at the top of the file.",
                                  SourceLocation(1, 0, current_file));
                std::cerr << err.get_traceback();
                return 1;
            }

            std::string parsed_package_name = parser.parsed_package_name();
            try {
                package_resolver.validate_package_directory(current_file, parsed_package_name);
            } catch (const CompilerError& e) {
                std::cerr << e.get_traceback();
                return 1;
            }

            std::string current_dir = PackageResolver::directory_of(current_file);
            if (directory_packages.count(current_dir) && directory_packages[current_dir] != parsed_package_name) {
                SemanticError err("Directory contains mixed packages: '" + directory_packages[current_dir] +
                                  "' and '" + parsed_package_name + "'. Move package '" +
                                  parsed_package_name + "' into its own directory.",
                                  SourceLocation(1, 0, current_file));
                std::cerr << err.get_traceback();
                return 1;
            }
            directory_packages[current_dir] = parsed_package_name;
            directory_package_sources[current_dir] = current_file;
            file_packages[current_file] = parsed_package_name;

            // Проверяем обязательные элементы для главного файла
            if (current_file == filename) {
                if (!parser.found_package_main()) {
                    SemanticError err("Main file must declare 'package main'.",
                                      SourceLocation(1, 0, current_file));
                    std::cerr << err.get_traceback();
                    return 1;
                }
                try {
                    package_resolver.validate_main_package_root(filename);
                } catch (const CompilerError& e) {
                    std::cerr << e.get_traceback();
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
            std::string base_dir = PackageResolver::directory_of(current_file);
            for (const auto& node : program) {
                if (auto import = std::dynamic_pointer_cast<ImportStmt>(node)) {
                    ResolvedImport resolved_import = package_resolver.resolve_import_path(base_dir, import->file_path);
                    if (!resolved_import.found) {
                        SemanticError err("Import '" + import->file_path + "' was not found. Expected file '" +
                                          resolved_import.path + "'.",
                                          SourceLocation(import->location.line, import->location.column, current_file));
                        std::cerr << err.get_traceback();
                        return 1;
                    }
                    if (DEBUG) {
                        std::cout << "[DEBUG] Found import: ";
                        for (size_t i = 0; i < import->import_names.size(); ++i) {
                            if (i > 0) std::cout << ", ";
                            std::cout << import->import_names[i];
                        }
                        std::cout << " from " << import->file_path
                                 << " -> resolved to " << resolved_import.path << std::endl;
                    }
                    for (const auto& import_file : resolved_import.files) {
                        files_to_load.push_back(import_file);
                    }
                }
            }

            loaded_files.insert(current_file);
        }

        // Проверяем, что все импортированные символы существуют в импортируемых файлах
        std::map<std::string, std::set<std::string>> file_symbols;  // файл -> функции и классы
        for (const auto& [file, ast] : file_asts) {
            for (const auto& node : ast) {
                if (auto func = std::dynamic_pointer_cast<FunctionDef>(node)) {
                    file_symbols[file].insert(func->name);
                } else if (auto klass = std::dynamic_pointer_cast<ClassDef>(node)) {
                    file_symbols[file].insert(klass->name);
                }
            }
        }

        // Проверяем импорты
        for (const auto& [file, ast] : file_asts) {
            for (const auto& node : ast) {
                if (auto import = std::dynamic_pointer_cast<ImportStmt>(node)) {
                    ResolvedImport resolved_import = package_resolver.resolve_import_path(PackageResolver::directory_of(file),
                                                                                         import->file_path);
                    if (!resolved_import.found || resolved_import.files.empty()) {
                        std::cerr << "Error: Cannot find imported file: " << resolved_import.path << "\n";
                        return 1;
                    }
                    const std::string& current_package = file_packages[file];
                    std::set<std::string> available_symbols;
                    for (const auto& import_file : resolved_import.files) {
                        if (!file_asts.count(import_file)) {
                            std::cerr << "Error: Cannot find imported file: " << import_file << "\n";
                            return 1;
                        }

                        const std::string& imported_package = file_packages[import_file];
                        if (imported_package == "main") {
                            SemanticError err("Package 'main' cannot be imported. Move shared code into a separate package.",
                                              SourceLocation(import->location.line, import->location.column, file));
                            std::cerr << err.get_traceback();
                            return 1;
                        }
                        if (PackageResolver::directory_of(file) == PackageResolver::directory_of(import_file) &&
                            current_package != imported_package) {
                            SemanticError err("Directory cannot contain mixed packages: '" + current_package +
                                              "' and '" + imported_package +
                                              "'. Move package '" + imported_package + "' into its own directory.",
                                              SourceLocation(import->location.line, import->location.column, file));
                            std::cerr << err.get_traceback();
                            return 1;
                        }
                        if (file_symbols.count(import_file)) {
                            available_symbols.insert(file_symbols[import_file].begin(), file_symbols[import_file].end());
                        }
                    }
                    for (const auto& symbol_name : import->import_names) {
                        if (!available_symbols.count(symbol_name)) {
                            SemanticError err("Symbol '" + symbol_name + "' not found in import '" + import->file_path + "'",
                                             SourceLocation(import->location.line, import->location.column, file));
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

        // Проверяем наличие return 1 в каждой функции
        for (const auto& node : combined_program) {
            if (auto func = std::dynamic_pointer_cast<FunctionDef>(node)) {
                if (!func->has_return_one) {
                    SemanticError err("Function '" + func->name + "' must contain 'return 1'", func->location);
                    std::cerr << err.get_traceback();
                    return 1;
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
