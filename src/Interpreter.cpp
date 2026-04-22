#include "Interpreter.h"
#include "Utils.h"
#include "Error.h"
#include <iostream>
#include <limits>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <cctype>
#include <ctime>
#include <sstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <sqlite3.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

struct FunctionReturn {
    Value value;
    bool has_expr;
};

static bool path_ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string response_content_type_for_path(const std::string& path) {
    if (path == "/api/v1/docs" || path_ends_with(path, ".yaml") || path_ends_with(path, ".yml")) {
        return "application/yaml; charset=utf-8";
    }
    if (path_ends_with(path, ".html")) {
        return "text/html; charset=utf-8";
    }
    if (path_ends_with(path, ".css")) {
        return "text/css; charset=utf-8";
    }
    if (path_ends_with(path, ".js")) {
        return "application/javascript; charset=utf-8";
    }
    if (path_ends_with(path, ".json")) {
        return "application/json; charset=utf-8";
    }
    if (path_ends_with(path, ".svg")) {
        return "image/svg+xml";
    }
    return "";
}

Interpreter::~Interpreter() {
    if (sqlite_db) {
        sqlite3_close(sqlite_db);
        sqlite_db = nullptr;
    }
}

bool Interpreter::is_truthy(const Value& value) const {
    if (value.type == ValueType::INT) {
        return value.int_val != 0;
    }
    if (value.type == ValueType::BOOL) {
        return value.bool_val;
    }
    if (value.type == ValueType::FLOAT) {
        return value.float_val != 0.0;
    }
    if (value.type == ValueType::STRING) {
        return !value.str_val.empty();
    }
    if (value.type == ValueType::BYTES) {
        return !value.str_val.empty();
    }
    if (value.type == ValueType::OBJECT) {
        return !value.obj_val.empty();
    }
    if (value.type == ValueType::ARRAY) {
        return !value.arr_val.empty();
    }
    return false;
}

Value Interpreter::coerce_value(const Value& value, const std::string& type_name, const SourceLocation& loc) const {
    if (type_name == "int") {
        if (value.type == ValueType::INT) return value;
        if (value.type == ValueType::BOOL) return Value(value.bool_val ? 1LL : 0LL);
        if (value.type == ValueType::FLOAT) return Value(static_cast<long long>(value.float_val));
    }
    if (type_name == "float") {
        if (value.type == ValueType::FLOAT) return value;
        if (value.type == ValueType::INT) return Value(static_cast<double>(value.int_val));
        if (value.type == ValueType::BOOL) return Value(value.bool_val ? 1.0 : 0.0);
    }
    if (type_name == "string") {
        if (value.type == ValueType::STRING) return value;
        if (value.type == ValueType::BYTES) return Value(value.str_val);
    }
    if (type_name == "bool") {
        if (value.type == ValueType::BOOL) return value;
        if (value.type == ValueType::INT) return Value::Bool(value.int_val != 0);
        if (value.type == ValueType::FLOAT) return Value::Bool(value.float_val != 0.0);
        if (value.type == ValueType::STRING || value.type == ValueType::BYTES) {
            return Value::Bool(!value.str_val.empty());
        }
    }
    if (type_name == "bytes") {
        if (value.type == ValueType::BYTES) return value;
        if (value.type == ValueType::STRING) return Value::Bytes(value.str_val);
    }
    if (type_name == "object") {
        if (value.type == ValueType::OBJECT) return value;
    }
    if (type_name == "array") {
        if (value.type == ValueType::ARRAY) return value;
    }

    throw TypeError("Cannot convert value to type '" + type_name + "'", loc);
}

void Interpreter::assign_value(const std::string& name, const Value& value, std::map<std::string, Value>& locals) {
    if (globals.count(name)) {
        globals[name] = value;
    } else {
        locals[name] = value;
    }
}

Interpreter::ParsedUrl Interpreter::parse_url(const std::string& url, const SourceLocation& loc) const {
    ParsedUrl parsed;

    size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        throw RuntimeError("Invalid URL, expected scheme://host/path", loc);
    }

    parsed.scheme = url.substr(0, scheme_end);
    size_t host_start = scheme_end + 3;
    size_t path_start = url.find('/', host_start);
    std::string host_port = path_start == std::string::npos
        ? url.substr(host_start)
        : url.substr(host_start, path_start - host_start);

    if (host_port.empty()) {
        throw RuntimeError("Invalid URL, missing host", loc);
    }

    size_t colon_pos = host_port.rfind(':');
    if (colon_pos != std::string::npos) {
        parsed.host = host_port.substr(0, colon_pos);
        parsed.port = host_port.substr(colon_pos + 1);
    } else {
        parsed.host = host_port;
        parsed.port = parsed.scheme == "https" ? "443" : "80";
    }

    parsed.path = path_start == std::string::npos ? "/" : url.substr(path_start);
    if (parsed.path.empty()) {
        parsed.path = "/";
    }

    return parsed;
}

std::string Interpreter::extract_http_body(const std::string& response) const {
    size_t header_end = response.find("\r\n\r\n");
    if (header_end != std::string::npos) {
        return response.substr(header_end + 4);
    }
    header_end = response.find("\n\n");
    if (header_end != std::string::npos) {
        return response.substr(header_end + 2);
    }
    return response;
}

std::string Interpreter::perform_http_request(const std::string& transport, const std::string& method,
                                              const std::string& url, const std::string& body,
                                              const SourceLocation& loc) const {
    ParsedUrl parsed = parse_url(url, loc);
    if (!transport.empty() && transport != parsed.scheme) {
        throw RuntimeError("Network transport and URL scheme must match", loc);
    }
    if (parsed.scheme != "http" && parsed.scheme != "https") {
        throw RuntimeError("Only HTTP and HTTPS are supported in the current Linux network stack", loc);
    }
    bool use_tls = (parsed.scheme == "https");

    struct addrinfo hints {};
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = nullptr;
    int status = getaddrinfo(parsed.host.c_str(), parsed.port.c_str(), &hints, &result);
    if (status != 0) {
        throw RuntimeError("Failed to resolve host '" + parsed.host + "': " + gai_strerror(status), loc);
    }

    int sockfd = -1;
    for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) {
            continue;
        }
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(sockfd);
        sockfd = -1;
    }
    freeaddrinfo(result);

    if (sockfd == -1) {
        throw RuntimeError("Failed to connect to '" + parsed.host + ":" + parsed.port + "'", loc);
    }

    std::string request = method + " " + parsed.path + " HTTP/1.1\r\n";
    request += "Host: " + parsed.host + "\r\n";
    request += "User-Agent: PGT/0.1\r\n";
    request += "Connection: close\r\n";

    if (method == "POST") {
        request += "Content-Type: text/plain; charset=utf-8\r\n";
        request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    request += "\r\n";
    if (method == "POST") {
        request += body;
    }

    SSL_CTX* ssl_ctx = nullptr;
    SSL* ssl = nullptr;

    if (use_tls) {
        OPENSSL_init_ssl(0, nullptr);
        ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx) {
            close(sockfd);
            throw RuntimeError("Failed to initialize TLS context", loc);
        }

        bool certs_loaded = (SSL_CTX_set_default_verify_paths(ssl_ctx) == 1);
        const char* cert_files[] = {
            "/etc/ssl/certs/ca-certificates.crt",
            "/etc/ssl/cert.pem",
            "/etc/pki/tls/certs/ca-bundle.crt",
            "/etc/ssl/ca-bundle.pem"
        };
        const char* cert_dirs[] = {
            "/etc/ssl/certs",
            "/etc/pki/tls/certs"
        };

        for (const char* cert_file : cert_files) {
            if (!certs_loaded && std::filesystem::exists(cert_file)) {
                certs_loaded = (SSL_CTX_load_verify_locations(ssl_ctx, cert_file, nullptr) == 1);
            }
        }
        for (const char* cert_dir : cert_dirs) {
            if (!certs_loaded && std::filesystem::exists(cert_dir)) {
                certs_loaded = (SSL_CTX_load_verify_locations(ssl_ctx, nullptr, cert_dir) == 1);
            }
        }

        if (!certs_loaded) {
            SSL_CTX_free(ssl_ctx);
            close(sockfd);
            throw RuntimeError("Failed to load Linux CA certificates. Install/update ca-certificates for your system.", loc);
        }

        ssl = SSL_new(ssl_ctx);
        if (!ssl) {
            SSL_CTX_free(ssl_ctx);
            close(sockfd);
            throw RuntimeError("Failed to create TLS session", loc);
        }

        SSL_set_tlsext_host_name(ssl, parsed.host.c_str());
        SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, nullptr);
        X509_VERIFY_PARAM* verify_params = SSL_get0_param(ssl);
        X509_VERIFY_PARAM_set1_host(verify_params, parsed.host.c_str(), 0);

        if (SSL_set_fd(ssl, sockfd) != 1) {
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            close(sockfd);
            throw RuntimeError("Failed to bind TLS session to socket", loc);
        }

        if (SSL_connect(ssl) != 1) {
            long verify_result = SSL_get_verify_result(ssl);
            unsigned long err = ERR_get_error();
            std::string err_msg;
            if (verify_result != X509_V_OK) {
                err_msg = X509_verify_cert_error_string(verify_result);
            } else if (err != 0) {
                err_msg = ERR_error_string(err, nullptr);
            } else {
                err_msg = "TLS handshake failed";
            }
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            close(sockfd);
            throw RuntimeError("Failed to establish TLS connection: " + err_msg, loc);
        }
    }

    size_t total_sent = 0;
    while (total_sent < request.size()) {
        int sent = 0;
        if (use_tls) {
            sent = SSL_write(ssl, request.data() + total_sent, static_cast<int>(request.size() - total_sent));
        } else {
            sent = static_cast<int>(send(sockfd, request.data() + total_sent, request.size() - total_sent, 0));
        }
        if (sent <= 0) {
            if (ssl) SSL_free(ssl);
            if (ssl_ctx) SSL_CTX_free(ssl_ctx);
            close(sockfd);
            throw RuntimeError("Failed to send HTTP request", loc);
        }
        total_sent += static_cast<size_t>(sent);
    }

    std::string response;
    char buffer[4096];
    while (true) {
        int received = 0;
        if (use_tls) {
            received = SSL_read(ssl, buffer, sizeof(buffer));
        } else {
            received = static_cast<int>(recv(sockfd, buffer, sizeof(buffer), 0));
        }
        if (received < 0) {
            if (ssl) SSL_free(ssl);
            if (ssl_ctx) SSL_CTX_free(ssl_ctx);
            close(sockfd);
            throw RuntimeError("Failed to receive HTTP response", loc);
        }
        if (received == 0) {
            break;
        }
        response.append(buffer, static_cast<size_t>(received));
    }

    if (ssl) SSL_free(ssl);
    if (ssl_ctx) SSL_CTX_free(ssl_ctx);
    close(sockfd);
    return extract_http_body(response);
}

