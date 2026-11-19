#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "insys/asset/asset_loader.h"
#include "insys/editor/editor_session.h"

using namespace insys;

namespace {

std::filesystem::path make_temp_directory() {
    const auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("insys_editor_" + stamp);
    std::filesystem::create_directories(path);
    return path;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    REQUIRE(stream.good());
}

const char* kAsset =
    "{\n"
    "  \"version\": 1,\n"
    "  \"name\": \"Player\",\n"
    "  \"maps\": [{\n"
    "    \"name\": \"Player\",\n"
    "    \"actions\": [\n"
    "      {\"name\": \"Jump\", \"type\": \"button\", \"priority\": 5}\n"
    "    ],\n"
    "    \"bindings\": [\n"
    "      {\"action\": \"Jump\", \"path\": \"<Keyboard>/space\"}\n"
    "    ]\n"
    "  }]\n"
    "}\n";

} // namespace

TEST_CASE("Editor session opens validates saves and reopens assets") {
    const std::filesystem::path root = make_temp_directory();
    const std::filesystem::path assetPath = root / "player.json";
    write_text(assetPath, kAsset);

    editor::EditorSession session(editor::EditorSessionOptions{{root}});
    const editor::EditorOpenResult opened = session.open("player.json");
    REQUIRE(opened.path == std::filesystem::weakly_canonical(assetPath).string());
    REQUIRE(opened.revision.rfind("sha256:", 0) == 0);
    REQUIRE(opened.json.find("\"priority\": 5") != std::string::npos);

    const auto diagnostics = session.validate(opened.json);
    REQUIRE(diagnostics.empty());

    std::string edited = opened.json;
    const std::size_t position = edited.find("\"priority\": 5");
    REQUIRE(position != std::string::npos);
    edited.replace(position, 13, "\"priority\": 9");

    const editor::EditorSaveResult saved =
        session.save("player.json", edited, opened.revision, true);
    REQUIRE(saved.changed);
    REQUIRE(saved.revision.rfind("sha256:", 0) == 0);
    REQUIRE(saved.revision != opened.revision);
    REQUIRE(std::filesystem::exists(assetPath.string() + ".bak"));
    REQUIRE(read_text(assetPath.string() + ".bak").find("\"priority\": 5") !=
            std::string::npos);

    ActionAsset reloaded = asset::AssetLoader::load_from_file(assetPath);
    REQUIRE(reloaded.find_action("Jump")->priority == 9);

    const editor::EditorOpenResult reopened = session.open("player.json");
    const editor::EditorSaveResult unchanged =
        session.save("player.json", reopened.json, reopened.revision, false);
    REQUIRE_FALSE(unchanged.changed);
    REQUIRE(unchanged.revision == reopened.revision);
}

TEST_CASE("Editor session rejects stale revisions and path escapes") {
    const std::filesystem::path root = make_temp_directory();
    const std::filesystem::path assetPath = root / "player.json";
    write_text(assetPath, kAsset);

    editor::EditorSession session(editor::EditorSessionOptions{{root}});
    const editor::EditorOpenResult opened = session.open("player.json");
    write_text(assetPath, std::string(kAsset) + "\n");

    try {
        (void)session.save("player.json", opened.json, opened.revision, false);
        FAIL("expected revision conflict");
    } catch (const editor::EditorError& error) {
        REQUIRE(error.code() == "revision_conflict");
    }

    try {
        (void)session.open("../outside.json");
        FAIL("expected path denial");
    } catch (const editor::EditorError& error) {
        REQUIRE(error.code() == "path_denied");
    }
}

TEST_CASE("Editor session uses SHA-256 revisions") {
    const std::filesystem::path root = make_temp_directory();
    write_text(root / "hash.json", "abc");

    editor::EditorSession session(editor::EditorSessionOptions{{root}});
    const editor::EditorOpenResult opened = session.open("hash.json");
    REQUIRE(opened.revision ==
            "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("Editor session reports JSON diagnostics") {
    editor::EditorSession session;
    const auto diagnostics = session.validate("{\"version\": 1, \"maps\": [}");
    REQUIRE_FALSE(diagnostics.empty());
    REQUIRE(diagnostics.front().severity == editor::EditorDiagnostic::Severity::Error);
    REQUIRE(diagnostics.front().code == "asset.parse");
    REQUIRE(diagnostics.front().line > 0);
    REQUIRE(diagnostics.front().column > 0);

    const std::string duplicateGuid = R"({
      "version": 1,
      "maps": [
        {"name": "A", "id": "00000000000000000000000000000001"},
        {"name": "B", "id": "00000000000000000000000000000001"}
      ]
    })";
    const auto duplicateDiagnostics = session.validate(duplicateGuid);
    bool foundDuplicate = false;
    for (const editor::EditorDiagnostic& diagnostic : duplicateDiagnostics) {
        if (diagnostic.code == "duplicate-guid")
            foundDuplicate = true;
    }
    REQUIRE(foundDuplicate);

    const std::string created = session.new_asset("Fresh");
    REQUIRE(created.find("\"version\": 1") != std::string::npos);
    REQUIRE(created.find("\"name\": \"Fresh\"") != std::string::npos);
}

