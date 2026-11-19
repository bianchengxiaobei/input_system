#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "insys/asset/json.h"
#include "insys/editor/editor_protocol.h"

using namespace insys;

namespace {

std::filesystem::path make_temp_directory() {
    const auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("insys_editor_protocol_" + stamp);
    std::filesystem::create_directories(path);
    return path;
}

asset::JsonValue parse_response(const std::string& text) {
    asset::JsonValue value;
    asset::JsonParseError error;
    REQUIRE(asset::parse_json(text, value, error));
    return value;
}

std::string string_field(const asset::JsonValue& value, std::string_view name) {
    const asset::JsonValue* field = value.find(name);
    REQUIRE(field != nullptr);
    REQUIRE(field->is_string());
    return field->string;
}

std::string escape_json(std::string_view text) {
    std::string result;
    result.reserve(text.size() + 16);
    for (unsigned char character : text) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 0x20) {
                static constexpr char kHex[] = "0123456789abcdef";
                result += "\\u00";
                result.push_back(kHex[(character >> 4) & 0xf]);
                result.push_back(kHex[character & 0xf]);
            } else {
                result.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    return result;
}

const char* kAsset =
    "{\n"
    "  \"version\": 1,\n"
    "  \"maps\": [{\n"
    "    \"name\": \"Player\",\n"
    "    \"actions\": [{\"name\": \"Jump\", \"priority\": 5}],\n"
    "    \"bindings\": [{\"action\": \"Jump\", \"path\": \"<Keyboard>/space\"}]\n"
    "  }]\n"
    "}\n";

} // namespace

TEST_CASE("Editor protocol implements hello and asset lifecycle") {
    const std::filesystem::path root = make_temp_directory();
    {
        std::ofstream stream(root / "player.json", std::ios::binary);
        stream << kAsset;
    }

    editor::EditorSession session(editor::EditorSessionOptions{{root}});
    editor::EditorProtocol protocol(session);

    const auto hello = protocol.handle(
        R"({"jsonrpc":"2.0","id":1,"method":"session.hello","params":{"protocolVersion":"1"}})");
    REQUIRE(hello.has_value());
    const asset::JsonValue helloValue = parse_response(*hello);
    const asset::JsonValue* helloResult = helloValue.find("result");
    REQUIRE(helloResult != nullptr);
    REQUIRE(string_field(*helloResult, "serverVersion") == "0.1.0");
    REQUIRE(string_field(*helloResult, "protocolVersion") == "1");

    const auto opened = protocol.handle(
        R"({"jsonrpc":"2.0","id":2,"method":"asset.open","params":{"path":"player.json"}})");
    REQUIRE(opened.has_value());
    const asset::JsonValue openedValue = parse_response(*opened);
    const asset::JsonValue* openedResult = openedValue.find("result");
    REQUIRE(openedResult != nullptr);
    const std::string revision = string_field(*openedResult, "revision");
    const std::string json = string_field(*openedResult, "json");
    REQUIRE(revision.rfind("sha256:", 0) == 0);
    REQUIRE(json.find("\"priority\": 5") != std::string::npos);

    std::string edited = json;
    edited.replace(edited.find("\"priority\": 5"), 13, "\"priority\": 7");
    const std::string saveRequest =
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"asset.save\",\"params\":{"
        "\"path\":\"player.json\",\"json\":\"" +
        escape_json(edited) + "\",\"expectedRevision\":\"" + revision + "\"}}";
    const auto saved = protocol.handle(saveRequest);
    REQUIRE(saved.has_value());
    const asset::JsonValue savedValue = parse_response(*saved);
    const asset::JsonValue* savedResult = savedValue.find("result");
    REQUIRE(savedResult != nullptr);
    REQUIRE(string_field(*savedResult, "revision") != revision);
    REQUIRE(savedResult->find("changed")->boolean == true);

    const auto reopened = protocol.handle(
        R"({"jsonrpc":"2.0","id":4,"method":"asset.open","params":{"path":"player.json"}})");
    REQUIRE(reopened.has_value());
    const asset::JsonValue reopenedValue = parse_response(*reopened);
    REQUIRE(string_field(*reopenedValue.find("result"), "json").find("\"priority\": 7") !=
            std::string::npos);
}

