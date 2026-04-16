#pragma once

#include "Error.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

struct ResolvedImport {
    std::string path;
    bool found = false;
    bool is_standard = false;
};

class PackageResolver {
    std::string project_root;
    std::string compiler_root;

    static bool file_exists(const std::string& path) {
        std::ifstream file(path);
        return file.good();
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
            return "stdlib";
        }
        if (starts_with(import_path, "std/") || starts_with(import_path, "std\\")) {
            return (std::filesystem::path("stdlib") / import_path.substr(4)).string();
        }
        return import_path;
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
        std::vector<std::string> candidates;
        bool standard = is_standard_import(import_path);

        if (standard) {
            add_import_candidates(candidates, compiler_root, import_path);
            add_import_candidates(candidates, compiler_root, stdlib_path_for(import_path));
        } else if (std::filesystem::path(import_path).is_absolute()) {
            add_import_candidates(candidates, "", import_path);
        } else if (is_relative_path(import_path)) {
            add_import_candidates(candidates, base_dir, import_path);
        } else {
            add_import_candidates(candidates, base_dir, import_path);
            add_import_candidates(candidates, project_root, import_path);
            add_import_candidates(candidates, ".", import_path);
        }

        for (const auto& candidate : candidates) {
            if (file_exists(candidate)) {
                return {candidate, true, standard};
            }
        }

        std::string fallback = candidates.empty() ? with_pgt_extension(import_path) : candidates.front();
        return {fallback, false, standard};
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