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
    bool create_auth = false;
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

std::string auth_user_model_source() {
    return
        "package user\n"
        "\n"
        "class User(db.Model) {\n"
        "    id = db.Column(db.Integer, primary_key=True)\n"
        "    name = db.Column(db.String(100))\n"
        "    email = db.Column(db.String(160))\n"
        "    password = db.Column(db.String(255))\n"
        "    is_active = db.Column(db.Boolean)\n"
        "}\n";
}

std::string auth_model_helpers_source() {
    return
        "package auth\n"
        "\n"
        "from \"../user\" import User\n"
        "\n"
        "function(migrate_user) {\n"
        "    orm::migrate(\"User\")\n"
        "    return 1\n"
        "}\n";
}

std::string auth_source() {
    return
        "package auth\n"
        "\n"
        "from \"models/user\" import User\n"
        "\n"
        "function(register_page) {\n"
        "    return read::file(\"static/register.html\")\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(login_page) {\n"
        "    return read::file(\"static/login.html\")\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(register_css) {\n"
        "    return read::file(\"static/register.css\")\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(register_js) {\n"
        "    return read::file(\"static/register.js\")\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(login_css) {\n"
        "    return read::file(\"static/login.css\")\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(login_js) {\n"
        "    return read::file(\"static/login.js\")\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(register_user) {\n"
        "    payload + object = request::json()\n"
        "    name + string = json::get(payload, \"name\")\n"
        "    email + string = json::get(payload, \"email\")\n"
        "    password + string = json::get(payload, \"password\")\n"
        "    existing + object = orm::find(\"User\", \"email\", email)\n"
        "    if (json::stringify(existing) != \"{}\") {\n"
        "        return json::object(\"error\", \"email already registered\")\n"
        "    }\n"
        "    password_hash + string = auth::hash_password(password)\n"
        "    orm::save(\"User\", json::object(\"name\", name, \"email\", email, \"password\", password_hash, \"is_active\", true))\n"
        "    return json::object(\"status\", \"registered\", \"email\", email)\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(login_user) {\n"
        "    payload + object = request::json()\n"
        "    email + string = json::get(payload, \"email\")\n"
        "    password + string = json::get(payload, \"password\")\n"
        "    user + object = orm::find(\"User\", \"email\", email)\n"
        "    if (json::stringify(user) != \"{}\") {\n"
        "        password_hash + string = json::get(user, \"password\")\n"
        "        if (auth::verify_password(password, password_hash)) {\n"
        "            token + string = jwt::sign(json::object(\"sub\", json::get(user, \"id\"), \"email\", email), \"change-me-secret\")\n"
        "            return json::object(\"token\", token, \"user\", json::object(\"id\", json::get(user, \"id\"), \"email\", email, \"name\", json::get(user, \"name\")))\n"
        "        }\n"
        "    }\n"
        "    return json::object(\"error\", \"invalid credentials\")\n"
        "    return 1\n"
        "}\n"
        "\n"
        "function(verify_token) {\n"
        "    payload + object = request::json()\n"
        "    token + string = json::get(payload, \"token\")\n"
        "    return json::object(\"valid\", jwt::verify(token, \"change-me-secret\"))\n"
        "    return 1\n"
        "}\n";
}

std::string static_index_source(const InitOptions& options) {
    std::ostringstream source;
    source << "<!doctype html>\n"
           << "<html lang=\"en\">\n"
           << "<head>\n"
           << "    <meta charset=\"utf-8\">\n"
           << "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           << "    <title>" << options.project_name << "</title>\n"
           << "    <link rel=\"stylesheet\" href=\"/static/index.css\">\n"
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
        source << "            <a href=\"/api/v1/docs\">Open Swagger docs</a>\n";
    }
    if (options.create_auth) {
        source << "            <a href=\"/auth/login\">Open auth</a>\n";
    }
    source << "        </section>\n"
           << "    </main>\n"
           << "    <script src=\"/static/index.js\"></script>\n"
           << "</body>\n"
           << "</html>\n";
    return source.str();
}

