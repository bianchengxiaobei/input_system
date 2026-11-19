#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "action_test_helpers.h"
#include "insys/actions/action_state.h"
#include "insys/actions/control_scheme.h"
#include "insys/asset/asset_loader.h"

using namespace insys;

namespace {

const char* kAssetJson = R"JSON({
  "version": 1,
  "name": "default",
  "maps": [
    {
      "name": "Player",
      "actions": [
        { "name": "Move", "type": "value", "control": "Vector2" },
        { "name": "Jump", "type": "button" }
      ],
      "bindings": [
        { "action": "Move", "composite": "2DVector",
          "parts": {
            "up": "<Keyboard>/w",
            "down": "<Keyboard>/s",
            "left": "<Keyboard>/a",
            "right": "<Keyboard>/d"
          } },
        { "action": "Jump", "path": "<Keyboard>/space", "interactions": "Tap" }
      ]
    },
    {
      "name": "UI",
      "actions": [
        { "name": "Submit", "type": "button" }
      ],
      "bindings": [
        { "action": "Submit", "path": "<Keyboard>/enter" }
      ]
    }
  ],
  "schemes": [
    { "name": "Keyboard", "bindingGroup": "Keyboard", "devices": ["Keyboard"] },
    { "name": "Gamepad", "bindingGroup": "Gamepad", "devices": ["Gamepad"] }
  ]
})JSON";

struct AssetFixture {
    insys_test::TestRuntime runtime;
    Manager manager{runtime};
    Keyboard* keyboard = insys_test::add_keyboard(manager);
};

} // namespace

TEST_CASE("Asset loader parses actions, composites, and schemes") {
    ActionAsset asset = asset::AssetLoader::load_from_json(kAssetJson);
    REQUIRE(asset.name == "default");
    REQUIRE(asset.maps.size() == 2);
    REQUIRE(asset.maps[0].name == "Player");
    REQUIRE(asset.maps[0].actions.size() == 2);
    REQUIRE(asset.maps[0].actions[0].type == ActionType::Value);
    REQUIRE(asset.maps[0].actions[0].expectedControlType == "Vector2");
    REQUIRE(asset.maps[0].bindings.size() == 6);
    REQUIRE(asset.maps[0].bindings[0].isComposite);
    REQUIRE(asset.maps[0].bindings[1].isPartOfComposite);
    REQUIRE(asset.maps[0].bindings[1].name == "up");
    REQUIRE(asset.maps[0].bindings[0].interactions.empty());
    REQUIRE(asset.maps[0].bindings[5].interactions == "Tap");

    REQUIRE(asset.controlSchemes.size() == 2);
    REQUIRE(asset.controlSchemes[0].name == "Keyboard");
    REQUIRE(asset.controlSchemes[0].bindingGroup == "Keyboard");
    REQUIRE(asset.controlSchemes[0].devices == std::vector<std::string>{"Keyboard"});
    REQUIRE(asset.controlSchemes[0].isOptional == false);
}

TEST_CASE("Asset loader round-trips action priority") {
    const std::string json = R"({
      "version": 1,
      "maps": [{
        "name": "Player",
        "actions": [
          {"name": "Jump", "type": "button", "priority": -7},
          {"name": "Fire", "type": "button", "priority": 42}
        ]
      }]
    })";

    const ActionAsset asset = asset::AssetLoader::load_from_json(json);
    REQUIRE(asset.maps[0].actions[0].priority == -7);
    REQUIRE(asset.maps[0].actions[1].priority == 42);

    const std::string serialized = asset::AssetLoader::to_json(asset);
    REQUIRE(serialized.find("\"priority\": -7") != std::string::npos);
    REQUIRE(serialized.find("\"priority\": 42") != std::string::npos);

    const ActionAsset roundTripped = asset::AssetLoader::load_from_json(serialized);
    REQUIRE(roundTripped.maps[0].actions[0].priority == -7);
    REQUIRE(roundTripped.maps[0].actions[1].priority == 42);

    const std::string fractional = R"({
      "version": 1,
      "maps": [{"name": "M", "actions": [{"name": "A", "priority": 1.5}]}]
    })";
    REQUIRE_THROWS_AS(asset::AssetLoader::load_from_json(fractional), asset::AssetError);
}

