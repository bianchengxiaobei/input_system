#include "insys/editor/editor_session.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "insys/actions/action_asset.h"
#include "insys/asset/asset_loader.h"
#include "insys/asset/json.h"
#include "insys/layout_registry.h"
#include "sha256.h"

namespace insys::editor {
namespace {

[[noreturn]] void fail(const char* code, const std::string& message,
                       std::vector<EditorDiagnostic> diagnostics = {}) {
    throw EditorError(code, message, std::move(diagnostics));
}

std::string strip_utf8_bom(std::string_view text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef &&
        static_cast<unsigned char>(text[1]) == 0xbb &&
        static_cast<unsigned char>(text[2]) == 0xbf) {
        return std::string(text.substr(3));
    }
    return std::string(text);
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string trim_copy(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return std::string(text);
}

int detect_json_schema_version(std::string_view text) {
    asset::JsonValue root;
    asset::JsonParseError error;
    if (!asset::parse_json(text, root, error) || !root.is_object())
        return 0;
    const asset::JsonValue* version = root.find("version");
    if (version == nullptr || !version->is_number())
        return 0;
    const double value = version->number;
    if (!std::isfinite(value) || value < 0.0 || value > 2147483647.0 ||
        std::floor(value) != value) {
        return 0;
    }
    return static_cast<int>(value);
}

int detect_binary_schema_version(std::string_view binary) {
    static constexpr char kMagic[8] = {'I', 'N', 'S', 'Y', 'A', 'S', 'T', '\0'};
    if (binary.size() < 12 || std::memcmp(binary.data(), kMagic, 8) != 0)
        return 0;
    const auto byte = [&](std::size_t index) {
        return static_cast<std::uint32_t>(
            static_cast<unsigned char>(binary[index]));
    };
    return static_cast<int>(byte(8) | (byte(9) << 8u) | (byte(10) << 16u) |
                            (byte(11) << 24u));
}

std::vector<std::string> split_groups(std::string_view groups) {
    std::vector<std::string> result;
    while (!groups.empty()) {
        const std::size_t separator = groups.find(';');
        std::string token = trim_copy(
            separator == std::string_view::npos ? groups : groups.substr(0, separator));
        if (!token.empty())
            result.push_back(std::move(token));
        if (separator == std::string_view::npos)
            break;
        groups.remove_prefix(separator + 1);
    }
    return result;
}

std::filesystem::path normalize_path(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
    if (error)
        normalized = std::filesystem::absolute(path, error);
    if (error)
        normalized = path.lexically_normal();
    return normalized;
}

bool is_within(const std::filesystem::path& candidate,
               const std::filesystem::path& root) {
    const std::filesystem::path relative = candidate.lexically_relative(root);
    if (relative.empty() || relative == ".")
        return false;
    if (relative.is_absolute())
        return false;
    for (const std::filesystem::path& component : relative) {
        if (component == "..")
            return false;
    }
    return true;
}

std::string revision_for(std::string_view content) {
    return "sha256:" + detail::sha256_hex(content);
}

std::string read_file(const std::filesystem::path& path, std::size_t maxBytes,
                      bool stripBom = true) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error)
        fail("io_error", "cannot inspect asset file '" + path.string() + "'");
    if (size > maxBytes)
        fail("file_too_large", "asset file exceeds the configured size limit");

    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        fail("io_error", "cannot open asset file '" + path.string() + "'");
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof())
        fail("io_error", "cannot read asset file '" + path.string() + "'");
    std::string content = buffer.str();
    return stripBom ? strip_utf8_bom(content) : content;
}

std::string unique_suffix() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device device;
    return std::to_string(now) + "." + std::to_string(device());
}

void write_temp_file(const std::filesystem::path& path, std::string_view content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        fail("io_error", "cannot create temporary asset file '" + path.string() + "'");
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    stream.flush();
    if (!stream)
        fail("io_error", "cannot write temporary asset file '" + path.string() + "'");
}