TEST_CASE("Editor session saves a new asset inside an allowed root") {
    const std::filesystem::path root = make_temp_directory();
    editor::EditorSession session(editor::EditorSessionOptions{{root}});
    const std::string json = session.new_asset("Created");
    const editor::EditorSaveResult saved = session.save("created.json", json);
    REQUIRE(saved.changed);
    REQUIRE(std::filesystem::is_regular_file(root / "created.json"));
    REQUIRE(read_text(root / "created.json").find("\"name\": \"Created\"") !=
            std::string::npos);
}

TEST_CASE("Editor session reports semantic scheme diagnostics") {
    editor::EditorSession session;
    const std::string json = R"({
      "version": 1,
      "maps": [{
        "name": "Gameplay",
        "actions": [{"name": "Jump"}],
        "bindings": [{
          "action": "Jump",
          "path": "<Keyboard>/space",
          "groups": "Keyboard"
        }]
      }],
      "schemes": [{
        "name": "Gamepad",
        "bindingGroup": "Gamepad",
        "devices": ["Gamepad", "MadeUp"]
      }]
    })";

    const auto diagnostics = session.validate(json);
    bool foundGroup = false;
    bool foundDevice = false;
    for (const editor::EditorDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.code == "binding.unknown-group")
            foundGroup = true;
        if (diagnostic.code == "scheme.unknown-device")
            foundDevice = true;
    }
    REQUIRE(foundGroup);
    REQUIRE(foundDevice);
}

TEST_CASE("Editor session imports and bakes binary assets") {
    const std::filesystem::path root = make_temp_directory();
    const std::filesystem::path jsonPath = root / "player.json";
    write_text(jsonPath, kAsset);

    editor::EditorSession session(editor::EditorSessionOptions{{root}});
    const editor::EditorOpenResult opened = session.open("player.json");
    REQUIRE(opened.format == "json");
    REQUIRE_FALSE(opened.readOnly);
    REQUIRE(opened.schemaVersion == 1);

    const editor::EditorBakeResult baked =
        session.bake("player.insysb", opened.json);
    REQUIRE(baked.changed);
    REQUIRE(baked.bytes > 0);
    REQUIRE(std::filesystem::is_regular_file(root / "player.insysb"));

    const editor::EditorOpenResult imported = session.open("player.insysb");
    REQUIRE(imported.format == "insysb");
    REQUIRE(imported.readOnly);
    REQUIRE(imported.schemaVersion == 1);
    REQUIRE(imported.json.find("\"priority\": 5") != std::string::npos);

    const editor::EditorBakeResult unchanged =
        session.bake("player.insysb", imported.json, imported.revision);
    REQUIRE_FALSE(unchanged.changed);
    REQUIRE(unchanged.revision == imported.revision);

    try {
        (void)session.bake("player.insysb", imported.json);
        FAIL("expected baked revision conflict");
    } catch (const editor::EditorError& error) {
        REQUIRE(error.code() == "revision_conflict");
    }

    try {
        (void)session.save("player.insysb", imported.json, imported.revision);
        FAIL("expected binary save rejection");
    } catch (const editor::EditorError& error) {
        REQUIRE(error.code() == "unsupported_format");
    }
}

TEST_CASE("Editor session reports schema migration errors") {
    editor::EditorSession session;
    const auto diagnostics =
        session.validate(R"({"version": 2, "maps": []})");
    REQUIRE_FALSE(diagnostics.empty());
    REQUIRE(diagnostics.front().code == "asset.schema");
    REQUIRE(diagnostics.front().message.find("migration") != std::string::npos);

    const std::filesystem::path root = make_temp_directory();
    write_text(root / "asset.json", kAsset);
    editor::EditorSession binarySession(editor::EditorSessionOptions{{root}});
    const editor::EditorOpenResult opened = binarySession.open("asset.json");
    REQUIRE(binarySession.bake("future.insysb", opened.json).changed);
    std::string binary = read_text(root / "future.insysb");
    REQUIRE(binary.size() >= 12);
    binary[8] = 2;
    binary[9] = 0;
    binary[10] = 0;
    binary[11] = 0;
    write_text(root / "future.insysb", binary);

    try {
        (void)binarySession.open("future.insysb");
        FAIL("expected future binary schema rejection");
    } catch (const editor::EditorError& error) {
        REQUIRE(error.code() == "schema_unsupported");
        REQUIRE(error.diagnostics().size() == 1);
        REQUIRE(error.diagnostics().front().code == "asset.schema");
    }
}

TEST_CASE("Editor session serializes canonical JSON in stable order") {
    editor::EditorSession session;
    const std::string text = session.new_asset("Stable");
    const std::string first = asset::AssetLoader::to_json(
        asset::AssetLoader::load_from_json(text));
    const std::string second = asset::AssetLoader::to_json(
        asset::AssetLoader::load_from_json(first));
    REQUIRE(first == second);
    REQUIRE(first.find("\"version\": 1") < first.find("\"name\": \"Stable\""));
    REQUIRE(first.find("\"name\": \"Stable\"") < first.find("\"maps\": []"));
}
