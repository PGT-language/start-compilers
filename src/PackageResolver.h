#pragma once

#include "Error.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

struct ResolvedImport {
    std::string path;
    std::vector<std::string> files;
    bool found = false;
    bool is_standard = false;
    bool is_package = false;
};

class PackageResolver {
    std::string project_root;
    std::string compiler_root;

    static bool file_exists(const std::string& path) {
        std::ifstream file(path);
        return file.good();
    }

    static bool directory_exists(const std::string& path) {
        return std::filesystem::exists(path) && std::filesystem::is_directory(path);
    }

    static bool starts_with(const std::string& value, const std::string& prefix) {
        return value.rfind(prefix, 0) == 0;
    }

    static bool is_standard_import(const std::string& import_path) {
        return import_path == "std" ||
               starts_with(import_path, "std/") ||
               starts_with(import_path, "std\\");
    }

    static bool is_relative_path(const std::string& path) {
        return starts_with(path, "./") ||
               starts_with(path, "../") ||
               starts_with(path, ".\\") ||
               starts_with(path, "..\\");
    }

    static bool has_pgt_extension(const std::string& path) {
        return path.size() >= 4 && path.substr(path.size() - 4) == ".pgt";
    }

    static std::string with_pgt_extension(const std::string& path) {
        if (has_pgt_extension(path)) {
            return path;
        }
        return path + ".pgt";
    }

    static std::string join_path(const std::string& base, const std::string& child) {
        if (base.empty() || base == ".") {
            return child;
        }
        return (std::filesystem::path(base) / child).string();
    }

    static void add_candidate(std::vector<std::string>& candidates, const std::string& path) {
        for (const auto& candidate : candidates) {
            if (candidate == path) {
                return;
            }
        }
        candidates.push_back(path);
    }

    static void add_directory_candidate(std::vector<std::string>& candidates,
                                        const std::string& base,
                                        const std::string& import_path) {
        if (has_pgt_extension(import_path)) {
            return;
        }
        add_candidate(candidates, join_path(base, import_path));
    }

    static void add_import_candidates(std::vector<std::string>& candidates,
                                      const std::string& base,
                                      const std::string& import_path) {
        add_candidate(candidates, join_path(base, with_pgt_extension(import_path)));
        if (has_pgt_extension(import_path)) {
            return;
        }

        std::filesystem::path package_path(import_path);
        std::string package_file = package_path.filename().string();
        if (!package_file.empty()) {
            package_file += ".pgt";
            add_candidate(candidates, join_path(base, (package_path / package_file).string()));
        }
    }

    static std::string stdlib_path_for(const std::string& import_path) {
        if (import_path == "std") {
            return "src/stdlib";
        }
        if (starts_with(import_path, "std/") || starts_with(import_path, "std\\")) {
            return (std::filesystem::path("src/stdlib") / import_path.substr(4)).string();
        }
        return import_path;
    }

    static std::vector<std::string> collect_package_files(const std::string& directory) {
        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (entry.path().extension() == ".pgt") {
                files.push_back(entry.path().string());
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    static std::string trim(const std::string& value) {
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

    static std::string package_name_from_line(const std::string& line) {
        std::string cleaned = trim(line);
        if (cleaned.rfind("//", 0) == 0 || cleaned.empty()) {
            return "";
        }
        if (cleaned.rfind("package", 0) != 0) {
            return "";
        }

        std::string rest = trim(cleaned.substr(7));
        size_t end = 0;
        while (end < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[end])) || rest[end] == '_')) {
            end++;
        }
        return rest.substr(0, end);
    }

    static std::string read_package_name(const std::string& file_path) {
        std::ifstream file(file_path);
        if (!file) {
            return "";
        }

        std::string line;
        while (std::getline(file, line)) {
            std::string package_name = package_name_from_line(line);
            if (!package_name.empty()) {
                return package_name;
            }
            std::string cleaned = trim(line);
            if (!cleaned.empty() && cleaned.rfind("//", 0) != 0) {
                return "";
            }
        }
        return "";
    }

public:
    PackageResolver(const std::string& entry_file, const std::string& executable_path)
        : project_root(directory_of(entry_file)), compiler_root(directory_of(executable_path)) {
        if (project_root.empty()) {
            project_root = ".";
        }
        if (compiler_root.empty()) {
            compiler_root = ".";
        }
    }

