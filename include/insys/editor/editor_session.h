#pragma once

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace insys::editor {

struct EditorDiagnostic {
    enum class Severity {
        Error,
        Warning,
        Info,
    };

    Severity severity = Severity::Error;
    std::string code;
    std::string message;
    std::string jsonPointer;
    std::size_t line = 0;
    std::size_t column = 0;
};

struct EditorSessionOptions {
    std::vector<std::filesystem::path> assetRoots;
    std::size_t maxAssetBytes = 16u * 1024u * 1024u;
};

struct EditorOpenResult {
    std::string path;
    std::string revision;
    std::string json;
    std::string format = "json";
    bool readOnly = false;
    int schemaVersion = 0;
};

struct EditorSaveResult {
    std::string revision;
    bool changed = false;
    std::string path;
};

struct EditorBakeResult {
    std::string path;
    std::string revision;
    std::size_t bytes = 0;
    bool changed = false;
};

class EditorError final : public std::runtime_error {
public:
    EditorError(std::string code, std::string message,
                std::vector<EditorDiagnostic> diagnostics = {})
        : std::runtime_error(std::move(message)),
          m_code(std::move(code)),
          m_diagnostics(std::move(diagnostics)) {}

    const std::string& code() const noexcept { return m_code; }
    const std::vector<EditorDiagnostic>& diagnostics() const noexcept {
        return m_diagnostics;
    }

private:
    std::string m_code;
    std::vector<EditorDiagnostic> m_diagnostics;
};

class EditorSession {
public:
    explicit EditorSession(EditorSessionOptions options = {});

    EditorOpenResult open(std::string_view path) const;
    std::string new_asset(std::string_view name = {}) const;
    std::vector<EditorDiagnostic> validate(std::string_view json) const;
    EditorSaveResult save(std::string_view path, std::string_view json,
                          std::string_view expectedRevision = {},
                          bool createBackup = false) const;
    EditorBakeResult bake(std::string_view path, std::string_view json,
                          std::string_view expectedRevision = {},
                          bool createBackup = false) const;

    const std::vector<std::filesystem::path>& asset_roots() const {
        return m_options.assetRoots;
    }

private:
    enum class Access {
        Read,
        WriteJson,
        WriteBinary,
    };

    std::filesystem::path resolve(std::string_view path, Access access) const;

    EditorSessionOptions m_options;
};

} // namespace insys::editor
