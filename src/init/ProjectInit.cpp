#include "ProjectInit.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <termios.h>
#include <unistd.h>

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
    bool create_static = true;
    bool create_api_spec = true;
    bool create_docker = false;
    bool create_nginx = false;
};

const char* DEFAULT_PGT_DOCKER_IMAGE = "pablaofficeal/pgt-language:latest";

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
    std::cout << "  backend  Create a modular backend project with optional logging, database, API, static files, Docker, and nginx\n\n";
    std::cout << "Interactive choices support Up/Down arrows and Enter.\n\n";
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

std::string prompt_choice(const std::string& question,
                          const std::vector<std::string>& choices,
                          const std::string& default_value);

bool prompt_yes_no(const std::string& question, bool default_value) {
    return prompt_choice(question, {"yes", "no"}, default_value ? "yes" : "no") == "yes";
}

std::string prompt_choice(const std::string& question,
                          const std::vector<std::string>& choices,
                          const std::string& default_value) {
    if (choices.empty()) {
        return default_value;
    }

    size_t selected = 0;
    for (size_t i = 0; i < choices.size(); ++i) {
        if (choices[i] == default_value) {
            selected = i;
            break;
        }
    }

    if (isatty(STDIN_FILENO)) {
        termios original {};
        bool raw_enabled = tcgetattr(STDIN_FILENO, &original) == 0;
        if (raw_enabled) {
            termios raw = original;
            raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;
            raw_enabled = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
        }

        auto render = [&]() {
            std::cout << "\033[?25l";
            std::cout << question << "\n";
            for (size_t i = 0; i < choices.size(); ++i) {
                std::cout << (i == selected ? "> " : "  ") << choices[i] << "\n";
            }
            std::cout.flush();
        };

        if (raw_enabled) {
            render();
            while (true) {
                char ch = 0;
                if (read(STDIN_FILENO, &ch, 1) != 1) {
                    break;
                }
                if (ch == '\n' || ch == '\r') {
                    break;
                }
                if (choices.size() == 2 && choices[0] == "yes" && choices[1] == "no" &&
                    (ch == 'y' || ch == 'd')) {
                    selected = 0;
                    break;
                }
                if (choices.size() == 2 && choices[0] == "yes" && choices[1] == "no" && ch == 'n') {
                    selected = 1;
                    break;
                }
                if (ch >= '1' && ch <= '9') {
                    size_t index = static_cast<size_t>(ch - '1');
                    if (index < choices.size()) {
                        selected = index;
                        break;
                    }
                }
                if (ch == '\033') {
                    char seq[2] = {0, 0};
                    if (read(STDIN_FILENO, &seq[0], 1) != 1 ||
                        read(STDIN_FILENO, &seq[1], 1) != 1) {
                        continue;
                    }
                    if (seq[0] == '[' && seq[1] == 'A') {
                        selected = selected == 0 ? choices.size() - 1 : selected - 1;
                    } else if (seq[0] == '[' && seq[1] == 'B') {
                        selected = (selected + 1) % choices.size();
                    }
                } else if (ch == 'k' || ch == 'w') {
                    selected = selected == 0 ? choices.size() - 1 : selected - 1;
                } else if (ch == 'j' || ch == 's') {
                    selected = (selected + 1) % choices.size();
                }

                std::cout << "\033[" << (choices.size() + 1) << "A";
                render();
            }

            tcsetattr(STDIN_FILENO, TCSANOW, &original);
            std::cout << "\033[?25h";
            std::cout << "\033[" << (choices.size() + 1) << "A";
            std::cout << "\033[J";
            std::cout << question << ": " << choices[selected] << "\n";
            return choices[selected];
        }
    }

    std::ostringstream choices_text;
    for (size_t i = 0; i < choices.size(); ++i) {
        if (i > 0) {
            choices_text << "/";
        }
        choices_text << choices[i];
    }

    while (true) {
        std::string answer = lowercase(prompt_text(question + " (" + choices_text.str() + ")", default_value));
        if (choices.size() == 2 && choices[0] == "yes" && choices[1] == "no") {
            if (answer == "y" || answer == "d" || answer == "yes" || answer == "da" || answer == "да") {
                return "yes";
            }
            if (answer == "n" || answer == "no" || answer == "net" || answer == "нет") {
                return "no";
            }
        }
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
           << "}\n";
    return source.str();
}

std::string orm_init_source(const InitOptions& options) {
    std::string package_name = normalize_identifier(options.table_name, "model");
    std::string class_name = class_name_for(package_name);

    std::ostringstream source;
    source << "package init\n"
           << "\n"
           << "from \"../" << package_name << "\" import " << class_name << "\n"
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

std::string static_index_source(const InitOptions& options) {
    std::ostringstream source;
    source << "<!doctype html>\n"
           << "<html lang=\"en\">\n"
           << "<head>\n"
           << "    <meta charset=\"utf-8\">\n"
           << "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           << "    <title>" << options.project_name << "</title>\n"
           << "    <link rel=\"stylesheet\" href=\"/static/styles.css\">\n"
           << "</head>\n"
           << "<body>\n"
           << "    <main>\n"
           << "        <section class=\"hero\">\n"
           << "            <p class=\"eyebrow\">PGT backend</p>\n"
           << "            <h1>" << options.project_name << "</h1>\n"
           << "            <p id=\"status\">Backend is ready.</p>\n";
    if (options.create_api) {
        source << "            <form id=\"message-form\">\n"
               << "                <input id=\"message\" name=\"message\" type=\"text\" placeholder=\"Message\" autocomplete=\"off\">\n"
               << "                <button type=\"submit\">Send</button>\n"
               << "            </form>\n"
               << "            <pre id=\"response\"></pre>\n";
    }
    if (options.create_api_spec) {
        source << "            <a href=\"/api/v1/docs\">Open api.yaml</a>\n";
    }
    source << "        </section>\n"
           << "    </main>\n"
           << "    <script src=\"/static/app.js\"></script>\n"
           << "</body>\n"
           << "</html>\n";
    return source.str();
}

std::string static_css_source() {
    return
        ":root {\n"
        "    color-scheme: light;\n"
        "    font-family: Inter, Arial, sans-serif;\n"
        "    background: #f5f7fb;\n"
        "    color: #18202f;\n"
        "}\n"
        "\n"
        "* {\n"
        "    box-sizing: border-box;\n"
        "}\n"
        "\n"
        "body {\n"
        "    margin: 0;\n"
        "}\n"
        "\n"
        "main {\n"
        "    min-height: 100vh;\n"
        "    display: grid;\n"
        "    place-items: center;\n"
        "    padding: 32px;\n"
        "}\n"
        "\n"
        ".hero {\n"
        "    width: min(680px, 100%);\n"
        "    padding: 32px;\n"
        "    background: #ffffff;\n"
        "    border: 1px solid #dce3ee;\n"
        "    border-radius: 8px;\n"
        "    box-shadow: 0 18px 45px rgba(24, 32, 47, 0.08);\n"
        "}\n"
        "\n"
        ".eyebrow {\n"
        "    margin: 0 0 8px;\n"
        "    color: #4d647f;\n"
        "    font-size: 14px;\n"
        "    font-weight: 700;\n"
        "    text-transform: uppercase;\n"
        "}\n"
        "\n"
        "h1 {\n"
        "    margin: 0;\n"
        "    font-size: 44px;\n"
        "    line-height: 1.1;\n"
        "}\n"
        "\n"
        "form {\n"
        "    display: flex;\n"
        "    gap: 12px;\n"
        "    margin-top: 24px;\n"
        "}\n"
        "\n"
        "input,\n"
        "button {\n"
        "    min-height: 44px;\n"
        "    border-radius: 8px;\n"
        "    font: inherit;\n"
        "}\n"
        "\n"
        "input {\n"
        "    flex: 1;\n"
        "    min-width: 0;\n"
        "    border: 1px solid #b9c5d4;\n"
        "    padding: 0 14px;\n"
        "}\n"
        "\n"
        "button {\n"
        "    border: 0;\n"
        "    padding: 0 18px;\n"
        "    background: #156d72;\n"
        "    color: #ffffff;\n"
        "    font-weight: 700;\n"
        "    cursor: pointer;\n"
        "}\n"
        "\n"
        "pre {\n"
        "    overflow: auto;\n"
        "    margin-top: 18px;\n"
        "    padding: 16px;\n"
        "    background: #18202f;\n"
        "    color: #ecf3ff;\n"
        "    border-radius: 8px;\n"
        "}\n"
        "\n"
        "a {\n"
        "    display: inline-block;\n"
        "    margin-top: 18px;\n"
        "    color: #156d72;\n"
        "    font-weight: 700;\n"
        "}\n";
}

std::string static_js_source(const InitOptions& options) {
    std::ostringstream source;
    source << "const form = document.querySelector('#message-form');\n"
           << "const response = document.querySelector('#response');\n"
           << "const statusLine = document.querySelector('#status');\n"
           << "\n"
           << "if (statusLine) {\n"
           << "    statusLine.textContent = 'Backend is ready.';\n"
           << "}\n";
    if (options.create_api) {
        source << "\n"
               << "if (form && response) {\n"
               << "    form.addEventListener('submit', async (event) => {\n"
               << "        event.preventDefault();\n"
               << "        const message = new FormData(form).get('message') || '';\n"
               << "        const result = await fetch('/api', {\n"
               << "            method: 'POST',\n"
               << "            headers: { 'Content-Type': 'application/json' },\n"
               << "            body: JSON.stringify({ message })\n"
               << "        });\n"
               << "        response.textContent = await result.text();\n"
               << "        form.reset();\n"
               << "    });\n"
               << "}\n";
    }
    return source.str();
}

std::string api_spec_source(const InitOptions& options) {
    std::ostringstream source;
    source << "openapi: 3.0.3\n"
           << "info:\n"
           << "  title: " << options.project_name << " API\n"
           << "  version: 0.1.0\n"
           << "paths:\n"
           << "  /:\n"
           << "    get:\n"
           << "      summary: Index route\n"
           << "      responses:\n"
           << "        \"200\":\n"
           << "          description: OK\n";
    if (options.create_api) {
        source << "  /api:\n"
               << "    get:\n"
               << "      summary: Default API status\n"
               << "      responses:\n"
               << "        \"200\":\n"
               << "          description: OK\n"
               << "    post:\n"
               << "      summary: Save JSON payload\n"
               << "      requestBody:\n"
               << "        required: true\n"
               << "        content:\n"
               << "          application/json:\n"
               << "            schema:\n"
               << "              type: object\n"
               << "      responses:\n"
               << "        \"200\":\n"
               << "          description: Saved\n";
    }
    if (options.create_api_spec) {
        source << "  /api/v1/docs:\n"
               << "    get:\n"
               << "      summary: OpenAPI YAML\n"
               << "      responses:\n"
               << "        \"200\":\n"
               << "          description: API specification\n";
    }
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
            source << "from \"models/init\" import migrate\n";
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

    std::ostringstream source;
    source << "package api\n"
           << "\n";

    if (model_database && options.create_api) {
        source << "from \"models/init\" import save\n"
               << "\n";
    }

    source << "function(index) {\n";
    if (options.create_static) {
        source << "    return read::file(\"static/index.html\")\n";
    } else {
        source << "    return \"" << options.project_name << " backend is running\"\n";
    }
    source << "    return 1\n"
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
        source << "    return \"I am working\"\n"
               << "    return 1\n"
               << "}\n";
    }

    if (options.create_static) {
        source << "\n"
               << "function(static_css) {\n"
               << "    return read::file(\"static/styles.css\")\n"
               << "    return 1\n"
               << "}\n"
               << "\n"
               << "function(static_js) {\n"
               << "    return read::file(\"static/app.js\")\n"
               << "    return 1\n"
               << "}\n";
    }

    if (options.create_api_spec) {
        source << "\n"
               << "function(docs) {\n"
               << "    return read::file(\"api.yaml\")\n"
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
    if (options.create_static) {
        source << ", static_css, static_js";
    }
    if (options.create_api_spec) {
        source << ", docs";
    }
    source << "\n"
           << "\n"
           << "function(register) {\n"
           << "    web::route(\"/\", \"index\")\n";
    if (options.create_api) {
        source << "    web::get(\"/api\", \"api\")\n"
               << "    web::post(\"/api\", \"api\")\n";
    }
    if (options.create_static) {
        source << "    web::get(\"/static/styles.css\", \"static_css\")\n"
               << "    web::get(\"/static/app.js\", \"static_js\")\n";
    }
    if (options.create_api_spec) {
        source << "    web::get(\"/api/v1/docs\", \"docs\")\n";
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
    if (options.create_static) {
        source << "- `static/index.html`, `static/styles.css`, and `static/app.js` contain frontend assets.\n";
    }
    if (options.create_api_spec) {
        source << "- `api.yaml` contains the editable API specification served from `/api/v1/docs`.\n";
    }
    if (options.create_logging) {
        source << "- `components/logging/logging.pgt` wraps log output and log levels.\n";
    }
    if (options.use_database) {
        if (options.table_type == "model") {
            source << "- `models/" << table_package << "/" << table_package << ".pgt` contains the ORM model.\n"
                   << "- `models/init/init-db.pgt` runs ORM migration and save helpers.\n";
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
        if (options.create_api_spec) {
            source << "curl http://localhost:5000/api/v1/docs\n";
        }
        if (options.use_database && options.table_type == "model") {
            source << "curl -X POST http://localhost:5000/api \\\n"
                   << "  -H 'Content-Type: application/json' \\\n"
                   << "  -d '{\"message\":\"hello\"}'\n";
        }
        source << "```\n";
    }
    if (options.create_static) {
        source << "\n"
               << "## Static Files\n"
               << "\n"
               << "The root route returns `static/index.html`. Edit the files in `static/` to change the page, styles, and browser script.\n";
    }
    if (options.create_api_spec) {
        source << "\n"
               << "## API Spec\n"
               << "\n"
               << "Edit `api.yaml` when you add routes. The backend serves the current file at `/api/v1/docs`.\n";
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

        if (options.create_static) {
            if (!write_file(project_dir / "static" / "index.html",
                            static_index_source(options))) return false;
            if (!write_file(project_dir / "static" / "styles.css",
                            static_css_source())) return false;
            if (!write_file(project_dir / "static" / "app.js",
                            static_js_source(options))) return false;
        }

        if (options.create_api_spec) {
            if (!write_file(project_dir / "api.yaml", api_spec_source(options))) return false;
        }

        if (options.create_logging) {
            if (!write_file(project_dir / "components" / "logging" / "logging.pgt",
                            logging_component_source())) return false;
        }

        if (options.use_database) {
            std::string table_package = normalize_identifier(options.table_name, "model");
            if (options.table_type == "model") {
                if (!write_file(project_dir / "models" / table_package / (table_package + ".pgt"),
                                model_source(options))) return false;
                if (!write_file(project_dir / "models" / "init" / "init-db.pgt",
                                orm_init_source(options))) return false;
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
    options.create_static = prompt_yes_no("Create static html/css/js", true);
    options.create_api_spec = prompt_yes_no("Create api.yaml and /api/v1/docs", true);
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
