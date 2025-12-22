import subprocess

subprocess.run(["g++", "-std=c++17", "src/main.cpp", "src/Interpreter.cpp", "src/Lexer.cpp", "src/Parser.cpp", "src/Utils.cpp", "-o", "pgt"])

subprocess.run(["cp", "pgt", "/home/pabla/Documents/my-langvihc/compile/"])