TEST_CASE("Editor protocol serves the catalog") {
    editor::EditorSession session;
    editor::EditorProtocol protocol(session);

    const auto hello = protocol.handle(
        R"({"jsonrpc":"2.0","id":1,"method":"session.hello","params":{}})");
    REQUIRE(hello.has_value());
    const asset::JsonValue helloValue = parse_response(*hello);
    const asset::JsonValue* capabilities = helloValue.find("result")->find("capabilities");
    REQUIRE(capabilities != nullptr);
    bool hasCatalog = false;
    for (const asset::JsonValue& capability : capabilities->array) {
        if (capability.is_string() && capability.string == "catalog.get")
            hasCatalog = true;
    }
    REQUIRE(hasCatalog);

    const auto response = protocol.handle(
        R"({"jsonrpc":"2.0","id":2,"method":"catalog.get","params":{}})");
    REQUIRE(response.has_value());
    const asset::JsonValue value = parse_response(*response);
    const asset::JsonValue* result = value.find("result");
    REQUIRE(result != nullptr);
    REQUIRE(result->find("layouts") != nullptr);
    REQUIRE(result->find("devices") != nullptr);
    REQUIRE(result->find("controlTypes") != nullptr);
    REQUIRE(result->find("composites") != nullptr);
    REQUIRE(result->find("interactions") != nullptr);
    REQUIRE(result->find("processors") != nullptr);
    REQUIRE(!result->find("layouts")->array.empty());
}

TEST_CASE("Editor protocol previews actions and captures controls") {
    editor::EditorSession session;
    editor::EditorProtocolOptions options;
    options.enablePreviewHid = false;
    editor::EditorProtocol protocol(session, options);

    const std::string startRequest =
        std::string("{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"preview.start\","
                    "\"params\":{\"json\":\"") +
        escape_json(kAsset) + "\"}}";
    const auto started = protocol.handle(startRequest);
    REQUIRE(started.has_value());
    const asset::JsonValue startedValue = parse_response(*started);
    const asset::JsonValue* startedResult = startedValue.find("result");
    REQUIRE(startedResult != nullptr);
    REQUIRE(startedResult->find("generation")->number == 2.0);

    const auto pressed = protocol.handle(
        R"({"jsonrpc":"2.0","id":11,"method":"preview.input.key","params":{"code":"Space","pressed":true}})");
    REQUIRE(pressed.has_value());
    REQUIRE(parse_response(*pressed).find("result")->find("recognized")->boolean);

    const auto snapshot = protocol.handle(
        R"({"jsonrpc":"2.0","id":12,"method":"preview.snapshot","params":{}})");
    REQUIRE(snapshot.has_value());
    const asset::JsonValue snapshotValue = parse_response(*snapshot);
    const asset::JsonValue* snapshotResult = snapshotValue.find("result");
    REQUIRE(snapshotResult != nullptr);
    const asset::JsonValue* actions = snapshotResult->find("actions");
    REQUIRE(actions != nullptr);
    REQUIRE(actions->array.size() == 1);
    REQUIRE(string_field(actions->array[0], "phase") == "Performed");

    protocol.handle(
        R"({"jsonrpc":"2.0","id":13,"method":"preview.input.key","params":{"code":"Space","pressed":false}})");
    const auto capture = protocol.handle(
        R"({"jsonrpc":"2.0","id":14,"method":"capture.start","params":{"expectedControlType":"Button","deviceFilter":"Keyboard"}})");
    REQUIRE(capture.has_value());
    REQUIRE(parse_response(*capture).find("result")->find("captureId") != nullptr);
    protocol.handle(
        R"({"jsonrpc":"2.0","id":15,"method":"preview.input.key","params":{"code":"Space","pressed":true}})");

    const auto captured = protocol.handle(
        R"({"jsonrpc":"2.0","id":16,"method":"preview.snapshot","params":{}})");
    REQUIRE(captured.has_value());
    const asset::JsonValue capturedValue = parse_response(*captured);
    const asset::JsonValue* capturedResult = capturedValue.find("result");
    REQUIRE(capturedResult != nullptr);
    const asset::JsonValue* capturedControl = capturedResult->find("captured");
    REQUIRE(capturedControl != nullptr);
    REQUIRE(capturedControl->is_object());
    REQUIRE(string_field(*capturedControl, "path") == "<Keyboard>/space");
}