void replace_file(const std::filesystem::path& source,
                  const std::filesystem::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(source.wstring().c_str(), destination.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fail("io_error", "cannot replace asset file '" + destination.string() + "'");
    }
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (error)
        fail("io_error", "cannot replace asset file '" + destination.string() + "'");
#endif
}

std::string child_pointer(std::string_view parent, std::string_view field) {
    std::string result(parent);
    result += "/";
    result += field;
    return result;
}

std::string item_pointer(std::string_view parent, std::string_view field,
                         std::size_t index) {
    std::string result = child_pointer(parent, field);
    result += "/";
    result += std::to_string(index);
    return result;
}

void add_guid_diagnostic(std::vector<EditorDiagnostic>& diagnostics,
                         std::unordered_map<std::string, std::string>& seen,
                         const Guid& id, const std::string& jsonPointer) {
    const std::string key = id.to_string();
    const auto inserted = seen.emplace(key, jsonPointer);
    if (!inserted.second) {
        diagnostics.push_back(
            {EditorDiagnostic::Severity::Error, "duplicate-guid",
             "duplicate GUID '" + key + "' (first at " + inserted.first->second + ")",
             jsonPointer, 0, 0});
    }
}

} // namespace

EditorSession::EditorSession(EditorSessionOptions options)
    : m_options(std::move(options)) {
    if (m_options.assetRoots.empty())
        m_options.assetRoots.push_back(std::filesystem::current_path());

    for (std::filesystem::path& root : m_options.assetRoots)
        root = normalize_path(root);
}

std::filesystem::path EditorSession::resolve(std::string_view path,
                                             Access access) const {
    const std::string value(path);
    if (value.empty())
        fail("invalid_path", "asset path must not be empty");

    const std::filesystem::path input(value);
    std::filesystem::path candidate;
    if (input.is_absolute()) {
        candidate = input;
    } else {
        if (m_options.assetRoots.empty())
            fail("path_denied", "no asset root is configured");
        candidate = m_options.assetRoots.front() / input;
    }

    const std::filesystem::path normalized = normalize_path(candidate);
    bool insideRoot = false;
    for (const std::filesystem::path& root : m_options.assetRoots) {
        if (is_within(normalized, root)) {
            insideRoot = true;
            break;
        }
    }
    if (!insideRoot)
        fail("path_denied", "asset path is outside the configured asset roots");

    const std::string extension = lowercase(normalized.extension().string());
    const bool allowed =
        access == Access::Read
            ? (extension == ".json" || extension == ".insysb")
            : (access == Access::WriteJson ? extension == ".json"
                                           : extension == ".insysb");
    if (!allowed) {
        if (access == Access::Read)
            fail("unsupported_format", "only .json and .insysb assets can be opened");
        if (access == Access::WriteJson)
            fail("unsupported_format", "only .json assets can be saved");
        fail("unsupported_format", "baked assets must use the .insysb extension");
    }

    if (access != Access::Read) {
        std::error_code error;
        const std::filesystem::path parent = normalized.parent_path();
        if (!std::filesystem::is_directory(parent, error) || error)
            fail("not_found", "asset directory does not exist: '" + parent.string() + "'");
        return normalized;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(normalized, error) || error)
        fail("not_found", "asset file does not exist: '" + normalized.string() + "'");
    return normalized;
}

