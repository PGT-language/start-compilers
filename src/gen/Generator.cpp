#include "Generator.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
std::string normalize_component_name(const std::string& raw_name) {
    std::string name;
    bool previous_was_separator = false;

    for (char raw_ch : raw_name) {
        unsigned char ch = static_cast<unsigned char>(raw_ch);
        if (std::isalnum(ch)) {
            name += static_cast<char>(std::tolower(ch));
            previous_was_separator = false;
        } else if (raw_ch == '_' || raw_ch == '-' || raw_ch == ' ') {
            if (!name.empty() && !previous_was_separator) {
                name += '_';
                previous_was_separator = true;
            }
        }
    }

    while (!name.empty() && name.back() == '_') {
        name.pop_back();
    }

    if (name.empty()) {
        name = "component";
    }
    if (std::isdigit(static_cast<unsigned char>(name[0]))) {
        name = "component_" + name;
    }
    return name;
}

std::string sql_type_for(const std::string& pgt_type) {
    if (pgt_type == "int") return "INTEGER";
    if (pgt_type == "float") return "REAL";
    if (pgt_type == "bool") return "INTEGER";
    if (pgt_type == "bytes") return "BLOB";
    return "TEXT";
}

std::string pluralize_table_name(const std::string& name) {
    if (!name.empty() && name.back() == 's') {
        return name;
    }
    return name + "s";
}

std::string logging_component_source() {
    return
        "package logging\n"
        "\n"
        "function(to_console) {\n"
        "    log::console()\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(to_file, path + string) {\n"
        "    log::file(path)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(output, target + string) {\n"
        "    log::output(target)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(open, path + string) {\n"
        "    log::file(path)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(trace, message + string) {\n"
        "    log::trace(message)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(debug, message + string) {\n"
        "    log::debug(message)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(info, message + string) {\n"
        "    log::info(message)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(warn, message + string) {\n"
        "    log::warn(message)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(error, message + string) {\n"
        "    log::error(message)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(critical, message + string) {\n"
        "    log::critical(message)\n"
        "    return 1\n"
        "}\n";
}

std::string generic_component_source(const std::string& name) {
    std::ostringstream source;
    source << "package " << name << "\n"
           << "\n"
           << "function(init) {\n"
           << "    log::info(\"Component " << name << " initialized\")\n"
           << "    return 1\n"
           << "}\n"
           << "\n"
           << "function(render) {\n"
           << "    return \"" << name << " component\"\n"
           << "    return 1\n"
           << "}\n";
    return source.str();
}

std::string file_source(const std::string& package_name) {
    std::ostringstream source;
    source << "package " << package_name << "\n"
           << "\n"
           << "function(init) {\n"
           << "    return 1\n"
           << "}\n";
    return source.str();
}

std::string model_source(const std::string& name, char** argv, int argc) {
    std::string table_name = pluralize_table_name(name);
    std::ostringstream columns;
    columns << "id INTEGER PRIMARY KEY";

    for (int i = 4; i < argc; ++i) {
        std::string field = argv[i];
        size_t colon = field.find(':');
        std::string field_name = normalize_component_name(colon == std::string::npos ? field : field.substr(0, colon));
        std::string field_type = colon == std::string::npos ? "string" : field.substr(colon + 1);
        if (field_name.empty() || field_name == "id") {
            continue;
        }
        columns << ", " << field_name << " " << sql_type_for(field_type);
    }

    std::ostringstream source;
    source << "package " << name << "\n"
           << "\n"
           << "function(migrate) {\n"
           << "    sql::table(\"" << table_name << "\", \"" << columns.str() << "\")\n"
           << "    return 1\n"
           << "}\n"
           << "\n"
           << "function(save, data + object) {\n"
           << "    orm::save(\"" << table_name << "\", data)\n"
           << "    return 1\n"
           << "}\n";
    return source.str();
}

int generate_file_command(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: pgt generate file <path> [package]\n";
        return 1;
    }

    std::filesystem::path file_path = argv[3];
    if (file_path.extension() != ".pgt") {
        file_path += ".pgt";
    }

    std::string package_name = argc >= 5
        ? normalize_component_name(argv[4])
        : normalize_component_name(file_path.stem().string());

    try {
        if (std::filesystem::exists(file_path)) {
            std::cerr << "File already exists: " << file_path.string() << "\n";
            return 1;
        }
        if (file_path.has_parent_path()) {
            std::filesystem::create_directories(file_path.parent_path());
        }
    } catch (const std::filesystem::filesystem_error& error) {
        std::cerr << "Error: Cannot prepare file path: " << error.what() << "\n";
        return 1;
    }

    std::ofstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot create file: " << file_path.string() << "\n";
        return 1;
    }
    file << file_source(package_name);
    file.close();

    std::cout << "Created file: " << file_path.string() << "\n";
    return 0;
}

int generate_model_command(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: pgt generate model <name> [field:type ...]\n";
        return 1;
    }

    std::string name = normalize_component_name(argv[3]);
    std::filesystem::path model_dir = std::filesystem::path("models") / name;
    std::filesystem::path model_file = model_dir / (name + ".pgt");

    try {
        if (std::filesystem::exists(model_file)) {
            std::cerr << "Model already exists: " << model_file.string() << "\n";
            return 1;
        }
        std::filesystem::create_directories(model_dir);
    } catch (const std::filesystem::filesystem_error& error) {
        std::cerr << "Error: Cannot prepare model directory: " << error.what() << "\n";
        return 1;
    }

    std::ofstream file(model_file);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot create model file: " << model_file.string() << "\n";
        return 1;
    }
    file << model_source(name, argv, argc);
    file.close();

    std::cout << "Created model: " << model_file.string() << "\n";
    return 0;
}

int generate_component_command(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: pgt generate component <name>\n";
        return 1;
    }

    std::string kind = argv[2];
    if (kind != "component" && kind != "c") {
        std::cerr << "Unknown generator: " << kind << "\n";
        std::cerr << "Usage: pgt generate component <name>\n";
        return 1;
    }

    std::string name = normalize_component_name(argv[3]);
    std::filesystem::path component_dir = std::filesystem::path("components") / name;
    std::filesystem::path component_file = component_dir / (name + ".pgt");

    try {
        if (std::filesystem::exists(component_file)) {
            std::cerr << "Component already exists: " << component_file.string() << "\n";
            return 1;
        }

        std::filesystem::create_directories(component_dir);
    } catch (const std::filesystem::filesystem_error& error) {
        std::cerr << "Error: Cannot prepare component directory: " << error.what() << "\n";
        return 1;
    }

    std::ofstream file(component_file);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot create component file: " << component_file.string() << "\n";
        return 1;
    }

    if (name == "logging") {
        file << logging_component_source();
    } else {
        file << generic_component_source(name);
    }
    file.close();

    std::cout << "Created component: " << component_file.string() << "\n";
    return 0;
}
}

int run_generator_command(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: pgt generate <component|file|model|class> ...\n";
        return 1;
    }

    std::string kind = argv[2];
    if (kind == "component" || kind == "c") {
        return generate_component_command(argc, argv);
    }
    if (kind == "file" || kind == "f") {
        return generate_file_command(argc, argv);
    }
    if (kind == "model" || kind == "class" || kind == "m") {
        return generate_model_command(argc, argv);
    }

    std::cerr << "Unknown generator: " << kind << "\n";
    std::cerr << "Usage: pgt generate <component|file|model|class> ...\n";
    return 1;
}
