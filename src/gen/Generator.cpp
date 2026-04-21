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

std::string logging_component_source() {
    return
        "package logging\n"
        "\n"
        "function(to_console) {\n"
        "    log_console()\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(to_file, path + string) {\n"
        "    log_file(path)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(output, target + string) {\n"
        "    log_output(target)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(open, path + string) {\n"
        "    log_file(path)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(trace, message + string) {\n"
        "    log_trace(message)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(debug, message + string) {\n"
        "    log_debug(message)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(info, message + string) {\n"
        "    log_info(message)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(warn, message + string) {\n"
        "    log_warn(message)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(error, message + string) {\n"
        "    log_error(message)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(critical, message + string) {\n"
        "    log_critical(message)\n"
        "    return 1\n"
        "}\n";
}

std::string generic_component_source(const std::string& name) {
    std::ostringstream source;
    source << "package " << name << "\n"
           << "\n"
           << "function(init) {\n"
           << "    log_info(\"Component " << name << " initialized\")\n"
           << "    return 1\n"
           << "}\n"
           << "\n"
           << "function(render) {\n"
           << "    return \"" << name << " component\"\n"
           << "    return 1\n"
           << "}\n";
    return source.str();
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
    return generate_component_command(argc, argv);
}