EditorOpenResult EditorSession::open(std::string_view path) const {
    const std::filesystem::path resolved = resolve(path, Access::Read);
    const std::string extension = lowercase(resolved.extension().string());
    const bool binary = extension == ".insysb";
    std::string content =
        read_file(resolved, m_options.maxAssetBytes, !binary);
    const std::string revision = revision_for(content);
    if (!binary) {
        const int schemaVersion = detect_json_schema_version(content);
        return {resolved.string(), revision, std::move(content), "json", false,
                schemaVersion};
    }

    const int schemaVersion = detect_binary_schema_version(content);
    try {
        const ActionAsset imported = asset::AssetLoader::load_from_binary(content);
        return {resolved.string(), revision,
                asset::AssetLoader::to_json(imported), "insysb", true,
                schemaVersion};
    } catch (const asset::AssetError& error) {
        const std::string message = error.what();
        if (message.find("unsupported version") != std::string::npos) {
            fail("schema_unsupported",
                 "binary asset schema version " +
                     std::to_string(schemaVersion) +
                     " is not supported; this editor supports version 1",
                 {EditorDiagnostic{EditorDiagnostic::Severity::Error,
                                   "asset.schema",
                                   "binary asset migration is required before import",
                                   "", 0, 0}});
        }
        fail("unsupported_format", "cannot import .insysb asset: " + message);
    }
}

std::string EditorSession::new_asset(std::string_view name) const {
    ActionAsset asset(name.empty() ? "New Action Asset" : std::string(name));
    return asset::AssetLoader::to_json(asset);
}

