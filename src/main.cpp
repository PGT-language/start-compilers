#include "Lexer.h"
#include "Parser.h"
#include "Interpreter.h"
#include "Utils.h"
#include "Ast.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cctype>
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
        std::cout << "  history                 — Show history of commands\n";
        std::cout << "Example:\n";
        std::cout << "  ./pgt run test.pgt\n";
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

            Lexer lexer(source);
            std::vector<Token> tokens;
            Token t;
            do {
                t = lexer.next_token();
                tokens.push_back(t);
            } while (t.type != T_EOF);

            Parser parser;
            parser.load_tokens(tokens);
            auto program = parser.parse_program();

            // Сохраняем AST для этого файла
            file_asts[current_file] = program;

            // Ищем импорты и добавляем их в очередь загрузки
            std::string base_dir = get_directory(current_file);
            for (const auto& node : program) {
                if (auto import = std::dynamic_pointer_cast<ImportStmt>(node)) {
                    std::string import_path = resolve_import_path(base_dir, import->file_path);
                    if (DEBUG) std::cout << "[DEBUG] Found import: " << import->import_name 
                                         << " from " << import->file_path 
                                         << " -> resolved to " << import_path << std::endl;
                    files_to_load.push_back(import_path);
                }
            }

            loaded_files.insert(current_file);
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

        Interpreter interp;
        interp.run(combined_program);

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