#include "ProjectInit.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct InitOptions {
    std::string template_name;
    std::string project_name;
    bool create_logging = false;
    std::string log_output = "console";
    std::string log_level = "info";
    bool use_database = false;
    std::string table_type = "model";
    std::string table_name = "test";
    bool create_api = true;
    bool create_docker = false;
    bool create_nginx = false;
};

const char* DEFAULT_PGT_DOCKER_IMAGE = "pablaofficeal/pgt:latest";

std::string trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        start++;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        end--;
    }
    return value.substr(start, end - start);
}

std::string lowercase(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string normalize_identifier(const std::string& raw_name, const std::string& fallback) {
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
        name = fallback;
    }
    if (std::isdigit(static_cast<unsigned char>(name[0]))) {
        name = fallback + "_" + name;
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

bool contains_choice(const std::vector<std::string>& choices, const std::string& value) {
    return std::find(choices.begin(), choices.end(), value) != choices.end();
}

void print_init_help() {
    std::cout << "Usage:\n";
    std::cout << "  pgt init [template] [project-name]\n\n";
    std::cout << "Templates:\n";
    std::cout << "  backend  Create a modular backend project with optional logging, database, API, Docker, and nginx\n\n";
    std::cout << "Example:\n";
    std::cout << "  pgt init backend test\n";
}

std::string prompt_text(const std::string& question, const std::string& default_value = "") {
    while (true) {
        std::cout << question;
        if (!default_value.empty()) {
            std::cout << " [" << default_value << "]";
        }
        std::cout << ": ";

        std::string answer;
        if (!std::getline(std::cin, answer)) {
            return default_value;
        }

        answer = trim(answer);
        if (!answer.empty()) {
            return answer;
        }
        if (!default_value.empty()) {
            return default_value;
        }
    }
}

bool prompt_yes_no(const std::string& question, bool default_value) {
    std::string suffix = default_value ? "Y/n" : "y/N";
    while (true) {
        std::cout << question << " (" << suffix << "): ";
        std::string answer;
        if (!std::getline(std::cin, answer)) {
            return default_value;
        }

        answer = lowercase(trim(answer));
        if (answer.empty()) {
            return default_value;
        }
        if (answer == "y" || answer == "yes" || answer == "да" || answer == "d") {
            return true;
        }
        if (answer == "n" || answer == "no" || answer == "нет") {
            return false;
        }
        std::cout << "Please answer yes or no.\n";
    }
}

std::string prompt_choice(const std::string& question,
                          const std::vector<std::string>& choices,
                          const std::string& default_value) {
    std::ostringstream choices_text;
    for (size_t i = 0; i < choices.size(); ++i) {
        if (i > 0) {
            choices_text << "/";
        }
        choices_text << choices[i];
    }

    while (true) {
        std::string answer = lowercase(prompt_text(question + " (" + choices_text.str() + ")", default_value));
        if (contains_choice(choices, answer)) {
            return answer;
        }
        std::cout << "Available choices: " << choices_text.str() << "\n";
    }
}

bool write_file(const std::filesystem::path& path, const std::string& content) {
    if (std::filesystem::exists(path)) {
        std::cerr << "Error: File already exists: " << path.string() << "\n";
        return false;
    }
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot create file: " << path.string() << "\n";
        return false;
    }
    file << content;
    file.close();
    return true;
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
        "function(debug, message + string) {\n"
        "    log::debug(message)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(trace, message + string) {\n"
        "    log::trace(message)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(info, message + string) {\n"
        "    log::info(message)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(notice, message + string) {\n"
        "    log::notice(message)\n"
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

std::string model_source(const InitOptions& options) {
    std::string package_name = normalize_identifier(options.table_name, "model");
    std::string class_name = class_name_for(package_name);

    std::ostringstream source;
    source << "package " << package_name << "\n"
           << "\n"
           << "class " << class_name << "(db.Model) {\n"
           << "    id = db.Column(db.Integer, primary_key=True)\n"
           << "    message = db.Column(db.String(255))\n"
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

std::string raw_table_source(const InitOptions& options) {
    std::string package_name = normalize_identifier(options.table_name, "table");
    std::string table_name = package_name;
    if (!table_name.empty() && table_name.back() != 's') {
        table_name += "s";
    }

    std::ostringstream source;
    source << "package " << package_name << "\n"
           << "\n"
           << "function(migrate) {\n"
           << "    sql::table(\"" << table_name << "\", \"id INTEGER PRIMARY KEY, message TEXT\")\n"
           << "    return 1\n"
           << "}\n";
    return source.str();
}

std::string runtime_source(const InitOptions& options) {
    bool model_database = options.use_database && options.table_type == "model";
    std::string table_package = normalize_identifier(options.table_name, "model");

    std::ostringstream source;
    source << "package runtime\n"
           << "\n";

    bool has_imports = false;
    if (options.create_logging) {
        source << "from \"components/logging\" import ";
        source << (options.log_output == "file" ? "to_file" : "to_console");
        source << ", " << options.log_level << "\n";
        has_imports = true;
    }
    if (options.use_database) {
        if (model_database) {
            source << "from \"models/" << table_package << "\" import migrate\n";
        } else {
            source << "from \"database/" << table_package << "\" import migrate\n";
        }
        has_imports = true;
    }
    if (has_imports) {
        source << "\n";
    }

    source << "function(setup) {\n";
    if (options.create_logging) {
        if (options.log_output == "file") {
            source << "    to_file(\"server.log\")\n";
        } else {
            source << "    to_console()\n";
        }
        source << "    " << options.log_level << "(\"starting backend\")\n";
    }
    if (options.use_database) {
        source << "    sql::open(\"app.sqlite\")\n"
               << "    migrate()\n";
    }
    source << "    return 1\n"
           << "}\n";
    return source.str();
}

std::string api_source(const InitOptions& options) {
    bool model_database = options.use_database && options.table_type == "model";
    std::string table_package = normalize_identifier(options.table_name, "model");

    std::ostringstream source;
    source << "package api\n"
           << "\n";

    if (model_database) {
        source << "from \"models/" << table_package << "\" import save\n"
               << "\n";
    }

    source << "function(index) {\n"
           << "    return \"" << options.project_name << " backend is running\"\n"
           << "    return 1\n"
           << "}\n";

    if (options.create_api) {
        source << "\n"
               << "function(api) {\n";
        if (model_database) {
            source << "    if (request::method() == \"POST\") {\n"
                   << "        save(request::json())\n"
                   << "        return json::object(\"status\", \"saved\")\n"
                   << "    }\n";
        }
        source << "    return \"I'm is working\"\n"
               << "    return 1\n"
               << "}\n";
    }

    return source.str();
}

std::string routes_source(const InitOptions& options) {
    std::ostringstream source;
    source << "package routes\n"
           << "\n"
           << "from \"api\" import index";
    if (options.create_api) {
        source << ", api";
    }
    source << "\n"
           << "\n"
           << "function(register) {\n"
           << "    web::route(\"/\", \"index\")\n";
    if (options.create_api) {
        source << "    web::get(\"/api\", \"api\")\n"
               << "    web::post(\"/api\", \"api\")\n";
    }
    source << "    return 1\n"
           << "}\n";
    return source.str();
}

std::string main_source() {
    std::ostringstream source;
    source << "package main\n"
           << "\n"
           << "from \"runtime\" import setup\n"
           << "from \"routes\" import register\n"
           << "\n"
           << "function(main) {\n"
           << "    setup()\n"
           << "    register()\n"
           << "    web::run(\"0.0.0.0\", 5000)\n"
           << "    return 1\n"
           << "}\n"
           << "\n"
           << "return 0\n";
    return source.str();
}

std::string dockerfile_source() {
    std::ostringstream source;
    source << "ARG PGT_IMAGE=" << DEFAULT_PGT_DOCKER_IMAGE << "\n"
           << "FROM ${PGT_IMAGE}\n"
           << "\n"
           << "WORKDIR /app\n"
           << "COPY . .\n"
           << "\n"
           << "EXPOSE 5000\n"
           << "CMD [\"run\", \"main.pgt\"]\n";
    return source.str();
}

std::string compose_source(bool with_nginx) {
    std::ostringstream source;
    source << "services:\n"
           << "  app:\n"
           << "    build:\n"
           << "      context: .\n"
           << "      args:\n"
           << "        PGT_IMAGE: " << DEFAULT_PGT_DOCKER_IMAGE << "\n";
    if (with_nginx) {
        source << "    expose:\n"
               << "      - \"5000\"\n"
               << "  nginx:\n"
               << "    image: nginx:1.27-alpine\n"
               << "    ports:\n"
               << "      - \"80:80\"\n"
               << "    volumes:\n"
               << "      - ./nginx/default.conf:/etc/nginx/conf.d/default.conf:ro\n"
               << "    depends_on:\n"
               << "      - app\n";
    } else {
        source << "    ports:\n"
               << "      - \"5000:5000\"\n";
    }
    return source.str();
}

std::string nginx_source(bool docker) {
    std::string upstream = docker ? "app:5000" : "127.0.0.1:5000";
    std::ostringstream source;
    source << "server {\n"
           << "    listen 80;\n"
           << "\n"
           << "    location / {\n"
           << "        proxy_pass http://" << upstream << ";\n"
           << "        proxy_set_header Host $host;\n"
           << "        proxy_set_header X-Real-IP $remote_addr;\n"
           << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
           << "        proxy_set_header X-Forwarded-Proto $scheme;\n"
           << "    }\n"
           << "}\n";
    return source.str();
}

std::string readme_source(const InitOptions& options) {
    std::string table_package = normalize_identifier(options.table_name, "model");
    std::ostringstream source;
    source << "# " << options.project_name << "\n"
           << "\n"
           << "Generated with `pgt init " << options.template_name << " " << options.project_name << "`.\n"
           << "\n"
           << "## Structure\n"
           << "\n"
           << "- `main.pgt` starts the app and stays intentionally small.\n"
           << "- `runtime/runtime.pgt` prepares logging and database startup.\n"
           << "- `routes/routes.pgt` registers HTTP routes.\n"
           << "- `api/api.pgt` contains request handlers.\n";
    if (options.create_logging) {
        source << "- `components/logging/logging.pgt` wraps log output and log levels.\n";
    }
    if (options.use_database) {
        if (options.table_type == "model") {
            source << "- `models/" << table_package << "/" << table_package << ".pgt` contains the ORM model.\n";
        } else {
            source << "- `database/" << table_package << "/" << table_package << ".pgt` contains raw SQL migration helpers.\n";
        }
    }

    source << "\n"
           << "## Run\n"
           << "\n"
           << "```bash\n"
           << "pgt run main.pgt\n"
           << "```\n";
    if (options.create_api) {
        source << "\n"
               << "## API Examples\n"
               << "\n"
               << "```bash\n"
               << "curl http://localhost:5000/\n"
               << "curl http://localhost:5000/api\n";
        if (options.use_database && options.table_type == "model") {
            source << "curl -X POST http://localhost:5000/api \\\n"
                   << "  -H 'Content-Type: application/json' \\\n"
                   << "  -d '{\"message\":\"hello\"}'\n";
        }
        source << "```\n";
    }
    if (options.create_logging) {
        source << "\n"
               << "## Logging\n"
               << "\n"
               << "Logging is configured in `runtime/runtime.pgt`.\n"
               << "Current output: `" << options.log_output << "`, level: `" << options.log_level << "`.\n";
    }
    if (options.use_database) {
        source << "\n"
               << "## Database\n"
               << "\n"
               << "The app opens `app.sqlite` during startup and runs `migrate()` from `runtime/runtime.pgt`.\n";
    }
    if (options.create_docker) {
        source << "\n"
               << "## Docker\n"
               << "\n"
               << "The Dockerfile uses the PGT runtime image `" << DEFAULT_PGT_DOCKER_IMAGE << "` by default.\n"
               << "\n"
               << "```bash\n"
               << "docker compose up --build\n"
               << "```\n"
               << "\n"
               << "Use another runtime image with:\n"
               << "\n"
               << "```bash\n"
               << "docker compose build --build-arg PGT_IMAGE=your/image:tag\n"
               << "```\n";
    }
    if (options.create_nginx) {
        source << "\n"
               << "## Nginx\n"
               << "\n"
               << "Nginx config lives in `nginx/default.conf` and proxies traffic to the app on port 5000.\n";
    }
    return source.str();
}

bool create_backend_project(const InitOptions& options) {
    std::filesystem::path project_dir = options.project_name;
    if (std::filesystem::exists(project_dir)) {
        std::cerr << "Error: Project already exists: " << project_dir.string() << "\n";
        return false;
    }

    try {
        std::filesystem::create_directories(project_dir);

        if (!write_file(project_dir / "main.pgt", main_source())) return false;
        if (!write_file(project_dir / "runtime" / "runtime.pgt", runtime_source(options))) return false;
        if (!write_file(project_dir / "routes" / "routes.pgt", routes_source(options))) return false;
        if (!write_file(project_dir / "api" / "api.pgt", api_source(options))) return false;
        if (!write_file(project_dir / "README.md", readme_source(options))) return false;

        if (options.create_logging) {
            if (!write_file(project_dir / "components" / "logging" / "logging.pgt",
                            logging_component_source())) return false;
        }

        if (options.use_database) {
            std::string table_package = normalize_identifier(options.table_name, "model");
            if (options.table_type == "model") {
                if (!write_file(project_dir / "models" / table_package / (table_package + ".pgt"),
                                model_source(options))) return false;
            } else {
                if (!write_file(project_dir / "database" / table_package / (table_package + ".pgt"),
                                raw_table_source(options))) return false;
            }
        }

        if (options.create_docker) {
            if (!write_file(project_dir / "Dockerfile", dockerfile_source())) return false;
            if (!write_file(project_dir / "docker-compose.yml",
                            compose_source(options.create_nginx))) return false;
        }

        if (options.create_nginx) {
            if (!write_file(project_dir / "nginx" / "default.conf",
                            nginx_source(options.create_docker))) return false;
        }
    } catch (const std::filesystem::filesystem_error& error) {
        std::cerr << "Error: Cannot create project: " << error.what() << "\n";
        return false;
    }

    std::cout << "Created " << options.template_name << " project: " << project_dir.string() << "\n";
    return true;
}

bool is_supported_template(const std::string& template_name) {
    return template_name == "backend";
}

InitOptions collect_options(int argc, char** argv) {
    InitOptions options;

    if (argc >= 3) {
        options.template_name = lowercase(trim(argv[2]));
    } else {
        options.template_name = prompt_choice("Choose template", {"backend"}, "backend");
    }

    if (argc >= 4) {
        options.project_name = normalize_identifier(argv[3], "project");
    } else {
        options.project_name = normalize_identifier(prompt_text("Project name"), "project");
    }

    options.create_logging = prompt_yes_no("Create logging", true);
    if (options.create_logging) {
        options.log_output = prompt_choice("Log output", {"file", "console"}, "file");
        options.log_level = prompt_choice("Default log level",
                                          {"trace", "debug", "info", "notice", "warn", "error", "critical"},
                                          "info");
    }

    options.use_database = prompt_yes_no("Use database", true);
    if (options.use_database) {
        options.table_type = prompt_choice("Table type", {"model", "sql"}, "model");
        options.table_name = normalize_identifier(prompt_text("Table/model name", "test"), "test");
    }

    options.create_api = prompt_yes_no("Create default /api route", true);
    options.create_docker = prompt_yes_no("Create Dockerfile and docker-compose.yml", false);
    options.create_nginx = prompt_yes_no("Create nginx config", false);

    return options;
}
}

int run_project_init_command(int argc, char** argv) {
    if (argc >= 3) {
        std::string arg = argv[2];
        if (arg == "help" || arg == "--help" || arg == "-h") {
            print_init_help();
            return 0;
        }
        std::string template_name = lowercase(trim(arg));
        if (!is_supported_template(template_name)) {
            std::cerr << "Unknown template: " << template_name << "\n";
            std::cerr << "Available templates: backend\n";
            return 1;
        }
    }

    InitOptions options = collect_options(argc, argv);
    if (!is_supported_template(options.template_name)) {
        std::cerr << "Unknown template: " << options.template_name << "\n";
        std::cerr << "Available templates: backend\n";
        return 1;
    }

    if (options.project_name.empty()) {
        std::cerr << "Error: Project name cannot be empty.\n";
        return 1;
    }

    return create_backend_project(options) ? 0 : 1;
}