TEST_CASE("Asset loader output can be loaded again and driven") {
    AssetFixture fixture;
    ActionAsset asset = asset::AssetLoader::load_from_json(kAssetJson);
    const std::string serialized = asset::AssetLoader::to_json(asset);
    ActionAsset roundTripped = asset::AssetLoader::load_from_json(serialized);
    REQUIRE(roundTripped.maps.size() == asset.maps.size());
    REQUIRE(roundTripped.maps[0].bindings.size() == asset.maps[0].bindings.size());
    REQUIRE(roundTripped.maps[0].bindings[0].id == asset.maps[0].bindings[0].id);

    roundTripped.enable(fixture.manager);
    ActionDef* move = roundTripped.find_action("Move");
    REQUIRE(move != nullptr);
    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::W}, 1.0);
    REQUIRE(move->state->phase(*move) == ActionPhase::Started);
    const Vector2 value = move->state->read_value<Vector2>(*move);
    REQUIRE(value.x == 0.0f);
    REQUIRE(value.y == Catch::Approx(1.0f));
}

TEST_CASE("Asset loader reports validation errors with useful context") {
    const std::string duplicateAction = R"({
      "version": 1,
      "maps": [{ "name": "M", "actions": [
        {"name": "A"}, {"name": "a"}
      ] }]
    })";
    try {
        (void)asset::AssetLoader::load_from_json(duplicateAction);
        FAIL("expected duplicate action error");
    } catch (const asset::AssetError& error) {
        REQUIRE(std::string(error.what()).find("duplicate action") != std::string::npos);
        REQUIRE(error.line() > 0);
    }

    const std::string unknownComposite = R"({
      "version": 1,
      "maps": [{ "name": "M", "actions": [{"name": "A"}],
        "bindings": [{"action": "A", "composite": "Nope", "parts": {"x": "<Keyboard>/a"}}]
      }]
    })";
    REQUIRE_THROWS_AS(asset::AssetLoader::load_from_json(unknownComposite), asset::AssetError);

    const std::string malformedPath = R"({
      "version": 1,
      "maps": [{ "name": "M", "actions": [{"name": "A"}],
        "bindings": [{"action": "A", "path": "<Keyboard"}]
      }]
    })";
    REQUIRE_THROWS_AS(asset::AssetLoader::load_from_json(malformedPath), asset::AssetError);

    const std::string typo = R"({
      "version": 1,
      "maps": [{ "name": "M", "actions": [{"name": "A"}], "binding": [] }]
    })";
    try {
        (void)asset::AssetLoader::load_from_json(typo);
        FAIL("expected unknown field error");
    } catch (const asset::AssetError& error) {
        REQUIRE(std::string(error.what()).find("unknown field") != std::string::npos);
    }
}

TEST_CASE("Control schemes match connected device layouts") {
    AssetFixture fixture;
    ControlScheme keyboard;
    keyboard.name = "Keyboard";
    keyboard.devices = {"Keyboard"};
    ControlScheme keyboardAndMouse;
    keyboardAndMouse.devices = {"Keyboard", "Mouse"};
    ControlScheme gamepad;
    gamepad.devices = {"Gamepad"};

    const auto& devices = fixture.manager.devices();
    REQUIRE(scheme_matches(keyboard, devices));
    REQUIRE_FALSE(scheme_matches(keyboardAndMouse, devices));
    REQUIRE_FALSE(scheme_matches(gamepad, devices));

    insys_test::add_gamepad(fixture.manager);
    REQUIRE(scheme_matches(gamepad, fixture.manager.devices()));
}