std::string static_register_source(const InitOptions& options) {
    std::ostringstream source;
    source << "<!doctype html>\n"
           << "<html lang=\"en\">\n"
           << "<head>\n"
           << "    <meta charset=\"utf-8\">\n"
           << "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           << "    <title>" << options.project_name << " Register</title>\n"
           << "    <link rel=\"stylesheet\" href=\"/static/register.css\">\n"
           << "</head>\n"
           << "<body>\n"
           << "    <main>\n"
           << "        <section class=\"hero\">\n"
           << "            <p class=\"eyebrow\">Account</p>\n"
           << "            <h1>Register</h1>\n"
           << "            <form id=\"register-form\" class=\"stack-form\">\n"
           << "                <input name=\"name\" type=\"text\" placeholder=\"Name\" autocomplete=\"name\">\n"
           << "                <input name=\"email\" type=\"email\" placeholder=\"Email\" autocomplete=\"email\">\n"
           << "                <input name=\"password\" type=\"password\" placeholder=\"Password\" autocomplete=\"new-password\">\n"
           << "                <button type=\"submit\">Create account</button>\n"
           << "            </form>\n"
           << "            <pre id=\"auth-response\"></pre>\n"
           << "            <a href=\"/auth/login\">Login</a>\n"
           << "            <a href=\"/\">Back to index</a>\n"
           << "        </section>\n"
           << "    </main>\n"
           << "    <script src=\"/static/register.js\"></script>\n"
           << "</body>\n"
           << "</html>\n";
    return source.str();
}