void Interpreter::register_http_route(const std::string& method, const std::string& path,
                                      const std::string& handler, const SourceLocation& loc) {
    if (path.empty() || path[0] != '/') {
        throw RuntimeError("HTTP route path must start with '/'", loc);
    }
    if (handler.empty()) {
        throw RuntimeError("HTTP route handler cannot be empty", loc);
    }

    std::string key = make_route_key(method, path);
    http_routes[key] = {handler, loc};
    log_message("Registered route: " + normalize_http_method(method) + " " + path + " -> " + handler, "INFO");
}

std::string Interpreter::make_route_key(const std::string& method, const std::string& path) const {
    return normalize_http_method(method) + " " + path;
}

std::string Interpreter::normalize_http_method(const std::string& method) const {
    std::string normalized;
    for (char ch : method) {
        normalized += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return normalized;
}

std::string Interpreter::read_response_body(const std::string& body, const SourceLocation& loc) const {
    if (body.substr(0, 5) != "file:") {
        return body;
    }

    std::string file_path = body.substr(5);
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw RuntimeError("Failed to open response file: " + file_path, loc);
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

Value Interpreter::call_http_handler(const HttpRoute& route, const std::string& method,
                                     const std::string& path, const std::string& body) {
    if (functions.count(route.handler)) {
        HttpRequest previous_request = current_request;
        current_request = {method, path, body};
        try {
            auto handler = functions[route.handler];
            Value result = handler->param_names.empty()
                ? execute_function(route.handler, {})
                : execute_function(route.handler, {Value(method), Value(path), Value(body)});
            current_request = previous_request;
            return result;
        } catch (...) {
            current_request = previous_request;
            throw;
        }
    }
    return Value(read_response_body(route.handler, route.location));
}

std::string Interpreter::response_content_type(const Value& value) const {
    if (value.type == ValueType::OBJECT || value.type == ValueType::ARRAY) {
        return "application/json; charset=utf-8";
    }
    if (value.type == ValueType::STRING || value.type == ValueType::BYTES) {
        size_t start = 0;
        while (start < value.str_val.size() && std::isspace(static_cast<unsigned char>(value.str_val[start]))) {
            start++;
        }
        std::string prefix = value.str_val.substr(start, 15);
        for (char& ch : prefix) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (prefix.rfind("<!doctype html", 0) == 0 || prefix.rfind("<html", 0) == 0) {
            return "text/html; charset=utf-8";
        }
        if (prefix.rfind("{", 0) == 0 || prefix.rfind("[", 0) == 0) {
            return "application/json; charset=utf-8";
        }
    }
    return "text/plain; charset=utf-8";
}

std::string Interpreter::response_body_from_value(const Value& value) const {
    if (value.type == ValueType::OBJECT || value.type == ValueType::ARRAY) {
        return stringify_json(value);
    }
    return value.to_string();
}

Value Interpreter::read_file_path(const Value& arg, const SourceLocation& loc) const {
    if (arg.type != ValueType::STRING && arg.type != ValueType::BYTES) {
        throw TypeError("Builtin 'read::file' expects a string path", loc);
    }

    std::ifstream file(arg.str_val);
    if (!file.is_open()) {
        throw RuntimeError("Failed to open file: " + arg.str_val, loc);
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    return Value(content);
}

Value Interpreter::execute_json_builtin(const std::string& name, const std::vector<Value>& args,
                                        const SourceLocation& loc) {
    if (name == "json::parse" || name == "json::decode" || name == "json::unmarshal") {
        if (args.size() != 1) {
            throw RuntimeError("Builtin '" + name + "' expects 1 argument", loc);
        }
        if (args[0].type != ValueType::STRING && args[0].type != ValueType::BYTES) {
            throw TypeError("Builtin '" + name + "' expects a string", loc);
        }
        return parse_json(args[0].str_val, loc);
    }

    if (name == "json::stringify" || name == "json::encode" || name == "json::marshal") {
        if (args.size() != 1) {
            throw RuntimeError("Builtin '" + name + "' expects 1 argument", loc);
        }
        return Value(stringify_json(args[0]));
    }

    if (name == "json::object") {
        if (args.size() % 2 != 0) {
            throw RuntimeError("Builtin 'json::object' expects key/value pairs", loc);
        }

        std::map<std::string, Value> object;
        for (size_t i = 0; i < args.size(); i += 2) {
            if (args[i].type != ValueType::STRING && args[i].type != ValueType::BYTES) {
                throw TypeError("JSON object keys must be strings", loc);
            }
            object[args[i].str_val] = args[i + 1];
        }
        return Value::Object(object);
    }

    if (name == "json::write" || name == "json::save") {
        if (args.size() != 2) {
            throw RuntimeError("Builtin '" + name + "' expects path and value", loc);
        }
        if (args[0].type != ValueType::STRING && args[0].type != ValueType::BYTES) {
            throw TypeError("JSON file path must be a string", loc);
        }

        std::ofstream file(args[0].str_val, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            throw RuntimeError("Failed to open JSON file for writing: " + args[0].str_val, loc);
        }
        if (args[1].type == ValueType::OBJECT || args[1].type == ValueType::ARRAY) {
            file << stringify_json(args[1]);
        } else {
            file << args[1].to_json();
        }
        file.close();
        return Value::Bool(true);
    }

    if (name == "json::read") {
        if (args.size() != 1) {
            throw RuntimeError("Builtin 'json::read' expects 1 argument", loc);
        }
        Value content = read_file_path(args[0], loc);
        return parse_json(content.str_val, loc);
    }

    throw RuntimeError("Unknown JSON builtin: " + name, loc);
}

Value Interpreter::execute_request_builtin(const std::string& name, const std::vector<Value>& args,
                                           const SourceLocation& loc) {
    if (!args.empty()) {
        throw RuntimeError("Builtin '" + name + "' expects 0 arguments", loc);
    }

    if (name == "request::method") return Value(current_request.method);
    if (name == "request::path") return Value(current_request.path);
    if (name == "request::body") return Value(current_request.body);
    if (name == "request::json") return parse_json(current_request.body, loc);

    throw RuntimeError("Unknown request builtin: " + name, loc);
}

std::string Interpreter::escape_sql_identifier(const std::string& identifier, const SourceLocation& loc) const {
    if (identifier.empty()) {
        throw RuntimeError("SQL identifier cannot be empty", loc);
    }

    for (char ch : identifier) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
            throw RuntimeError("Unsafe SQL identifier: " + identifier, loc);
        }
    }
    return identifier;
}

std::string Interpreter::sql_literal(const Value& value) const {
    if (value.type == ValueType::INT) return std::to_string(value.int_val);
    if (value.type == ValueType::FLOAT) return std::to_string(value.float_val);
    if (value.type == ValueType::BOOL) return value.bool_val ? "1" : "0";
    if (value.type == ValueType::NONE) return "NULL";

    std::string raw = value.to_string();
    std::string escaped = "'";
    for (char ch : raw) {
        if (ch == '\'') {
            escaped += "''";
        } else {
            escaped += ch;
        }
    }
    escaped += "'";
    return escaped;
}

std::string Interpreter::orm_table_name(const std::string& model_name) const {
    std::string table_name;
    for (char ch : model_name) {
        table_name += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (!table_name.empty() && table_name.back() != 's') {
        table_name += "s";
    }
    return table_name;
}

std::string Interpreter::orm_sql_type(const OrmField& field) const {
    std::string type_name = field.db_type;
    size_t dot = type_name.rfind('.');
    if (dot != std::string::npos) {
        type_name = type_name.substr(dot + 1);
    }

    if (type_name == "Integer" || type_name == "Int") {
        return field.primary_key ? "INTEGER PRIMARY KEY" : "INTEGER";
    }
    if (type_name == "Float" || type_name == "Real") {
        return "REAL";
    }
    if (type_name == "Boolean" || type_name == "Bool") {
        return "INTEGER";
    }
    if (type_name == "Bytes" || type_name == "Blob") {
        return "BLOB";
    }
    if (type_name == "String" && field.size > 0) {
        return "VARCHAR(" + std::to_string(field.size) + ")";
    }
    return "TEXT";
}

std::string Interpreter::create_table_sql(const ClassDef& model) const {
    std::string query = "CREATE TABLE IF NOT EXISTS " + escape_sql_identifier(orm_table_name(model.name), model.location) + " (";
    for (size_t i = 0; i < model.fields.size(); ++i) {
        if (i > 0) {
            query += ", ";
        }
        query += escape_sql_identifier(model.fields[i].name, model.fields[i].location) + " " + orm_sql_type(model.fields[i]);
    }
    query += ")";
    return query;
}

std::string Interpreter::model_table_or_name(const std::string& model_or_table) const {
    if (orm_models.count(model_or_table)) {
        return orm_table_name(model_or_table);
    }
    return model_or_table;
}

std::string Interpreter::build_insert_sql(const std::string& table, const Value& data, const SourceLocation& loc) const {
    Value object = data;
    if (object.type == ValueType::STRING || object.type == ValueType::BYTES) {
        object = parse_json(object.str_val, loc);
    }
    if (object.type != ValueType::OBJECT) {
        throw TypeError("SQL insert expects a JSON object", loc);
    }
    if (object.obj_val.empty()) {
        throw RuntimeError("SQL insert object cannot be empty", loc);
    }

    const ClassDef* model = nullptr;
    auto model_it = orm_models.find(table);
    if (model_it != orm_models.end()) {
        model = model_it->second.get();
    }

    std::ostringstream columns;
    std::ostringstream values;
    bool first = true;
    for (const auto& [key, value] : object.obj_val) {
        if (model) {
            bool known_field = false;
            for (const auto& field : model->fields) {
                if (field.name == key) {
                    known_field = true;
                    break;
                }
            }
            if (!known_field) {
                continue;
            }
        }
        if (!first) {
            columns << ", ";
            values << ", ";
        }
        columns << escape_sql_identifier(key, loc);
        values << sql_literal(value);
        first = false;
    }
    if (first) {
        throw RuntimeError("SQL insert object does not contain fields for table/model '" + table + "'", loc);
    }

    return "INSERT INTO " + escape_sql_identifier(model_table_or_name(table), loc) +
           " (" + columns.str() + ") VALUES (" + values.str() + ")";
}

void Interpreter::execute_sql_statement(const std::string& statement, const SourceLocation& loc) const {
    if (!sqlite_db) {
        throw RuntimeError("Open SQLite database first with sql::open(\"app.sqlite\")", loc);
    }

    std::string query = statement;
    if (query.empty() || query.back() != ';') {
        query += ";";
    }

    char* error_message = nullptr;
    int rc = sqlite3_exec(sqlite_db, query.c_str(), nullptr, nullptr, &error_message);
    if (rc != SQLITE_OK) {
        std::string message = error_message ? error_message : sqlite3_errmsg(sqlite_db);
        sqlite3_free(error_message);
        throw RuntimeError("SQLite query failed: " + message, loc);
    }
}

Value Interpreter::execute_sql_builtin(const std::string& name, const std::vector<Value>& args,
                                       const SourceLocation& loc) {
    if (name == "sql::open" || name == "sql::connect") {
        if (args.size() != 1) {
            throw RuntimeError("Builtin '" + name + "' expects 1 argument", loc);
        }
        if (args[0].type != ValueType::STRING && args[0].type != ValueType::BYTES) {
            throw TypeError("SQLite database path must be a string", loc);
        }

        if (sqlite_db) {
            sqlite3_close(sqlite_db);
            sqlite_db = nullptr;
        }

        sql_output_path = args[0].str_val;
        int rc = sqlite3_open(sql_output_path.c_str(), &sqlite_db);
        if (rc != SQLITE_OK) {
            std::string message = sqlite_db ? sqlite3_errmsg(sqlite_db) : "unknown error";
            if (sqlite_db) {
                sqlite3_close(sqlite_db);
                sqlite_db = nullptr;
            }
            throw RuntimeError("Failed to open SQLite database: " + message, loc);
        }

        char* error_message = nullptr;
        rc = sqlite3_exec(sqlite_db, "PRAGMA schema_version;", nullptr, nullptr, &error_message);
        if (rc != SQLITE_OK) {
            std::string message = error_message ? error_message : sqlite3_errmsg(sqlite_db);
            sqlite3_free(error_message);
            sqlite3_close(sqlite_db);
            sqlite_db = nullptr;
            throw RuntimeError("File is not a SQLite database: " + sql_output_path +
                               ". Use a new .sqlite/.db file or rename the old SQL script. SQLite says: " +
                               message, loc);
        }

        return Value::Bool(true);
    }

    if (name == "sql::exec") {
        if (args.size() != 1) {
            throw RuntimeError("Builtin 'sql::exec' expects 1 argument", loc);
        }
        if (args[0].type != ValueType::STRING && args[0].type != ValueType::BYTES) {
            throw TypeError("SQL query must be a string", loc);
        }
        execute_sql_statement(args[0].str_val, loc);
        return Value(args[0].str_val);
    }

    if (name == "orm::migrate") {
        if (args.size() != 1) {
            throw RuntimeError("Builtin 'orm::migrate' expects 1 argument", loc);
        }
        if (args[0].type != ValueType::STRING && args[0].type != ValueType::BYTES) {
            throw TypeError("ORM model name must be a string", loc);
        }
        if (!orm_models.count(args[0].str_val)) {
            throw RuntimeError("ORM model was not found: " + args[0].str_val, loc);
        }

        std::string query = create_table_sql(*orm_models[args[0].str_val]);
        execute_sql_statement(query, loc);
        return Value(query);
    }

    if (name == "sql::table" || name == "orm::table") {
        if (args.size() != 2) {
            throw RuntimeError("Builtin '" + name + "' expects table and columns", loc);
        }
        if (args[0].type != ValueType::STRING && args[0].type != ValueType::BYTES) {
            throw TypeError("SQL table name must be a string", loc);
        }
        if (args[1].type != ValueType::STRING && args[1].type != ValueType::BYTES) {
            throw TypeError("SQL table columns must be a string", loc);
        }

        std::string query = "CREATE TABLE IF NOT EXISTS " +
                            escape_sql_identifier(args[0].str_val, loc) +
                            " (" + args[1].str_val + ")";
        execute_sql_statement(query, loc);
        return Value(query);
    }

    if (name == "sql::insert" || name == "orm::save") {
        if (args.size() != 2) {
            throw RuntimeError("Builtin '" + name + "' expects table and data", loc);
        }
        if (args[0].type != ValueType::STRING && args[0].type != ValueType::BYTES) {
            throw TypeError("SQL table name must be a string", loc);
        }

        std::string query = build_insert_sql(args[0].str_val, args[1], loc);
        execute_sql_statement(query, loc);
        return Value(query);
    }

    throw RuntimeError("Unknown SQL builtin: " + name, loc);
}

std::string Interpreter::normalize_log_level(const std::string& level) const {
    size_t start = 0;
    while (start < level.size() && std::isspace(static_cast<unsigned char>(level[start]))) {
        start++;
    }

    size_t end = level.size();
    while (end > start && std::isspace(static_cast<unsigned char>(level[end - 1]))) {
        end--;
    }

    std::string normalized;
    for (size_t i = start; i < end; ++i) {
        char ch = level[i];
        if (ch == '-') {
            ch = '_';
        }
        normalized += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }

    if (normalized.empty()) return "INFO";
    if (normalized == "WARNING") return "WARN";
    if (normalized == "ERR") return "ERROR";
    if (normalized == "FATAL") return "CRITICAL";
    if (normalized == "CRIT" || normalized == "CRITECAL") return "CRITICAL";
    return normalized;
}

bool Interpreter::is_known_log_level(const std::string& level) const {
    std::string normalized = normalize_log_level(level);
    return normalized == "TRACE" ||
           normalized == "DEBUG" ||
           normalized == "INFO" ||
           normalized == "NOTICE" ||
           normalized == "WARN" ||
           normalized == "ERROR" ||
           normalized == "CRITICAL";
}

std::string Interpreter::log_level_from_builtin(const std::string& name) const {
    if (name == "log_trace") return "TRACE";
    if (name == "log::trace") return "TRACE";
    if (name == "log_debug") return "DEBUG";
    if (name == "log::debug") return "DEBUG";
    if (name == "log_notice") return "NOTICE";
    if (name == "log::notice") return "NOTICE";
    if (name == "log_warn" || name == "log_warning") return "WARN";
    if (name == "log::warn" || name == "log::warning") return "WARN";
    if (name == "log_error") return "ERROR";
    if (name == "log::error") return "ERROR";
    if (name == "log_critical" || name == "log_critecal" || name == "log_fatal") return "CRITICAL";
    if (name == "log::critical" || name == "log::critecal" || name == "log::fatal") return "CRITICAL";
    return "INFO";
}

bool Interpreter::is_log_builtin_name(const std::string& name) const {
    return name == "log" ||
           name == "log_output" ||
           name == "set_log_output" ||
           name == "log_console" ||
           name == "log_file" ||
           name == "log::output" ||
           name == "log::set_output" ||
           name == "log::console" ||
           name == "log::file" ||
           name == "log::trace" ||
           name == "log::debug" ||
           name == "log::info" ||
           name == "log::notice" ||
           name == "log::warn" ||
           name == "log::warning" ||
           name == "log::error" ||
           name == "log::critical" ||
           name == "log::critecal" ||
           name == "log::fatal" ||
           name == "log_trace" ||
           name == "log_debug" ||
           name == "log_info" ||
           name == "log_notice" ||
           name == "log_warn" ||
           name == "log_warning" ||
           name == "log_error" ||
           name == "log_critical" ||
           name == "log_critecal" ||
           name == "log_fatal";
}

Value Interpreter::set_log_output(const Value& arg, const SourceLocation& loc) {
    if (arg.type != ValueType::STRING && arg.type != ValueType::BYTES) {
        throw TypeError("Builtin 'log_output' expects 'console' or 'file'", loc);
    }

    std::string target;
    for (char ch : arg.str_val) {
        target += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    if (target == "stdout" || target == "terminal") {
        target = "console";
    }
    if (target == "log" || target == "logs") {
        target = "file";
    }

    if (target == "file" && !log_file.is_open()) {
        throw RuntimeError("Cannot switch log output to file before opening a log file", loc);
    }
    if (target != "console" && target != "file") {
        throw RuntimeError("Log output must be 'console' or 'file'", loc);
    }

    log_output = target;
    return Value::Bool(true);
}

Value Interpreter::execute_log_builtin(const std::string& name, const std::vector<Value>& args,
                                       const SourceLocation& loc) {
    if (name == "log_console" || name == "log::console") {
        if (!args.empty()) {
            throw RuntimeError("Builtin 'log_console' expects 0 arguments", loc);
        }
        log_output = "console";
        return Value::Bool(true);
    }
    if (name == "log_file" || name == "log::file") {
        if (args.size() != 1) {
            throw RuntimeError("Builtin 'log_file' expects 1 argument", loc);
        }
        return open_log_path(args[0], loc);
    }
    if (name == "log_output" || name == "set_log_output" ||
        name == "log::output" || name == "log::set_output") {
        if (args.size() != 1) {
            throw RuntimeError("Builtin '" + name + "' expects 1 argument", loc);
        }
        return set_log_output(args[0], loc);
    }
    if (args.empty()) {
        throw RuntimeError("Builtin '" + name + "' expects at least 1 argument", loc);
    }

    std::string level = log_level_from_builtin(name);
    size_t message_start = 0;
    size_t message_end = args.size();
    if (name == "log" && args.size() > 1) {
        bool first_is_string = args[0].type == ValueType::STRING || args[0].type == ValueType::BYTES;
        bool last_is_string = args.back().type == ValueType::STRING || args.back().type == ValueType::BYTES;
        if (first_is_string && is_known_log_level(args[0].str_val)) {
            level = normalize_log_level(args[0].str_val);
            message_start = 1;
        } else if (last_is_string && is_known_log_level(args.back().str_val)) {
            level = normalize_log_level(args.back().str_val);
            message_end--;
        } else if (first_is_string) {
            level = normalize_log_level(args[0].str_val);
            message_start = 1;
        } else {
            throw TypeError("Builtin 'log' level must be a string", loc);
        }
    }

    std::string message;
    for (size_t i = message_start; i < message_end; ++i) {
        if (!message.empty()) {
            message += " ";
        }
        message += args[i].to_string();
    }

    log_message(message, level);
    return Value::Bool(true);
}

Value Interpreter::open_log_path(const Value& arg, const SourceLocation& loc) {
    if (arg.type != ValueType::STRING && arg.type != ValueType::BYTES) {
        throw TypeError("Builtin 'open_log' expects a string path", loc);
    }
    if (log_file.is_open()) {
        log_file.close();
    }
    log_file.open(arg.str_val, std::ios::app);
    if (!log_file.is_open()) {
        throw RuntimeError("Failed to open log file: " + arg.str_val, loc);
    }
    log_output = "file";
    return Value::Bool(true);
}

void Interpreter::run_http_server(const std::string& host, long long port, const std::string& body, const SourceLocation& loc) {
    if (port <= 0 || port > 65535) {
        throw RuntimeError("Server port must be between 1 and 65535", loc);
    }

    std::string display_host = host.empty() ? "0.0.0.0" : host;
    std::string server_url = "http://" + display_host + ":" + std::to_string(port);
    log_message("Server starting on " + server_url, "INFO");
    log_message("Registered HTTP routes: " + std::to_string(http_routes.size()), "DEBUG");

    struct addrinfo hints {};
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo* result = nullptr;
    std::string port_text = std::to_string(port);
    const char* bind_host = host.empty() ? nullptr : host.c_str();
    int status = getaddrinfo(bind_host, port_text.c_str(), &hints, &result);
    if (status != 0) {
        throw RuntimeError("Failed to resolve server bind address: " + std::string(gai_strerror(status)), loc);
    }

    int server_fd = -1;
    for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        server_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (server_fd == -1) {
            continue;
        }

        int yes = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (bind(server_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(server_fd);
        server_fd = -1;
    }
    freeaddrinfo(result);

    if (server_fd == -1) {
        throw RuntimeError("Failed to bind local server", loc);
    }
    if (listen(server_fd, 16) != 0) {
        close(server_fd);
        throw RuntimeError("Failed to listen on local server", loc);
    }

    log_message("Server listening on " + server_url, "INFO");

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            log_message("Failed to accept client connection", "WARN");
            continue;
        }
        log_message("Accepted client connection", "DEBUG");

        char buffer[8192] = {0};
        ssize_t received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            log_message("Client closed connection before sending a request", "DEBUG");
            close(client_fd);
            continue;
        }
        log_message("Received request bytes: " + std::to_string(received), "DEBUG");
        std::string request(buffer, received);

        std::string method, path;
        size_t pos = request.find(' ');
        if (pos != std::string::npos) {
            method = request.substr(0, pos);
            size_t pos2 = request.find(' ', pos + 1);
            if (pos2 != std::string::npos) {
                path = request.substr(pos + 1, pos2 - pos - 1);
            }
        }

        size_t body_pos = request.find("\r\n\r\n");
        std::string request_body = body_pos != std::string::npos ? request.substr(body_pos + 4) : "";
        std::string route_key = make_route_key(method, path);
        auto route = http_routes.find(route_key);
        log_message("Request: " + method + " " + path + " from client", "INFO");

        std::string response_body;
        std::string content_type = "text/plain; charset=utf-8";
        std::string status_line = "HTTP/1.1 200 OK\r\n";

        try {
            if (route != http_routes.end()) {
                Value result = call_http_handler(route->second, normalize_http_method(method), path, request_body);
                response_body = response_body_from_value(result);
                content_type = response_content_type(result);
                std::string route_content_type = response_content_type_for_path(path);
                if (!route_content_type.empty()) {
                    content_type = route_content_type;
                }
            } else if (!body.empty() && normalize_http_method(method) == "GET" && path == "/") {
                response_body = read_response_body(body, loc);
                if (body.find(".html") != std::string::npos) {
                    content_type = "text/html; charset=utf-8";
                }
            } else {
                status_line = "HTTP/1.1 404 Not Found\r\n";
                response_body = "Not found";
                log_message("Route not found: " + method + " " + path, "WARN");
            }
        } catch (const CompilerError& e) {
            status_line = "HTTP/1.1 500 Internal Server Error\r\n";
            response_body = e.what();
            log_message("Route handler failed: " + std::string(e.what()), "ERROR");
        }

        std::string response = status_line;
        response += "Content-Type: " + content_type + "\r\n";
        response += "Content-Length: " + std::to_string(response_body.size()) + "\r\n";
        response += "Connection: close\r\n\r\n";
        response += response_body;
        send(client_fd, response.data(), response.size(), 0);
        log_message("Response sent: " + std::to_string(response_body.size()) + " bytes", "INFO");
        close(client_fd);
    }
}

Value Interpreter::parse_json(const std::string& json_str, const SourceLocation& loc) const {
    // Простой JSON парсер
    size_t pos = 0;
    return parse_json_value(json_str, pos, loc);
}

Value Interpreter::parse_json_value(const std::string& json_str, size_t& pos, const SourceLocation& loc) const {
    skip_whitespace(json_str, pos);
    if (pos >= json_str.size()) throw RuntimeError("Unexpected end of JSON", loc);

    char ch = json_str[pos];
    if (ch == '{') {
        return parse_json_object(json_str, pos, loc);
    } else if (ch == '[') {
        return parse_json_array(json_str, pos, loc);
    } else if (ch == '"') {
        return parse_json_string(json_str, pos, loc);
    } else if (ch == 't' || ch == 'f') {
        return parse_json_bool(json_str, pos, loc);
    } else if (ch == 'n') {
        return parse_json_null(json_str, pos, loc);
    } else if (isdigit(ch) || ch == '-') {
        return parse_json_number(json_str, pos, loc);
    } else {
        throw RuntimeError("Invalid JSON character: " + std::string(1, ch), loc);
    }
}

Value Interpreter::parse_json_object(const std::string& json_str, size_t& pos, const SourceLocation& loc) const {
    std::map<std::string, Value> obj;
    pos++; // skip '{'
    skip_whitespace(json_str, pos);
    if (pos < json_str.size() && json_str[pos] == '}') {
        pos++;
        return Value::Object(obj);
    }
    while (true) {
        skip_whitespace(json_str, pos);
        if (json_str[pos] != '"') throw RuntimeError("Expected string key in object", loc);
        std::string key = parse_json_string_value(json_str, pos, loc);
        skip_whitespace(json_str, pos);
        if (json_str[pos] != ':') throw RuntimeError("Expected ':' after key", loc);
        pos++;
        Value value = parse_json_value(json_str, pos, loc);
        obj[key] = value;
        skip_whitespace(json_str, pos);
        if (json_str[pos] == '}') {
            pos++;
            break;
        } else if (json_str[pos] == ',') {
            pos++;
        } else {
            throw RuntimeError("Expected ',' or '}' in object", loc);
        }
    }
    return Value::Object(obj);
}

Value Interpreter::parse_json_array(const std::string& json_str, size_t& pos, const SourceLocation& loc) const {
    std::vector<Value> arr;
    pos++; // skip '['
    skip_whitespace(json_str, pos);
    if (pos < json_str.size() && json_str[pos] == ']') {
        pos++;
        return Value::Array(arr);
    }
    while (true) {
        Value value = parse_json_value(json_str, pos, loc);
        arr.push_back(value);
        skip_whitespace(json_str, pos);
        if (json_str[pos] == ']') {
            pos++;
            break;
        } else if (json_str[pos] == ',') {
            pos++;
        } else {
            throw RuntimeError("Expected ',' or ']' in array", loc);
        }
    }
    return Value::Array(arr);
}

Value Interpreter::parse_json_string(const std::string& json_str, size_t& pos, const SourceLocation& loc) const {
    std::string str = parse_json_string_value(json_str, pos, loc);
    return Value(str);
}

std::string Interpreter::parse_json_string_value(const std::string& json_str, size_t& pos, const SourceLocation& loc) const {
    if (json_str[pos] != '"') throw RuntimeError("Expected '\"'", loc);
    pos++;
    std::string str;
    while (pos < json_str.size() && json_str[pos] != '"') {
        if (json_str[pos] == '\\') {
            pos++;
            if (pos >= json_str.size()) throw RuntimeError("Unexpected end of string", loc);
            char esc = json_str[pos];
            if (esc == '"') str += '"';
            else if (esc == '\\') str += '\\';
            else if (esc == '/') str += '/';
            else if (esc == 'b') str += '\b';
            else if (esc == 'f') str += '\f';
            else if (esc == 'n') str += '\n';
            else if (esc == 'r') str += '\r';
            else if (esc == 't') str += '\t';
            else if (esc == 'u') {
                // Simple unicode handling, assume 4 hex digits
                pos += 4;
                str += '?'; // placeholder
            } else {
                throw RuntimeError("Invalid escape sequence", loc);
            }
        } else {
            str += json_str[pos];
        }
        pos++;
    }
    if (pos >= json_str.size() || json_str[pos] != '"') throw RuntimeError("Unterminated string", loc);
    pos++;
    return str;
}

Value Interpreter::parse_json_bool(const std::string& json_str, size_t& pos, const SourceLocation& loc) const {
    if (json_str.substr(pos, 4) == "true") {
        pos += 4;
        return Value::Bool(true);
    } else if (json_str.substr(pos, 5) == "false") {
        pos += 5;
        return Value::Bool(false);
    } else {
        throw RuntimeError("Invalid boolean", loc);
    }
}

Value Interpreter::parse_json_null(const std::string& json_str, size_t& pos, const SourceLocation& loc) const {
    if (json_str.substr(pos, 4) == "null") {
        pos += 4;
        return Value(); // NONE
    } else {
        throw RuntimeError("Invalid null", loc);
    }
}

Value Interpreter::parse_json_number(const std::string& json_str, size_t& pos, const SourceLocation& loc) const {
    size_t start = pos;
    if (json_str[pos] == '-') pos++;
    while (pos < json_str.size() && isdigit(json_str[pos])) pos++;
    if (pos < json_str.size() && json_str[pos] == '.') {
        pos++;
        while (pos < json_str.size() && isdigit(json_str[pos])) pos++;
        return Value(std::stod(json_str.substr(start, pos - start)));
    } else {
        return Value(std::stoll(json_str.substr(start, pos - start)));
    }
}

void Interpreter::skip_whitespace(const std::string& json_str, size_t& pos) const {
    while (pos < json_str.size() && isspace(json_str[pos])) pos++;
}

std::string Interpreter::stringify_json(const Value& value) const {
    return value.to_json();
}

void Interpreter::execute_block(const std::vector<std::shared_ptr<AstNode>>& body, std::map<std::string, Value>& locals) {
    for (const auto& stmt : body) {
        execute_statement(stmt, locals);
    }
}

void Interpreter::execute_statement(const std::shared_ptr<AstNode>& stmt, std::map<std::string, Value>& locals) {
    if (auto decl = std::dynamic_pointer_cast<VarDecl>(stmt)) {
        Value val = coerce_value(eval(decl->expr, locals), decl->type_name, decl->location);
        assign_value(decl->name, val, locals);
        if (DEBUG) {
            std::cout << "[DEBUG] Set " << (globals.count(decl->name) ? "global" : "local")
                      << " var " << decl->name << " = " << val.to_string() << std::endl;
        }
        return;
    }

    if (auto print = std::dynamic_pointer_cast<PrintStmt>(stmt)) {
        size_t fmt_idx = 0;
        for (const auto& arg : print->args) {
            Value v = eval(arg, locals);
            std::string fmt = fmt_idx < print->formats.size() ? print->formats[fmt_idx++] : "";
            std::cout << v.to_string(fmt);
        }
        if (!print->is_printg) {
            std::cout << std::endl;
        }
        return;
    }

    if (auto call = std::dynamic_pointer_cast<CallStmt>(stmt)) {
        std::vector<Value> args;
        for (const auto& arg : call->args) {
            args.push_back(eval(arg, locals));
        }
        if (is_log_builtin_name(call->func_name)) {
            execute_log_builtin(call->func_name, args, call->location);
            return;
        }
        if (call->func_name == "open_log") {
            if (args.size() != 1) {
                throw RuntimeError("Builtin 'open_log' expects 1 argument", call->location);
            }
            open_log_path(args[0], call->location);
            return;
        }
        if (DEBUG) {
            std::cout << "[DEBUG] Calling " << call->func_name << " with " << args.size() << " args" << std::endl;
        }
        try {
            execute_function(call->func_name, args);
        } catch (CompilerError& e) {
            e.traceback.push_back(call->location);
            throw;
        }
        return;
    }

    if (auto builtin = std::dynamic_pointer_cast<BuiltinCallExpr>(stmt)) {
        eval(builtin, locals);
        return;
    }

    if (auto ret = std::dynamic_pointer_cast<ReturnStmt>(stmt)) {
        throw FunctionReturn{ret->expr ? eval(ret->expr, locals) : Value(1LL), ret->expr != nullptr};
    }

    if (auto if_stmt = std::dynamic_pointer_cast<IfStmt>(stmt)) {
        const auto& body_to_execute = is_truthy(eval(if_stmt->condition, locals))
            ? if_stmt->then_body
            : if_stmt->else_body;
        execute_block(body_to_execute, locals);
        return;
    }

    if (auto while_stmt = std::dynamic_pointer_cast<WhileStmt>(stmt)) {
        while (is_truthy(eval(while_stmt->condition, locals))) {
            execute_block(while_stmt->body, locals);
        }
        return;
    }

    if (auto input = std::dynamic_pointer_cast<InputStmt>(stmt)) {
        Value val;

        if (!input->prompt.empty()) {
            std::cout << input->prompt;
        } else {
            std::cout << "> ";
        }
        std::cout.flush();

        if (input->format == "{int}") {
            long long x;
            if (std::cin >> x) {
                val = Value(x);
            } else {
                val = Value(0LL);
                std::cin.clear();
                std::cin.ignore(10000, '\n');
            }
        } else if (input->format == "{float}") {
            double x;
            if (std::cin >> x) {
                val = Value(x);
            } else {
                val = Value(0.0);
                std::cin.clear();
                std::cin.ignore(10000, '\n');
            }
        } else if (input->format == "{string}") {
            std::string x;
            if (std::cin.peek() == '\n') {
                std::cin.ignore();
            }
            std::getline(std::cin, x);
            val = Value(x);
        } else if (input->format == "{bool}") {
            std::string x;
            std::cin >> x;
            val = Value::Bool(x == "true" || x == "1" || x == "yes");
        } else if (input->format == "{bytes}") {
            std::string x;
            if (std::cin.peek() == '\n') {
                std::cin.ignore();
            }
            std::getline(std::cin, x);
            val = Value::Bytes(x);
        }

        std::string var_name = input->var_name.empty() ? "input" : input->var_name;
        assign_value(var_name, val, locals);
        if (DEBUG) {
            std::cout << "[DEBUG] Input saved to " << (globals.count(var_name) ? "global" : "local")
                      << " '" << var_name << "' = " << val.to_string() << std::endl;
        }
        return;
    }

    if (auto file_op = std::dynamic_pointer_cast<FileOp>(stmt)) {
        Value file_path_val = eval(file_op->file_path, locals);
        if (file_path_val.type != ValueType::STRING) {
            throw TypeError("File path must be a string", file_op->location);
        }
        std::string file_path = file_path_val.str_val;

        switch (file_op->operation) {
            case T_CREATE: {
                std::ofstream file(file_path, std::ios::out);
                if (!file.is_open()) {
                    throw RuntimeError("Failed to create file: " + file_path, file_op->location);
                }
                file.close();
                if (DEBUG) std::cout << "[DEBUG] Created file: " << file_path << std::endl;
                break;
            }
            case T_WRITE: {
                if (!file_op->data) {
                    throw SemanticError("Write operation requires data argument", file_op->location);
                }
                Value data_val = eval(file_op->data, locals);
                std::ofstream file(file_path, std::ios::out | std::ios::app);
                if (!file.is_open()) {
                    throw RuntimeError("Failed to open file for writing: " + file_path, file_op->location);
                }
                file << data_val.to_string();
                file.close();
                if (DEBUG) std::cout << "[DEBUG] Wrote to file: " << file_path << std::endl;
                break;
            }
            case T_READ: {
                std::ifstream file(file_path);
                if (!file.is_open()) {
                    throw RuntimeError("Failed to open file for reading: " + file_path, file_op->location);
                }
                std::string content;
                std::string line;
                while (std::getline(file, line)) {
                    if (!content.empty()) content += "\n";
                    content += line;
                }
                file.close();
                if (DEBUG) {
                    std::cout << "[DEBUG] Read from file: " << file_path << " (" << content.size() << " bytes)" << std::endl;
                }
                std::cout << content << std::endl;
                break;
            }
            case T_CLOSE: {
                if (open_files.count(file_path)) {
                    open_files[file_path]->close();
                    open_files.erase(file_path);
                    if (DEBUG) std::cout << "[DEBUG] Closed file: " << file_path << std::endl;
                }
                break;
            }
            case T_DELETE: {
                if (std::filesystem::exists(file_path)) {
                    if (open_files.count(file_path)) {
                        open_files[file_path]->close();
                        open_files.erase(file_path);
                    }
                    std::filesystem::remove(file_path);
                    if (DEBUG) std::cout << "[DEBUG] Deleted file: " << file_path << std::endl;
                } else {
                    throw RuntimeError("File does not exist: " + file_path, file_op->location);
                }
                break;
            }
            default:
                throw SemanticError("Unknown file operation", file_op->location);
        }
        return;
    }

    if (auto net_op = std::dynamic_pointer_cast<NetOp>(stmt)) {
        Value url_val = eval(net_op->url, locals);
        if (url_val.type != ValueType::STRING) {
            throw TypeError("Network URL must be a string", net_op->location);
        }

        if (!net_op->transport.empty() && net_op->transport != "http" && net_op->transport != "https") {
            throw RuntimeError("Unsupported network transport: " + net_op->transport, net_op->location);
        }
        if (net_op->method != "get" && net_op->method != "post" &&
            net_op->method != "serve" && net_op->method != "run" &&
            net_op->method != "route") {
            throw RuntimeError("Unsupported network method: " + net_op->method, net_op->location);
        }
        if ((net_op->method == "serve" || net_op->method == "run") && net_op->transport == "https") {
            throw RuntimeError("Local server currently supports HTTP only", net_op->location);
        }

        std::string body;
        if ((net_op->method == "get" || net_op->method == "post") &&
            net_op->data && url_val.str_val.rfind("/", 0) == 0) {
            Value handler_val = eval(net_op->data, locals);
            if (handler_val.type != ValueType::STRING && handler_val.type != ValueType::BYTES) {
                throw TypeError("Route handler must be a string", net_op->location);
            }
            register_http_route(net_op->method, url_val.str_val, handler_val.str_val, net_op->location);
            return;
        }

        if (net_op->method == "route") {
            if (!net_op->path) {
                throw RuntimeError("Route requires path and handler", net_op->location);
            }
            Value first_val = eval(net_op->url, locals);
            Value second_val = eval(net_op->path, locals);
            Value handler_val = net_op->data ? eval(net_op->data, locals) : second_val;
            std::string route_method = net_op->data ? first_val.str_val : "GET";
            std::string route_path = net_op->data ? second_val.str_val : first_val.str_val;
            if (first_val.type != ValueType::STRING || second_val.type != ValueType::STRING) {
                throw TypeError("Route arguments must be strings", net_op->location);
            }
            if (handler_val.type != ValueType::STRING && handler_val.type != ValueType::BYTES) {
                throw TypeError("Route handler must be a string", net_op->location);
            }
            register_http_route(route_method, route_path, handler_val.str_val, net_op->location);
            return;
        }

        if (net_op->method == "serve" || net_op->method == "run") {
            if (!net_op->port) {
                throw RuntimeError("Server requires host and port", net_op->location);
            }
            Value port_val = eval(net_op->port, locals);
            if (port_val.type != ValueType::INT) {
                throw TypeError("Server port must be an int", net_op->location);
            }
            if (net_op->data) {
                Value body_val = eval(net_op->data, locals);
                if (body_val.type != ValueType::STRING && body_val.type != ValueType::BYTES) {
                    throw TypeError("Server response body must be a string or bytes", net_op->location);
                }
                body = body_val.str_val;
            }
            run_http_server(url_val.str_val, port_val.int_val, body, net_op->location);
            return;
        }

        if (net_op->method == "post") {
            if (!net_op->data) {
                throw RuntimeError("POST requires a body argument", net_op->location);
            }
            Value body_val = eval(net_op->data, locals);
            if (body_val.type != ValueType::STRING) {
                if (body_val.type != ValueType::BYTES) {
                    throw TypeError("Network POST body must be a string or bytes", net_op->location);
                }
            }
            body = body_val.str_val;
        }

        std::string method = net_op->method == "post" ? "POST" : "GET";
        std::string response = perform_http_request(net_op->transport, method, url_val.str_val, body, net_op->location);
        std::cout << response << std::endl;
        return;
    }
}

void Interpreter::run(const std::vector<std::shared_ptr<AstNode>>& program) {
    // Сначала обрабатываем все функции и глобальные переменные
    for (const auto& node : program) {
        if (auto klass = std::dynamic_pointer_cast<ClassDef>(node)) {
            orm_models[klass->name] = klass;
        } else if (auto f = std::dynamic_pointer_cast<FunctionDef>(node)) {
            functions[f->name] = f;
            for (const auto& route : f->routes) {
                register_http_route(route.method, route.path, f->name, route.location);
            }
        } else if (auto var = std::dynamic_pointer_cast<VarDecl>(node)) {
            // Обрабатываем глобальные переменные
            Value val = coerce_value(eval(var->expr), var->type_name, var->location);
            globals[var->name] = val;
            if (DEBUG) std::cout << "[DEBUG] Global var " << var->name << " = " << val.to_string() << std::endl;
        }
    }

    if (functions.count("main")) {
        execute_function("main", {});
    }
}

Value Interpreter::execute_function(const std::string& name, const std::vector<Value>& call_args) {
    if (!functions.count(name)) {
        throw UndefinedError(name, "function", SourceLocation());
    }
    auto func = functions[name];

    // Добавляем текущую функцию в стек вызовов
    call_stack.push_back(func->location);

    // Локальные переменные функции (параметры + переменные внутри функции)
    std::map<std::string, Value> locals;
    if (!current_request.method.empty()) {
        locals["request_method"] = Value(current_request.method);
        locals["request_path"] = Value(current_request.path);
        locals["request_body"] = Value(current_request.body);
    }

    // Устанавливаем параметры как локальные переменные
    if (DEBUG) std::cout << "[DEBUG] Function " << name << " has " << func->param_names.size() << " params, got " << call_args.size() << " args" << std::endl;
    for (size_t i = 0; i < func->param_names.size() && i < call_args.size(); ++i) {
        std::string param_type = i < func->param_types.size() ? func->param_types[i] : "";
        locals[func->param_names[i]] = param_type.empty()
            ? call_args[i]
            : coerce_value(call_args[i], param_type, func->location);
        if (DEBUG) std::cout << "[DEBUG] Param " << func->param_names[i] << " = " << call_args[i].to_string() << std::endl;
    }

    try {
        execute_block(func->body, locals);
    } catch (const FunctionReturn& ret) {
        call_stack.pop_back();
        // Если return с выражением, возвращаем значение без проверки
        // Если return без выражения, проверяем что возвращаемое значение 1
        if (!ret.has_expr && (ret.value.type != ValueType::INT || ret.value.int_val != 1LL)) {
            CompilerError e("Function '" + name + "' must return 1 when using return without expression", func->location);
            e.traceback = call_stack;
            throw e;
        }
        return ret.value;
    } catch (CompilerError& e) {
        if (e.traceback.empty()) {
            e.traceback = call_stack;
        }
        call_stack.pop_back();
        throw;
    }
    // Если функция не вернула значение, это ошибка
    CompilerError e("Function '" + name + "' must contain 'return 1'", func->location);
    e.traceback = call_stack;
    call_stack.pop_back();
    throw e;
}

Value Interpreter::eval(const std::shared_ptr<AstNode>& node, const std::map<std::string, Value>& locals) {
    if (!node) return Value();
    if (auto lit = std::dynamic_pointer_cast<Literal>(node)) return lit->value;
    if (auto builtin = std::dynamic_pointer_cast<BuiltinCallExpr>(node)) {
        if (builtin->name == "read::file") {
            if (builtin->args.size() != 1) {
                throw RuntimeError("Builtin 'read::file' expects 1 argument", builtin->location);
            }
            Value arg = eval(builtin->args[0], locals);
            return read_file_path(arg, builtin->location);
        }
        if (builtin->name.rfind("json::", 0) == 0) {
            std::vector<Value> args;
            for (const auto& arg : builtin->args) {
                args.push_back(eval(arg, locals));
            }
            return execute_json_builtin(builtin->name, args, builtin->location);
        }
        if (builtin->name.rfind("request::", 0) == 0) {
            std::vector<Value> args;
            for (const auto& arg : builtin->args) {
                args.push_back(eval(arg, locals));
            }
            return execute_request_builtin(builtin->name, args, builtin->location);
        }
        if (builtin->name.rfind("sql::", 0) == 0 || builtin->name.rfind("orm::", 0) == 0) {
            std::vector<Value> args;
            for (const auto& arg : builtin->args) {
                args.push_back(eval(arg, locals));
            }
            return execute_sql_builtin(builtin->name, args, builtin->location);
        }
        if (is_log_builtin_name(builtin->name)) {
            std::vector<Value> args;
            for (const auto& arg : builtin->args) {
                args.push_back(eval(arg, locals));
            }
            return execute_log_builtin(builtin->name, args, builtin->location);
        }
        if (builtin->name == "protocol") {
            if (builtin->args.size() != 1) {
                throw RuntimeError("Builtin 'protocol' expects 1 argument", builtin->location);
            }
            Value arg = eval(builtin->args[0], locals);
            if (arg.type != ValueType::STRING && arg.type != ValueType::BYTES) {
                throw TypeError("Builtin 'protocol' expects a string URL", builtin->location);
            }
            ParsedUrl parsed = parse_url(arg.str_val, builtin->location);
            return Value(parsed.scheme);
        }
        if (builtin->name == "json_parse") {
            if (builtin->args.size() != 1) {
                throw RuntimeError("Builtin 'json_parse' expects 1 argument", builtin->location);
            }
            Value arg = eval(builtin->args[0], locals);
            if (arg.type != ValueType::STRING && arg.type != ValueType::BYTES) {
                throw TypeError("Builtin 'json_parse' expects a string", builtin->location);
            }
            return parse_json(arg.str_val, builtin->location);
        }
        if (builtin->name == "json_stringify") {
            if (builtin->args.size() != 1) {
                throw RuntimeError("Builtin 'json_stringify' expects 1 argument", builtin->location);
            }
            Value arg = eval(builtin->args[0], locals);
            return Value(stringify_json(arg));
        }
        if (builtin->name == "read_file") {
            if (builtin->args.size() != 1) {
                throw RuntimeError("Builtin 'read_file' expects 1 argument", builtin->location);
            }
            Value arg = eval(builtin->args[0], locals);
            return read_file_path(arg, builtin->location);
        }
        if (builtin->name == "open_log") {
            if (builtin->args.size() != 1) {
                throw RuntimeError("Builtin 'open_log' expects 1 argument", builtin->location);
            }
            Value arg = eval(builtin->args[0], locals);
            return open_log_path(arg, builtin->location);
        }
        if (builtin->name == "request_method") {
            if (!builtin->args.empty()) {
                throw RuntimeError("Builtin 'request_method' expects 0 arguments", builtin->location);
            }
            return Value(current_request.method);
        }
        if (builtin->name == "request_path") {
            if (!builtin->args.empty()) {
                throw RuntimeError("Builtin 'request_path' expects 0 arguments", builtin->location);
            }
            return Value(current_request.path);
        }
        if (builtin->name == "request_body") {
            if (!builtin->args.empty()) {
                throw RuntimeError("Builtin 'request_body' expects 0 arguments", builtin->location);
            }
            return Value(current_request.body);
        }
        if (builtin->name == "request_json") {
            if (!builtin->args.empty()) {
                throw RuntimeError("Builtin 'request_json' expects 0 arguments", builtin->location);
            }
            return parse_json(current_request.body, builtin->location);
        }
        throw RuntimeError("Unknown builtin expression: " + builtin->name, builtin->location);
    }
    if (auto id = std::dynamic_pointer_cast<Identifier>(node)) {
        // Сначала ищем в локальных переменных, потом в глобальных
        if (locals.count(id->name)) {
            return locals.at(id->name);
        }
        if (globals.count(id->name)) {
            return globals[id->name];
        }
        // Если переменная не найдена, выбрасываем ошибку с traceback
        UndefinedError err(id->name, "variable", id->location);
        err.traceback = call_stack;
        throw err;
    }
    if (auto bin = std::dynamic_pointer_cast<BinaryOp>(node)) {
        Value l = eval(bin->left, locals);
        Value r = eval(bin->right, locals);

        // Операторы сравнения
        if (bin->op == T_GREATER || bin->op == T_LESS || bin->op == T_GREATER_EQUAL ||
            bin->op == T_LESS_EQUAL || bin->op == T_EQUAL_EQUAL || bin->op == T_NOT_EQUAL) {
            bool result = false;

            // Сравнение строк
            if (l.type == ValueType::STRING && r.type == ValueType::STRING) {
                int cmp = l.str_val.compare(r.str_val);
                switch (bin->op) {
                    case T_GREATER: result = (cmp > 0); break;
                    case T_LESS: result = (cmp < 0); break;
                    case T_GREATER_EQUAL: result = (cmp >= 0); break;
                    case T_LESS_EQUAL: result = (cmp <= 0); break;
                    case T_EQUAL_EQUAL: result = (cmp == 0); break;
                    case T_NOT_EQUAL: result = (cmp != 0); break;
                }
            } else {
                // Сравнение чисел или смешанных типов
                // Если один из операндов - строка, а другой - нет, конвертируем в строки
                if (l.type == ValueType::STRING || r.type == ValueType::STRING ||
                    l.type == ValueType::BYTES || r.type == ValueType::BYTES ||
                    l.type == ValueType::NONE || r.type == ValueType::NONE) {
                    std::string l_str = (l.type == ValueType::STRING || l.type == ValueType::BYTES) ? l.str_val : l.to_string();
                    std::string r_str = (r.type == ValueType::STRING || r.type == ValueType::BYTES) ? r.str_val : r.to_string();
                    if (DEBUG) std::cout << "[DEBUG] Comparing: '" << l_str << "' " << (bin->op == T_GREATER ? ">" : bin->op == T_LESS ? "<" : bin->op == T_EQUAL_EQUAL ? "==" : "?") << " '" << r_str << "'" << std::endl;
                    int cmp = l_str.compare(r_str);
                    switch (bin->op) {
                        case T_GREATER: result = (cmp > 0); break;
                        case T_LESS: result = (cmp < 0); break;
                        case T_GREATER_EQUAL: result = (cmp >= 0); break;
                        case T_LESS_EQUAL: result = (cmp <= 0); break;
                        case T_EQUAL_EQUAL: result = (cmp == 0); break;
                        case T_NOT_EQUAL: result = (cmp != 0); break;
                    }
                    if (DEBUG) std::cout << "[DEBUG] Comparison result: " << (result ? "true" : "false") << std::endl;
                } else {
                    // Сравнение чисел
                    double lv, rv;
                    if (l.type == ValueType::FLOAT) lv = l.float_val;
                    else if (l.type == ValueType::INT) lv = l.int_val;
                    else if (l.type == ValueType::BOOL) lv = l.bool_val ? 1.0 : 0.0;
                    else lv = 0.0;

                    if (r.type == ValueType::FLOAT) rv = r.float_val;
                    else if (r.type == ValueType::INT) rv = r.int_val;
                    else if (r.type == ValueType::BOOL) rv = r.bool_val ? 1.0 : 0.0;
                    else rv = 0.0;

                    switch (bin->op) {
                        case T_GREATER: result = (lv > rv); break;
                        case T_LESS: result = (lv < rv); break;
                        case T_GREATER_EQUAL: result = (lv >= rv); break;
                        case T_LESS_EQUAL: result = (lv <= rv); break;
                        case T_EQUAL_EQUAL: result = (lv == rv); break;
                        case T_NOT_EQUAL: result = (lv != rv); break;
                    }
                }
            }
            return Value(result ? 1LL : 0LL);
        }

        // Арифметические операции
        if (l.type == ValueType::FLOAT || r.type == ValueType::FLOAT) {
            double lv = (l.type == ValueType::FLOAT) ? l.float_val : (l.type == ValueType::BOOL ? (l.bool_val ? 1.0 : 0.0) : l.int_val);
            double rv = (r.type == ValueType::FLOAT) ? r.float_val : (r.type == ValueType::BOOL ? (r.bool_val ? 1.0 : 0.0) : r.int_val);
            switch (bin->op) {
                case T_PLUS: return Value(lv + rv);
                case T_MINUS: return Value(lv - rv);
                case T_STAR: return Value(lv * rv);
                case T_SLASH: return Value(rv != 0.0 ? lv / rv : 0.0);
            }
        }
        if ((l.type == ValueType::INT || l.type == ValueType::BOOL) &&
            (r.type == ValueType::INT || r.type == ValueType::BOOL)) {
            long long lv = l.type == ValueType::BOOL ? (l.bool_val ? 1LL : 0LL) : l.int_val;
            long long rv = r.type == ValueType::BOOL ? (r.bool_val ? 1LL : 0LL) : r.int_val;
            switch (bin->op) {
                case T_PLUS: return Value(lv + rv);
                case T_MINUS: return Value(lv - rv);
                case T_STAR: return Value(lv * rv);
                case T_SLASH: return Value(rv != 0 ? lv / rv : 0LL);
            }
        }
        return Value();
    }
    return Value();
}

void Interpreter::log_message(const std::string& message, const std::string& level) {
    std::time_t now = std::time(nullptr);
    char time_str[20];
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    std::string log_entry = "[" + std::string(time_str) + "] [" + normalize_log_level(level) + "] " + message;
    if (log_output == "file" && log_file.is_open()) {
        log_file << log_entry << std::endl;
        log_file.flush();
        return;
    }
    std::cout << log_entry << std::endl;
}