namespace {

void set_u32(std::string& bytes, std::size_t offset, std::uint32_t value) {
    REQUIRE(offset + 4 <= bytes.size());
    bytes[offset] = static_cast<char>(value & 0xffu);
    bytes[offset + 1] = static_cast<char>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<char>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<char>((value >> 24) & 0xffu);
}

void require_binding_equal(const Binding& actual, const Binding& expected) {
    REQUIRE(actual.name == expected.name);
    REQUIRE(actual.id == expected.id);
    REQUIRE(actual.path == expected.path);
    REQUIRE(actual.overridePath == expected.overridePath);
    REQUIRE(actual.action == expected.action);
    REQUIRE(actual.interactions == expected.interactions);
    REQUIRE(actual.processors == expected.processors);
    REQUIRE(actual.groups == expected.groups);
    REQUIRE(actual.overrideInteractions == expected.overrideInteractions);
    REQUIRE(actual.overrideProcessors == expected.overrideProcessors);
    REQUIRE(actual.isComposite == expected.isComposite);
    REQUIRE(actual.isPartOfComposite == expected.isPartOfComposite);
    REQUIRE(actual.partIndex == expected.partIndex);
}

void require_action_equal(const ActionDef& actual, const ActionDef& expected) {
    REQUIRE(actual.name == expected.name);
    REQUIRE(actual.id == expected.id);
    REQUIRE(actual.type == expected.type);
    REQUIRE(actual.expectedControlType == expected.expectedControlType);
    REQUIRE(actual.processors == expected.processors);
    REQUIRE(actual.interactions == expected.interactions);
    REQUIRE(actual.initialStateCheck == expected.initialStateCheck);
    REQUIRE(actual.priority == expected.priority);
}

void require_asset_equal(const ActionAsset& actual, const ActionAsset& expected) {
    REQUIRE(actual.name == expected.name);
    REQUIRE(actual.maps.size() == expected.maps.size());
    for (std::size_t m = 0; m < expected.maps.size(); ++m) {
        const ActionMap& actualMap = actual.maps[m];
        const ActionMap& expectedMap = expected.maps[m];
        REQUIRE(actualMap.name == expectedMap.name);
        REQUIRE(actualMap.id == expectedMap.id);
        REQUIRE(actualMap.actions.size() == expectedMap.actions.size());
        for (std::size_t a = 0; a < expectedMap.actions.size(); ++a)
            require_action_equal(actualMap.actions[a], expectedMap.actions[a]);
        REQUIRE(actualMap.bindings.size() == expectedMap.bindings.size());
        for (std::size_t b = 0; b < expectedMap.bindings.size(); ++b)
            require_binding_equal(actualMap.bindings[b], expectedMap.bindings[b]);
    }

    REQUIRE(actual.controlSchemes.size() == expected.controlSchemes.size());
    for (std::size_t s = 0; s < expected.controlSchemes.size(); ++s) {
        const ControlScheme& actualScheme = actual.controlSchemes[s];
        const ControlScheme& expectedScheme = expected.controlSchemes[s];
        REQUIRE(actualScheme.name == expectedScheme.name);
        REQUIRE(actualScheme.id == expectedScheme.id);
        REQUIRE(actualScheme.bindingGroup == expectedScheme.bindingGroup);
        REQUIRE(actualScheme.devices == expectedScheme.devices);
        REQUIRE(actualScheme.isOptional == expectedScheme.isOptional);
    }
}

} // namespace

TEST_CASE("Binary asset round-trips every serialized field") {
    ActionAsset source = asset::AssetLoader::load_from_json(kAssetJson);
    source.maps[0].actions[0].priority = -7;
    source.maps[0].bindings[1].overridePath = "<Keyboard>/q";
    source.maps[0].bindings[1].overrideInteractions = "Press";
    source.maps[0].bindings[1].overrideProcessors = "Scale(factor=2)";
    source.maps[0].bindings[1].partIndex = 3;

    const std::string binary = asset::AssetLoader::to_binary(source);
    REQUIRE(binary.size() > 44);
    REQUIRE(binary.substr(0, 8) == std::string("INSYAST\0", 8));

    const ActionAsset loaded = asset::AssetLoader::load_from_binary(binary);
    require_asset_equal(loaded, source);
    REQUIRE(loaded.maps[0].bindings[1].partIndex == 3);
    REQUIRE(asset::AssetLoader::to_json(loaded) == asset::AssetLoader::to_json(source));
}