std::string static_login_source(const InitOptions& options) {
    std::ostringstream source;
    source << "<!doctype html>\n"
           << "<html lang=\"en\">\n"
           << "<head>\n"
           << "    <meta charset=\"utf-8\">\n"
           << "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           << "    <title>" << options.project_name << " Login</title>\n"
           << "    <link rel=\"stylesheet\" href=\"/static/login.css\">\n"
           << "</head>\n"
           << "<body>\n"
           << "    <main>\n"
           << "        <section class=\"hero\">\n"
           << "            <p class=\"eyebrow\">Account</p>\n"
           << "            <h1>Login</h1>\n"
           << "            <form id=\"login-form\" class=\"stack-form\">\n"
           << "                <input name=\"email\" type=\"email\" placeholder=\"Email\" autocomplete=\"email\">\n"
           << "                <input name=\"password\" type=\"password\" placeholder=\"Password\" autocomplete=\"current-password\">\n"
           << "                <button type=\"submit\">Sign in</button>\n"
           << "            </form>\n"
           << "            <pre id=\"auth-response\"></pre>\n"
           << "            <a href=\"/auth/register\">Register</a>\n"
           << "            <a href=\"/\">Back to index</a>\n"
           << "        </section>\n"
           << "    </main>\n"
           << "    <script src=\"/static/login.js\"></script>\n"
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
        "    margin-right: 16px;\n"
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

std::string static_auth_css_source() {
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
        "    width: min(920px, 100%);\n"
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
        ".auth-grid {\n"
        "    display: grid;\n"
        "    grid-template-columns: repeat(2, minmax(0, 1fr));\n"
        "    gap: 18px;\n"
        "    margin-top: 24px;\n"
        "}\n"
        "\n"
        ".stack-form {\n"
        "    display: grid;\n"
        "    gap: 12px;\n"
        "}\n"
        "\n"
        "h2 {\n"
        "    margin: 0 0 4px;\n"
        "    font-size: 22px;\n"
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
        "}\n"
        "\n"
        "@media (max-width: 720px) {\n"
        "    main {\n"
        "        padding: 18px;\n"
        "    }\n"
        "\n"
        "    .hero {\n"
        "        padding: 24px;\n"
        "    }\n"
        "\n"
        "    .auth-grid {\n"
        "        grid-template-columns: 1fr;\n"
        "    }\n"
        "\n"
        "    h1 {\n"
        "        font-size: 34px;\n"
        "    }\n"
        "}\n";
}

std::string static_register_js_source() {
    return
        "const registerForm = document.querySelector('#register-form');\n"
        "const authResponse = document.querySelector('#auth-response');\n"
        "\n"
        "async function submitJson(formElement, url) {\n"
        "    const payload = Object.fromEntries(new FormData(formElement).entries());\n"
        "    const result = await fetch(url, {\n"
        "        method: 'POST',\n"
        "        headers: { 'Content-Type': 'application/json' },\n"
        "        body: JSON.stringify(payload)\n"
        "    });\n"
        "    return result.text();\n"
        "}\n"
        "\n"
        "if (registerForm && authResponse) {\n"
        "    registerForm.addEventListener('submit', async (event) => {\n"
        "        event.preventDefault();\n"
        "        authResponse.textContent = await submitJson(registerForm, '/auth/register');\n"
        "        registerForm.reset();\n"
        "    });\n"
        "}\n";
}

std::string static_login_js_source() {
    return
        "const loginForm = document.querySelector('#login-form');\n"
        "const authResponse = document.querySelector('#auth-response');\n"
        "\n"
        "async function submitJson(formElement, url) {\n"
        "    const payload = Object.fromEntries(new FormData(formElement).entries());\n"
        "    const result = await fetch(url, {\n"
        "        method: 'POST',\n"
        "        headers: { 'Content-Type': 'application/json' },\n"
        "        body: JSON.stringify(payload)\n"
        "    });\n"
        "    return result.text();\n"
        "}\n"
        "\n"
        "if (loginForm && authResponse) {\n"
        "    loginForm.addEventListener('submit', async (event) => {\n"
        "        event.preventDefault();\n"
        "        authResponse.textContent = await submitJson(loginForm, '/auth/login');\n"
        "        loginForm.reset();\n"
        "    });\n"
        "}\n";
}

std::string swagger_html_source(const InitOptions& options) {
    std::ostringstream source;
    source << "<!doctype html>\n"
           << "<html lang=\"en\">\n"
           << "<head>\n"
           << "    <meta charset=\"utf-8\">\n"
           << "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           << "    <title>" << options.project_name << " Swagger</title>\n"
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
               << "      summary: Swagger UI\n"
               << "      responses:\n"
               << "        \"200\":\n"
               << "          description: Swagger documentation\n"
               << "  /api/v1/openapi.yaml:\n"
               << "    get:\n"
               << "      summary: OpenAPI YAML\n"
               << "      responses:\n"
               << "        \"200\":\n"
               << "          description: API specification\n";
    }
    if (options.create_auth) {
        source << "  /auth/register:\n"
               << "    post:\n"
               << "      summary: Register user\n"
               << "      responses:\n"
               << "        \"200\":\n"
               << "          description: Registered\n"
               << "  /auth/login:\n"
               << "    post:\n"
               << "      summary: Login user and return JWT\n"
               << "      responses:\n"
               << "        \"200\":\n"
               << "          description: JWT token\n"
               << "  /auth/verify:\n"
               << "    post:\n"
               << "      summary: Verify JWT token\n"
               << "      responses:\n"
               << "        \"200\":\n"
               << "          description: Token status\n";
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
    if (options.create_auth) {
        source << "from \"models/auth\" import migrate_user\n";
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
    if (options.use_database || options.create_auth) {
        source << "    sql::open(\"app.sqlite\")\n";
        if (options.use_database) {
            source << "    migrate()\n";
        }
        if (options.create_auth) {
            source << "    migrate_user()\n";
        }
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
               << "function(index_css) {\n"
               << "    return read::file(\"static/index.css\")\n"
               << "    return 1\n"
               << "}\n"
               << "\n"
               << "function(index_js) {\n"
               << "    return read::file(\"static/index.js\")\n"
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
        source << ", index_css, index_js";
    }
    source << "\n";
    if (options.create_api_spec) {
        source << "from \"sweiger\" import docs, openapi_yaml\n";
    }
    if (options.create_auth) {
        source << "from \"auth\" import register_page, login_page, register_css, register_js, login_css, login_js, register_user, login_user, verify_token\n";
    }
    source << "\n"
           << "function(register) {\n"
           << "    web::route(\"/\", \"index\")\n";
    if (options.create_api) {
        source << "    web::get(\"/api\", \"api\")\n"
               << "    web::post(\"/api\", \"api\")\n";
    }
    if (options.create_static) {
        source << "    web::get(\"/static/index.css\", \"index_css\")\n"
               << "    web::get(\"/static/index.js\", \"index_js\")\n";
    }
    if (options.create_api_spec) {
        source << "    web::get(\"/api/v1/docs\", \"docs\")\n"
               << "    web::get(\"/api/v1/openapi.yaml\", \"openapi_yaml\")\n";
    }
    if (options.create_auth) {
        source << "    web::get(\"/static/register.css\", \"register_css\")\n"
               << "    web::get(\"/static/register.js\", \"register_js\")\n"
               << "    web::get(\"/static/login.css\", \"login_css\")\n"
               << "    web::get(\"/static/login.js\", \"login_js\")\n"
               << "    web::get(\"/auth/register\", \"register_page\")\n"
               << "    web::get(\"/auth/login\", \"login_page\")\n"
               << "    web::post(\"/auth/register\", \"register_user\")\n"
               << "    web::post(\"/auth/login\", \"login_user\")\n"
               << "    web::post(\"/auth/verify\", \"verify_token\")\n";
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

std::string pgt_mod_source(const InitOptions& options) {
    std::ostringstream source;
    source << "module " << options.project_name << "\n"
           << "\n"
           << "require (\n"
           << ")\n";
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
        source << "- `sweiger/index.html` contains Swagger UI served from `/api/v1/docs`.\n"
               << "- `sweiger/sweiger.pgt` contains Swagger route handlers.\n"
               << "- `api.yaml` contains the editable API specification served from `/api/v1/openapi.yaml`.\n";
    }
    if (options.create_logging) {
        source << "- `components/logging/logging.pgt` wraps log output and log levels.\n";
    }
    if (options.create_auth) {
        source << "- `auth/auth.pgt` contains register, login, and JWT verify handlers.\n"
               << "- `models/user/user.pgt` contains the generated auth user model.\n"
               << "- `models/auth/auth.pgt` contains auth migration and lookup helpers.\n";
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
            source << "curl http://localhost:5000/api/v1/docs\n"
                   << "curl http://localhost:5000/api/v1/openapi.yaml\n";
        }
        if (options.use_database && options.table_type == "model") {
            source << "curl -X POST http://localhost:5000/api \\\n"
                   << "  -H 'Content-Type: application/json' \\\n"
                   << "  -d '{\"message\":\"hello\"}'\n";
        }
        if (options.create_auth) {
            source << "curl -X POST http://localhost:5000/auth/register \\\n"
                   << "  -H 'Content-Type: application/json' \\\n"
                   << "  -d '{\"name\":\"Pabla\",\"email\":\"pabla@example.com\",\"password\":\"secret\"}'\n"
                   << "curl -X POST http://localhost:5000/auth/login \\\n"
                   << "  -H 'Content-Type: application/json' \\\n"
                   << "  -d '{\"email\":\"pabla@example.com\",\"password\":\"secret\"}'\n";
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
               << "Open `/api/v1/docs` for Swagger UI. Edit `api.yaml` when you add routes; the backend serves it at `/api/v1/openapi.yaml`.\n";
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
    if (options.create_auth) {
        source << "\n"
               << "## Auth\n"
               << "\n"
               << "Auth routes are `/auth/register`, `/auth/login`, and `/auth/verify`.\n"
               << "Change the JWT secret in `auth/auth.pgt` before production.\n";
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
        if (!write_file(project_dir / "pgt.mod", pgt_mod_source(options))) return false;
        if (!write_file(project_dir / "runtime" / "runtime.pgt", runtime_source(options))) return false;
        if (!write_file(project_dir / "routes" / "routes.pgt", routes_source(options))) return false;
        if (!write_file(project_dir / "api" / "api.pgt", api_source(options))) return false;
        if (!write_file(project_dir / "README.md", readme_source(options))) return false;

        if (options.create_auth) {
            if (!write_file(project_dir / "auth" / "auth.pgt", auth_source())) return false;
            if (!write_file(project_dir / "models" / "user" / "user.pgt",
                            auth_user_model_source())) return false;
            if (!write_file(project_dir / "models" / "auth" / "auth.pgt",
                            auth_model_helpers_source())) return false;
        }

        if (options.create_static) {
            if (!write_file(project_dir / "static" / "index.html",
                            static_index_source(options))) return false;
            if (options.create_auth) {
                if (!write_file(project_dir / "static" / "register.html",
                                static_register_source(options))) return false;
                if (!write_file(project_dir / "static" / "register.css",
                                static_auth_css_source())) return false;
                if (!write_file(project_dir / "static" / "register.js",
                                static_register_js_source())) return false;
                if (!write_file(project_dir / "static" / "login.html",
                                static_login_source(options))) return false;
                if (!write_file(project_dir / "static" / "login.css",
                                static_auth_css_source())) return false;
                if (!write_file(project_dir / "static" / "login.js",
                                static_login_js_source())) return false;
            }
            if (!write_file(project_dir / "static" / "index.css",
                            static_css_source())) return false;
            if (!write_file(project_dir / "static" / "index.js",
                            static_js_source(options))) return false;
        }

        if (options.create_api_spec) {
            if (!write_file(project_dir / "api.yaml", api_spec_source(options))) return false;
            if (!write_file(project_dir / "sweiger" / "sweiger.pgt",
                            swagger_package_source())) return false;
            if (!write_file(project_dir / "sweiger" / "index.html",
                            swagger_html_source(options))) return false;
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
    options.create_auth = prompt_yes_no("Create auth/JWT template", false);
    options.create_static = prompt_yes_no("Create static html/css/js", true);
    if (options.create_auth) {
        options.create_static = true;
    }
    options.create_api_spec = prompt_yes_no("Create api.yaml and Swagger /api/v1/docs", true);
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
