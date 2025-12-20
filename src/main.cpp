#include "Lexer.h"
#include "Parser.h"
#include "Interpreter.h"
#include "Utils.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cctype>

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

        std::ifstream f(filename);
        if (!f) {
            std::cerr << "Error: Cannot open file '" << filename << "'\n";
            return 1;
        }

        std::string source((std::istreambuf_iterator<char>(f)), {});

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

        Interpreter interp;
        interp.run(program);

        return 0;
    }

    std::cerr << "Unknown command: " << command << "\n";
    std::cerr << "Use 'pgt help' for available commands.\n";
    return 1;
}