TEST_CASE("Editor protocol exposes HID tool methods without devices") {
    editor::EditorSession session(editor::EditorSessionOptions{});
    editor::EditorProtocol protocol(session);

    const auto hello = protocol.handle(
        R"({"jsonrpc":"2.0","id":1,"method":"session.hello","params":{}})");
    REQUIRE(hello.has_value());
    const asset::JsonValue helloValue = parse_response(*hello);
    const asset::JsonValue* capabilities = helloValue.find("result")->find("capabilities");
    REQUIRE(capabilities != nullptr);
    REQUIRE(capabilities->is_array());
    bool hasHid = false;
    for (const asset::JsonValue& capability : capabilities->array) {
        if (capability.is_string() && capability.string == "hid")
            hasHid = true;
    }
    REQUIRE(hasHid);

    const auto devices = protocol.handle(
        R"({"jsonrpc":"2.0","id":2,"method":"hid.devices","params":{}})");
    REQUIRE(devices.has_value());
    const asset::JsonValue devicesValue = parse_response(*devices);
    REQUIRE(devicesValue.find("result")->is_array());

    const auto errors = protocol.handle(
        R"({"jsonrpc":"2.0","id":3,"method":"hid.errors","params":{}})");
    REQUIRE(errors.has_value());
    const asset::JsonValue errorsValue = parse_response(*errors);
    REQUIRE(errorsValue.find("result")->find("errors")->is_array());

    const auto missing = protocol.handle(
        R"({"jsonrpc":"2.0","id":4,"method":"hid.reports.start","params":{"deviceId":"missing"}})");
    REQUIRE(missing.has_value());
    REQUIRE(parse_response(*missing).find("error") != nullptr);

    const auto unconfirmed = protocol.handle(
        R"({"jsonrpc":"2.0","id":5,"method":"hid.output.write","params":{"deviceId":"missing","bytes":[1]}})");
    REQUIRE(unconfirmed.has_value());
    const asset::JsonValue unconfirmedValue = parse_response(*unconfirmed);
    const asset::JsonValue* error = unconfirmedValue.find("error");
    REQUIRE(error != nullptr);
    REQUIRE(string_field(*error, "message").find("confirmed") != std::string::npos);
}
TEST_CASE("Editor protocol reports JSON-RPC errors") {
    editor::EditorSession session;
    editor::EditorProtocolOptions options;
    options.maxMessageBytes = 128;
    bool shutdown = false;
    options.requestShutdown = [&shutdown] { shutdown = true; };
    editor::EditorProtocol protocol(session, options);

    const auto malformed = protocol.handle("{");
    REQUIRE(malformed.has_value());
    REQUIRE(parse_response(*malformed).find("error")->find("code")->number == -32700);

    const auto unknown = protocol.handle(
        R"({"jsonrpc":"2.0","id":5,"method":"does.not.exist"})");
    REQUIRE(unknown.has_value());
    REQUIRE(parse_response(*unknown).find("error")->find("code")->number == -32601);

    const auto invalidParams = protocol.handle(
        R"({"jsonrpc":"2.0","id":6,"method":"asset.open","params":{"path":3}})");
    REQUIRE(invalidParams.has_value());
    REQUIRE(parse_response(*invalidParams).find("error")->find("code")->number == -32602);

    const auto tooLarge = protocol.handle(std::string(200, 'x'));
    REQUIRE(tooLarge.has_value());
    REQUIRE(parse_response(*tooLarge).find("error")->find("code")->number == -32013);

    const auto shutdownResponse = protocol.handle(
        R"({"jsonrpc":"2.0","id":7,"method":"app.shutdown"})");
    REQUIRE(shutdownResponse.has_value());
    REQUIRE(shutdown);

    const auto notification = protocol.handle(
        R"({"jsonrpc":"2.0","method":"app.shutdown"})");
    REQUIRE_FALSE(notification.has_value());
}