    static std::string directory_of(const std::string& file_path) {
        std::filesystem::path path(file_path);
        std::filesystem::path parent = path.parent_path();
        if (parent.empty()) {
            return ".";
        }
        return parent.string();
    }

    static std::string directory_name(const std::string& directory) {
        if (directory.empty() || directory == ".") {
            return "";
        }
        return std::filesystem::path(directory).filename().string();
    }

    ResolvedImport resolve_import_path(const std::string& base_dir, const std::string& import_path) const {
        std::vector<std::string> file_candidates;
        std::vector<std::string> directory_candidates;
        bool standard = is_standard_import(import_path);

        if (standard) {
            add_import_candidates(file_candidates, compiler_root, import_path);
            add_import_candidates(file_candidates, compiler_root, stdlib_path_for(import_path));
            add_import_candidates(file_candidates, ".", stdlib_path_for(import_path));
            add_directory_candidate(directory_candidates, compiler_root, import_path);
            add_directory_candidate(directory_candidates, compiler_root, stdlib_path_for(import_path));
            add_directory_candidate(directory_candidates, ".", stdlib_path_for(import_path));
        } else if (std::filesystem::path(import_path).is_absolute()) {
            add_import_candidates(file_candidates, "", import_path);
            add_directory_candidate(directory_candidates, "", import_path);
        } else if (is_relative_path(import_path)) {
            add_import_candidates(file_candidates, base_dir, import_path);
            add_directory_candidate(directory_candidates, base_dir, import_path);
        } else {
            add_import_candidates(file_candidates, base_dir, import_path);
            add_import_candidates(file_candidates, project_root, import_path);
            add_import_candidates(file_candidates, ".", import_path);
            add_directory_candidate(directory_candidates, base_dir, import_path);
            add_directory_candidate(directory_candidates, project_root, import_path);
            add_directory_candidate(directory_candidates, ".", import_path);
        }

        for (const auto& candidate : file_candidates) {
            if (file_exists(candidate)) {
                return {candidate, {candidate}, true, standard, false};
            }
        }

        for (const auto& candidate : directory_candidates) {
            if (directory_exists(candidate)) {
                std::vector<std::string> files = collect_package_files(candidate);
                if (!files.empty()) {
                    return {candidate, files, true, standard, true};
                }
            }
        }

        std::string fallback = file_candidates.empty() ? with_pgt_extension(import_path) : file_candidates.front();
        return {fallback, {}, false, standard, false};
    }

    void validate_main_package_root(const std::string& entry_file) const {
        std::string root_dir = directory_of(entry_file);
        for (const auto& entry : std::filesystem::directory_iterator(root_dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".pgt") {
                continue;
            }

            std::string file_path = entry.path().string();
            std::string package_name = read_package_name(file_path);
            if (package_name.empty()) {
                throw SemanticError("File in main package root has no package declaration: '" + file_path + "'",
                                    SourceLocation(1, 0, file_path));
            }
            if (package_name != "main") {
                throw SemanticError("Main package root cannot contain package '" + package_name +
                                    "' in file '" + file_path + "'. Move it into directory '" +
                                    (std::filesystem::path(root_dir) / package_name).string() +
                                    "' or change it to 'package main'.",
                                    SourceLocation(1, 0, file_path));
            }
        }
    }

    void validate_package_directory(const std::string& file_path, const std::string& package_name) const {
        if (package_name == "main") {
            return;
        }

        std::string directory = directory_of(file_path);
        std::string actual_name = directory_name(directory);
        if (actual_name.empty()) {
            return;
        }

        if (actual_name != package_name) {
            throw SemanticError("Package directory mismatch: file declares package '" + package_name +
                                "' but lives in directory '" + actual_name +
                                "'. Rename the directory to '" + package_name +
                                "' or change the package declaration.",
                                SourceLocation(1, 0, file_path));
        }
    }
};