std::vector<EditorDiagnostic> EditorSession::validate(std::string_view json) const {
    std::vector<EditorDiagnostic> diagnostics;
    const std::string text = strip_utf8_bom(json);

    ActionAsset asset;
    try {
        asset = asset::AssetLoader::load_from_json(text);
    } catch (const asset::AssetError& error) {
        const std::string message = error.what();
        const bool schemaError =
            message.find("unsupported asset version") != std::string::npos;
        const int schemaVersion = detect_json_schema_version(text);
        const std::string schemaMessage =
            schemaVersion > 0
                ? "unsupported asset schema version " +
                      std::to_string(schemaVersion) +
                      " (supported: 1); migration is required"
                : "unsupported asset schema version (supported: 1); "
                  "migration is required";
        diagnostics.push_back(
            {EditorDiagnostic::Severity::Error,
             schemaError ? "asset.schema" : "asset.parse",
             schemaError ? schemaMessage : message, {}, error.line(),
             error.column()});
        return diagnostics;
    } catch (const std::exception& error) {
        diagnostics.push_back({EditorDiagnostic::Severity::Error, "asset.error",
                               error.what(), {}, 0, 0});
        return diagnostics;
    }

    if (asset.maps.empty()) {
        diagnostics.push_back({EditorDiagnostic::Severity::Warning, "asset.empty",
                               "asset has no action maps", "", 0, 0});
    }

    std::unordered_map<std::string, std::string> schemeGroups;
    for (std::size_t schemeIndex = 0; schemeIndex < asset.controlSchemes.size();
         ++schemeIndex) {
        const ControlScheme& scheme = asset.controlSchemes[schemeIndex];
        const std::string schemePointer = item_pointer("", "schemes", schemeIndex);
        if (scheme.bindingGroup.empty()) {
            diagnostics.push_back(
                {EditorDiagnostic::Severity::Warning, "scheme.missing-group",
                 "control scheme '" + scheme.name + "' has no binding group",
                 child_pointer(schemePointer, "bindingGroup"), 0, 0});
        } else {
            const std::string key = lowercase(scheme.bindingGroup);
            const auto inserted = schemeGroups.emplace(key, schemePointer);
            if (!inserted.second) {
                diagnostics.push_back(
                    {EditorDiagnostic::Severity::Warning, "scheme.duplicate-group",
                     "duplicate control scheme binding group '" +
                         scheme.bindingGroup + "' (first at " +
                         inserted.first->second + ")",
                     child_pointer(schemePointer, "bindingGroup"), 0, 0});
            }
        }

        std::unordered_set<std::string> seenDevices;
        for (std::size_t deviceIndex = 0; deviceIndex < scheme.devices.size();
             ++deviceIndex) {
            const std::string& device = scheme.devices[deviceIndex];
            const std::string pointer =
                child_pointer(schemePointer, "devices") + "/" +
                std::to_string(deviceIndex);
            if (default_layout_registry().find(device) == nullptr) {
                diagnostics.push_back(
                    {EditorDiagnostic::Severity::Warning, "scheme.unknown-device",
                     "unknown device layout '" + device + "'", pointer, 0, 0});
            }
            if (!seenDevices.insert(lowercase(device)).second) {
                diagnostics.push_back(
                    {EditorDiagnostic::Severity::Warning, "scheme.duplicate-device",
                     "duplicate device layout '" + device + "'", pointer, 0, 0});
            }
        }
    }

    std::unordered_map<std::string, std::string> guids;
    for (std::size_t mapIndex = 0; mapIndex < asset.maps.size(); ++mapIndex) {
        const ActionMap& map = asset.maps[mapIndex];
        const std::string mapPointer = item_pointer("", "maps", mapIndex);
        add_guid_diagnostic(diagnostics, guids, map.id,
                            child_pointer(mapPointer, "id"));

        if (map.actions.empty() && map.bindings.empty()) {
            diagnostics.push_back({EditorDiagnostic::Severity::Warning, "map.empty",
                                   "action map '" + map.name + "' is empty", mapPointer,
                                   0, 0});
        }

        std::unordered_set<std::string> boundActions;
        for (std::size_t bindingIndex = 0; bindingIndex < map.bindings.size();
             ++bindingIndex) {
            const Binding& binding = map.bindings[bindingIndex];
            const std::size_t slash = binding.action.find_last_of('/');
            boundActions.insert(slash == std::string::npos
                                    ? binding.action
                                    : binding.action.substr(slash + 1));
            const std::string bindingPointer =
                item_pointer(mapPointer, "bindings", bindingIndex);
            add_guid_diagnostic(diagnostics, guids, binding.id,
                                child_pointer(bindingPointer, "id"));

            if (!binding.isPartOfComposite) {
                for (const std::string& group : split_groups(binding.groups)) {
                    if (schemeGroups.find(lowercase(group)) == schemeGroups.end()) {
                        diagnostics.push_back(
                            {EditorDiagnostic::Severity::Warning,
                             "binding.unknown-group",
                             "binding references unknown control scheme group '" +
                                 group + "'",
                             child_pointer(bindingPointer, "groups"), 0, 0});
                    }
                }
            }
        }

        for (std::size_t actionIndex = 0; actionIndex < map.actions.size();
             ++actionIndex) {
            const ActionDef& action = map.actions[actionIndex];
            const std::string actionPointer =
                item_pointer(mapPointer, "actions", actionIndex);
            add_guid_diagnostic(diagnostics, guids, action.id,
                                child_pointer(actionPointer, "id"));
            if (boundActions.find(action.name) == boundActions.end()) {
                diagnostics.push_back(
                    {EditorDiagnostic::Severity::Warning, "action.unbound",
                     "action '" + action.name + "' has no bindings", actionPointer, 0,
                     0});
            }
        }
    }

    for (std::size_t schemeIndex = 0; schemeIndex < asset.controlSchemes.size();
         ++schemeIndex) {
        const ControlScheme& scheme = asset.controlSchemes[schemeIndex];
        add_guid_diagnostic(diagnostics, guids, scheme.id,
                            child_pointer(item_pointer("", "schemes", schemeIndex),
                                          "id"));
    }

    return diagnostics;
}