TEST_CASE("Editor protocol bakes and imports binary assets") {
    const std::filesystem::path root = make_temp_directory();
    {
        std::ofstream stream(root / "player.json", std::ios::binary);
        stream << kAsset;
    }

    editor::EditorSession session(editor::EditorSessionOptions{{root}});
    editor::EditorProtocol protocol(session);

    const auto hello = protocol.handle(
        R"({"jsonrpc":"2.0","id":1,"method":"session.hello","params":{}})");
    REQUIRE(hello.has_value());
    const asset::JsonValue helloValue = parse_response(*hello);
    const asset::JsonValue* capabilities =
        helloValue.find("result")->find("capabilities");
    REQUIRE(capabilities != nullptr);
    bool hasBake = false;
    for (const asset::JsonValue& capability : capabilities->array) {
        if (capability.is_string() && capability.string == "asset.bake")
            hasBake = true;
    }
    REQUIRE(hasBake);

    const std::string bakeRequest =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"asset.bake\",\"params\":{"
        "\"path\":\"player.insysb\",\"json\":\"" +
        escape_json(kAsset) + "\"}}";
    const auto baked = protocol.handle(bakeRequest);
    REQUIRE(baked.has_value());
    const asset::JsonValue bakedValue = parse_response(*baked);
    const asset::JsonValue* bakedResult = bakedValue.find("result");
    REQUIRE(bakedResult != nullptr);
    REQUIRE(string_field(*bakedResult, "path").find("player.insysb") !=
            std::string::npos);
    REQUIRE(bakedResult->find("changed")->boolean);
    REQUIRE(bakedResult->find("bytes")->number > 0.0);

    const auto imported = protocol.handle(
        R"({"jsonrpc":"2.0","id":3,"method":"asset.open","params":{"path":"player.insysb"}})");
    REQUIRE(imported.has_value());
    const asset::JsonValue importedValue = parse_response(*imported);
    const asset::JsonValue* importedResult = importedValue.find("result");
    REQUIRE(importedResult != nullptr);
    REQUIRE(string_field(*importedResult, "format") == "insysb");
    REQUIRE(importedResult->find("readOnly")->boolean);
    REQUIRE(importedResult->find("schemaVersion")->number == 1.0);
    REQUIRE(string_field(*importedResult, "json").find("\"priority\": 5") !=
            std::string::npos);

    const auto invalidBake = protocol.handle(
        R"({"jsonrpc":"2.0","id":4,"method":"asset.bake","params":{"path":"bad.json","json":"{}"}})");
    REQUIRE(invalidBake.has_value());
    const asset::JsonValue invalidValue = parse_response(*invalidBake);
    REQUIRE(invalidValue.find("error") != nullptr);
}

TEST_CASE("Editor protocol surfaces schema migration diagnostics") {
    editor::EditorSession session;
    editor::EditorProtocol protocol(session);
    const auto response = protocol.handle(
        R"({"jsonrpc":"2.0","id":1,"method":"asset.validate","params":{"json":"{\"version\":2,\"maps\":[]}"}})");
    REQUIRE(response.has_value());
    const asset::JsonValue value = parse_response(*response);
    const asset::JsonValue* diagnostics = value.find("result")->find("diagnostics");
    REQUIRE(diagnostics != nullptr);
    REQUIRE(diagnostics->array.size() == 1);
    REQUIRE(string_field(diagnostics->array[0], "code") == "asset.schema");
    REQUIRE(string_field(diagnostics->array[0], "message").find("migration") !=
            std::string::npos);
}
