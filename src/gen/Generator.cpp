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

std::string class_name_for(const std::string& name) {
    std::string class_name;
    bool uppercase_next = true;

    for (char raw_ch : name) {
        if (raw_ch == '_') {
            uppercase_next = true;
            continue;
        }

        unsigned char ch = static_cast<unsigned char>(raw_ch);
        if (uppercase_next) {
            class_name += static_cast<char>(std::toupper(ch));
            uppercase_next = false;
        } else {
            class_name += static_cast<char>(std::tolower(ch));
        }
    }

    if (class_name.empty()) {
        return "Model";
    }
    if (std::isdigit(static_cast<unsigned char>(class_name[0]))) {
        return "Model" + class_name;
    }
    return class_name;
}

std::string orm_column_type_for(const std::string& pgt_type) {
    std::string type;
    for (char raw_ch : pgt_type) {
        type += static_cast<char>(std::tolower(static_cast<unsigned char>(raw_ch)));
    }

    if (type == "int" || type == "integer") return "db.Integer";
    if (type == "float" || type == "real") return "db.Float";
    if (type == "bool" || type == "boolean") return "db.Boolean";
    if (type == "bytes" || type == "blob") return "db.Bytes";
    if (type.rfind("string(", 0) == 0 && type.back() == ')') {
        return "db.String" + pgt_type.substr(6);
    }
    return "db.String(100)";
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
    std::string class_name = class_name_for(name);
    std::ostringstream fields;
    fields << "    id = db.Column(db.Integer, primary_key=True)\n";

    for (int i = 4; i < argc; ++i) {
        std::string field = argv[i];
        size_t colon = field.find(':');
        std::string field_name = normalize_component_name(colon == std::string::npos ? field : field.substr(0, colon));
        std::string field_type = colon == std::string::npos ? "string" : field.substr(colon + 1);
        if (field_name.empty() || field_name == "id") {
            continue;
        }
        fields << "    " << field_name << " = db.Column(" << orm_column_type_for(field_type) << ")\n";
    }

    std::ostringstream source;
    source << "package " << name << "\n"
           << "\n"
           << "class " << class_name << "(db.Model) {\n"
           << fields.str()
           << "}\n"
           << "\n"
           << "function(migrate) {\n"
           << "    orm::migrate(\"" << class_name << "\")\n"
           << "    return 1\n"
           << "}\n"
           << "\n"
           << "function(save, data + object) {\n"
           << "    orm::save(\"" << class_name << "\", data)\n"
           << "    return 1\n"
           << "}\n";
    return source.str();
}

std::string swagger_html_source(const std::string& title) {
    std::ostringstream source;
    source << "<!doctype html>\n"
           << "<html lang=\"en\">\n"
           << "<head>\n"
           << "    <meta charset=\"utf-8\">\n"
           << "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           << "    <title>" << title << " Swagger</title>\n"
           << "    <link rel=\"stylesheet\" href=\"https://unpkg.com/swagger-ui-dist@5/swagger-ui.css\">\n"
           << "</head>\n"
           << "<body>\n"
           << "    <div id=\"swagger-ui\"></div>\n"
           << "    <script src=\"https://unpkg.com/swagger-ui-dist@5/swagger-ui-bundle.js\"></script>\n"
           << "    <script>\n"
           << "        window.addEventListener('load', () => {\n"
           << "            SwaggerUIBundle({\n"
           << "                url: '/api/v1/openapi.yaml',\n"
           << "                dom_id: '#swagger-ui'\n"
           << "            });\n"
           << "        });\n"
           << "    </script>\n"
           << "</body>\n"
           << "</html>\n";
    return source.str();
}

std::string default_api_spec_source(const std::string& title) {
    std::ostringstream source;
    source << "openapi: 3.0.3\n"
           << "info:\n"
           << "  title: " << title << " API\n"
           << "  version: 0.1.0\n"
           << "paths:\n"
           << "  /:\n"
           << "    get:\n"
           << "      summary: Index route\n"
           << "      responses:\n"
           << "        \"200\":\n"
           << "          description: OK\n";
    return source.str();
}

std::string swagger_package_source() {
    return
        "package sweiger\n"
        "\n"
        "function(docs) {\n"
        "    return read::file(\"sweiger/index.html\")\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(openapi_yaml) {\n"
        "    return read::file(\"api.yaml\")\n"
        "    return 1\n"
        "}\n";
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

int generate_swagger_command(int argc, char** argv) {
    std::string title = argc >= 4 ? argv[3] : "PGT";
    std::filesystem::path swagger_dir = "sweiger";
    std::filesystem::path swagger_file = swagger_dir / "index.html";
    std::filesystem::path swagger_package = swagger_dir / "sweiger.pgt";
    std::filesystem::path api_spec_file = "api.yaml";

    try {
        if (std::filesystem::exists(swagger_file)) {
            std::cerr << "Swagger HTML already exists: " << swagger_file.string() << "\n";
            return 1;
        }
        if (std::filesystem::exists(swagger_package)) {
            std::cerr << "Swagger package already exists: " << swagger_package.string() << "\n";
            return 1;
        }
        std::filesystem::create_directories(swagger_dir);
    } catch (const std::filesystem::filesystem_error& error) {
        std::cerr << "Error: Cannot prepare swagger directory: " << error.what() << "\n";
        return 1;
    }

    std::ofstream file(swagger_file);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot create swagger HTML: " << swagger_file.string() << "\n";
        return 1;
    }
    file << swagger_html_source(title);
    file.close();

    std::ofstream package_file(swagger_package);
    if (!package_file.is_open()) {
        std::cerr << "Error: Cannot create swagger package: " << swagger_package.string() << "\n";
        return 1;
    }
    package_file << swagger_package_source();
    package_file.close();

    if (!std::filesystem::exists(api_spec_file)) {
        std::ofstream spec_file(api_spec_file);
        if (!spec_file.is_open()) {
            std::cerr << "Error: Cannot create API spec: " << api_spec_file.string() << "\n";
            return 1;
        }
        spec_file << default_api_spec_source(title);
        spec_file.close();
    }

    std::cout << "Created Swagger HTML: " << swagger_file.string() << "\n";
    std::cout << "Created Swagger package: " << swagger_package.string() << "\n";
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
        std::cerr << "Usage: pgt generate <component|file|model|class|swagger> ...\n";
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
    if (kind == "swagger" || kind == "swag" || kind == "sweiger") {
        return generate_swagger_command(argc, argv);
    }

    std::cerr << "Unknown generator: " << kind << "\n";
    std::cerr << "Usage: pgt generate <component|file|model|class|swagger> ...\n";
    return 1;
}