EditorSaveResult EditorSession::save(std::string_view path, std::string_view json,
                                     std::string_view expectedRevision,
                                     bool createBackup) const {
    const std::filesystem::path resolved = resolve(path, Access::WriteJson);
    const std::string normalized = strip_utf8_bom(json);
    const std::vector<EditorDiagnostic> diagnostics = validate(normalized);
    for (const EditorDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == EditorDiagnostic::Severity::Error)
            fail("validation_failed", "asset validation failed", diagnostics);
    }

    const ActionAsset asset = asset::AssetLoader::load_from_json(normalized);
    const std::string canonical = asset::AssetLoader::to_json(asset);

    std::error_code error;
    const bool exists = std::filesystem::exists(resolved, error);
    if (error)
        fail("io_error", "cannot inspect asset file '" + resolved.string() + "'");
    if (exists) {
        const bool regular = std::filesystem::is_regular_file(resolved, error);
        if (error || !regular)
            fail("invalid_path",
                 "asset path is not a regular file: '" + resolved.string() + "'");
    }
    std::string existing;
    if (exists)
        existing = read_file(resolved, m_options.maxAssetBytes);

    if (exists) {
        if (expectedRevision.empty() || revision_for(existing) != expectedRevision)
            fail("revision_conflict", "asset file changed since it was opened");
        if (existing == canonical)
            return {revision_for(existing), false, resolved.string()};
    } else if (!expectedRevision.empty()) {
        fail("revision_conflict", "asset file was removed since it was opened");
    }

    std::filesystem::path temp = resolved;
    temp += ".tmp." + unique_suffix();
    try {
        write_temp_file(temp, canonical);
        if (createBackup && exists) {
            std::filesystem::path backup = resolved;
            backup += ".bak";
            std::filesystem::copy_file(resolved, backup,
                                       std::filesystem::copy_options::overwrite_existing,
                                       error);
            if (error) {
                std::filesystem::remove(temp, error);
                fail("io_error", "cannot create backup for '" + resolved.string() + "'");
            }
        }
        replace_file(temp, resolved);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw;
    }

    return {revision_for(canonical), true, resolved.string()};
}

EditorBakeResult EditorSession::bake(std::string_view path, std::string_view json,
                                     std::string_view expectedRevision,
                                     bool createBackup) const {
    const std::filesystem::path resolved = resolve(path, Access::WriteBinary);
    const std::string normalized = strip_utf8_bom(json);
    const std::vector<EditorDiagnostic> diagnostics = validate(normalized);
    for (const EditorDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == EditorDiagnostic::Severity::Error)
            fail("validation_failed", "asset validation failed", diagnostics);
    }

    const ActionAsset asset = asset::AssetLoader::load_from_json(normalized);
    const std::string binary = asset::AssetLoader::to_binary(asset);

    std::error_code error;
    const bool exists = std::filesystem::exists(resolved, error);
    if (error)
        fail("io_error", "cannot inspect asset file '" + resolved.string() + "'");
    if (exists) {
        const bool regular = std::filesystem::is_regular_file(resolved, error);
        if (error || !regular)
            fail("invalid_path",
                 "asset path is not a regular file: '" + resolved.string() + "'");
    }
    std::string existing;
    if (exists)
        existing = read_file(resolved, m_options.maxAssetBytes, false);

    if (exists) {
        if (expectedRevision.empty() || revision_for(existing) != expectedRevision)
            fail("revision_conflict", "baked file changed since it was opened");
        if (existing == binary)
            return {resolved.string(), revision_for(existing), binary.size(), false};
    } else if (!expectedRevision.empty()) {
        fail("revision_conflict", "baked file was removed since it was opened");
    }

    std::filesystem::path temp = resolved;
    temp += ".tmp." + unique_suffix();
    try {
        write_temp_file(temp, binary);
        if (createBackup && exists) {
            std::filesystem::path backup = resolved;
            backup += ".bak";
            std::filesystem::copy_file(resolved, backup,
                                       std::filesystem::copy_options::overwrite_existing,
                                       error);
            if (error) {
                std::filesystem::remove(temp, error);
                fail("io_error",
                     "cannot create backup for '" + resolved.string() + "'");
            }
        }
        replace_file(temp, resolved);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw;
    }

    return {resolved.string(), revision_for(binary), binary.size(), true};
}

} // namespace insys::editor