TEST_CASE("Binary asset drives actions like JSON") {
    AssetFixture fixture;
    const ActionAsset jsonAsset = asset::AssetLoader::load_from_json(kAssetJson);
    ActionAsset binaryAsset =
        asset::AssetLoader::load_from_binary(asset::AssetLoader::to_binary(jsonAsset));
    binaryAsset.enable(fixture.manager);

    ActionDef* move = binaryAsset.find_action("Move");
    REQUIRE(move != nullptr);
    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::W}, 1.0);
    REQUIRE(move->state->phase(*move) == ActionPhase::Started);
    const Vector2 value = move->state->read_value<Vector2>(*move);
    REQUIRE(value.x == 0.0f);
    REQUIRE(value.y == Catch::Approx(1.0f));
}

TEST_CASE("Asset loader auto-detects JSON and binary files") {
    const auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path base =
        std::filesystem::temp_directory_path() / ("insys_asset_" + stamp);
    const std::filesystem::path jsonPath = base.string() + ".json";
    const std::filesystem::path binaryPath = base.string() + ".INSYSB";
    const std::filesystem::path unknownPath = base.string() + ".txt";

    const std::string binary =
        asset::AssetLoader::to_binary(asset::AssetLoader::load_from_json(kAssetJson));
    {
        std::ofstream jsonFile(jsonPath, std::ios::binary);
        jsonFile.write(kAssetJson, static_cast<std::streamsize>(std::char_traits<char>::length(kAssetJson)));
        REQUIRE(jsonFile.good());
        std::ofstream binaryFile(binaryPath, std::ios::binary);
        binaryFile.write(binary.data(), static_cast<std::streamsize>(binary.size()));
        REQUIRE(binaryFile.good());
        std::ofstream unknownFile(unknownPath, std::ios::binary);
        unknownFile.write(binary.data(), static_cast<std::streamsize>(binary.size()));
        REQUIRE(unknownFile.good());
    }

    REQUIRE(asset::AssetLoader::load_from_file(jsonPath).maps.size() == 2);
    REQUIRE(asset::AssetLoader::load_from_file(binaryPath).maps.size() == 2);
    REQUIRE_THROWS_AS(asset::AssetLoader::load_from_file(unknownPath), asset::AssetError);

    std::error_code ignored;
    std::filesystem::remove(jsonPath, ignored);
    std::filesystem::remove(binaryPath, ignored);
    std::filesystem::remove(unknownPath, ignored);
}

TEST_CASE("Binary asset rejects corrupt headers and references") {
    const std::string binary =
        asset::AssetLoader::to_binary(asset::AssetLoader::load_from_json(kAssetJson));

    std::string badMagic = binary;
    badMagic[0] = 'X';
    REQUIRE_THROWS_AS(asset::AssetLoader::load_from_binary(badMagic), asset::AssetError);

    std::string badVersion = binary;
    set_u32(badVersion, 8, 2);
    REQUIRE_THROWS_AS(asset::AssetLoader::load_from_binary(badVersion), asset::AssetError);

    std::string badPool = binary;
    set_u32(badPool, 12, 0xffffffffu);
    REQUIRE_THROWS_AS(asset::AssetLoader::load_from_binary(badPool), asset::AssetError);

    std::string badReference = binary;
    set_u32(badReference, 44, 0xffffu);
    REQUIRE_THROWS_AS(asset::AssetLoader::load_from_binary(badReference), asset::AssetError);

    const std::string truncated = binary.substr(0, binary.size() - 1);
    REQUIRE_THROWS_AS(asset::AssetLoader::load_from_binary(truncated), asset::AssetError);
}
