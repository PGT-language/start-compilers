import subprocess
import os

source_files = [
    "src/main.cpp",
    "src/Lexer.cpp",
    "src/Parser.cpp",
    "src/Interpreter.cpp",
    "src/Utils.cpp",
    "src/SemanticAnalyzer.cpp",
    "src/CodeGen.cpp",
    "src/GarbageCollector.cpp"
]

output_executable = "pgt"
object_files = []

try:
    # Compile source files into object files
    for source_file in source_files:
        object_file = source_file.replace(".cpp", ".o")
        compile_command = ["g++", "-std=c++17", "-I.", "-c", source_file, "-o", object_file]
        subprocess.run(compile_command, check=True)
        object_files.append(object_file)

    # Link object files into an executable
    link_command = ["g++", "-std=c++17", "-o", output_executable] + object_files
    subprocess.run(link_command, check=True)
    print(f"Compilation successful. Executable '{output_executable}' created.")

    # Copy the executable to the compile/ directory
    compile_dir = "compile/"
    if not os.path.exists(compile_dir):
        os.makedirs(compile_dir)

    subprocess.run(["cp", output_executable, compile_dir], check=True)
    print(f"Executable moved to '{compile_dir}'.")

except subprocess.CalledProcessError as e:
    print(f"Compilation failed: {e}")
except FileNotFoundError:
    print("Error: 'g++' compiler not found. Please install it and make sure it's in your PATH.")
finally:
    # Clean up object files
    for object_file in object_files:
        if os.path.exists(object_file):
            os.remove(object_file)
    if os.path.exists(output_executable):
        os.remove(output_executable)