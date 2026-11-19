#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "insys/asset/json.h"
#include "insys/backend/runtime.h"
#include "insys/device.h"
#include "insys/device_states.h"
#include "insys/editor/editor_preview.h"
#include "insys/low/state_format.h"
#include "insys/manager.h"

using namespace insys;
using JsonValue = asset::JsonValue;

namespace {

const char* kJumpAsset =
    "{\n"
    "  \"version\": 1,\n"
    "  \"name\": \"PreviewTest\",\n"
    "  \"maps\": [{\n"
    "    \"name\": \"Player\",\n"
    "    \"actions\": [{\"name\": \"Jump\", \"type\": \"Button\", \"control\": \"Button\"}],\n"
    "    \"bindings\": [{\"action\": \"Jump\", \"path\": \"<Keyboard>/space\"}]\n"
    "  }]\n"
    "}\n";

const char* kFireAsset =
    "{\n"
    "  \"version\": 1,\n"
    "  \"name\": \"PreviewTest\",\n"
    "  \"maps\": [{\n"
    "    \"name\": \"Player\",\n"
    "    \"actions\": [{\"name\": \"Fire\", \"type\": \"Button\", \"control\": \"Button\"}],\n"
    "    \"bindings\": [{\"action\": \"Fire\", \"path\": \"<Keyboard>/space\"}]\n"
    "  }]\n"
    "}\n";

asset::JsonValue parse_json_value(std::string_view text) {
    asset::JsonValue value;
    asset::JsonParseError error;
    REQUIRE(asset::parse_json(text, value, error));
    return value;
}

const asset::JsonValue& field(const asset::JsonValue& value, std::string_view name) {
    const asset::JsonValue* result = value.find(name);
    REQUIRE(result != nullptr);
    return *result;
}

std::string string_field(const asset::JsonValue& value, std::string_view name) {
    const asset::JsonValue& result = field(value, name);
    REQUIRE(result.is_string());
    return result.string;
}

void set_gamepad_stick(Manager& manager, Device* gamepad, Vector2 value, double time) {
    GamepadState state;
    state.leftStick = value;
    const auto event = low::make_state_event(
        kFormatGamepad, &state, static_cast<std::uint32_t>(sizeof(state)),
        gamepad->deviceId, time);
    manager.queue_event(reinterpret_cast<const low::Event*>(event.data()));
    manager.update(UpdateType::Dynamic);
}

} // namespace

TEST_CASE("Preview runtime loads assets and updates actions from browser keys") {
    editor::PreviewRuntimeOptions options;
    options.enableHid = false;
    editor::PreviewRuntime preview(std::move(options));

    preview.start();
    const std::uint64_t generation = preview.update_asset(kJumpAsset);
    REQUIRE(generation == 2);
    REQUIRE(preview.running());

    JsonValue snapshot = preview.snapshot();
    REQUIRE(field(snapshot, "generation").number == 2.0);
    REQUIRE(field(snapshot, "devices").array.size() == 2);
    const JsonValue& actions = field(snapshot, "actions");
    REQUIRE(actions.array.size() == 1);
    REQUIRE(string_field(actions.array[0], "phase") == "Waiting");

    REQUIRE(preview.input_key("Space", true, false));
    snapshot = preview.snapshot();
    const JsonValue& performed = field(snapshot, "actions").array[0];
    REQUIRE(string_field(performed, "phase") == "Performed");
    REQUIRE(string_field(performed, "name") == "Jump");
    REQUIRE(string_field(performed, "drivingControl") == "<Keyboard>/space");

    REQUIRE(preview.input_key("Space", false, false));
    snapshot = preview.snapshot();
    const std::string releasedPhase =
        string_field(field(snapshot, "actions").array[0], "phase");
    REQUIRE((releasedPhase == "Canceled" || releasedPhase == "Waiting"));
}

TEST_CASE("Preview runtime hot reload keeps devices and replaces actions") {
    editor::PreviewRuntimeOptions options;
    options.enableHid = false;
    editor::PreviewRuntime preview(std::move(options));
    preview.start();
    preview.update_asset(kJumpAsset);

    const JsonValue first = preview.snapshot();
    const int keyboardId = static_cast<int>(field(first, "devices").array[0].find("deviceId")->number);
    REQUIRE(string_field(field(first, "actions").array[0], "name") == "Jump");

    const std::uint64_t generation = preview.update_asset(kFireAsset, 2);
    REQUIRE(generation == 3);
    const JsonValue second = preview.snapshot();
    const JsonValue& devices = field(second, "devices");
    REQUIRE(static_cast<int>(devices.array[0].find("deviceId")->number) == keyboardId);
    REQUIRE(string_field(field(second, "actions").array[0], "name") == "Fire");
    REQUIRE(string_field(field(second, "actions").array[0], "phase") == "Waiting");
}

TEST_CASE("Preview runtime captures keyboard, mouse and gamepad controls") {
    editor::PreviewRuntimeOptions options;
    options.enableHid = false;
    editor::PreviewRuntime preview(std::move(options));
    preview.start();
    preview.update_asset(kJumpAsset);

    preview.start_capture("Button", "Keyboard");
    REQUIRE(preview.input_key("Space", true, false));
    std::optional<editor::PreviewCapturedControl> captured = preview.take_capture();
    REQUIRE(captured.has_value());
    REQUIRE(captured->path == "<Keyboard>/space");
    REQUIRE(captured->isButton);
    preview.stop_capture();

    preview.start_capture("Button", "Mouse");
    editor::PreviewMouseInput mouse;
    mouse.hasButton = true;
    mouse.button = 0;
    mouse.pressed = true;
    REQUIRE(preview.input_mouse(mouse));
    captured = preview.take_capture();
    REQUIRE(captured.has_value());
    REQUIRE(captured->path == "<Mouse>/leftButton");
    preview.stop_capture();

    DeviceDescription description;
    description.interfaceName = "Gamepad";
    description.deviceClass = "Gamepad";
    description.product = "Test Gamepad";
    Device* gamepad = preview.manager().create_device("Gamepad", description);
    REQUIRE(gamepad != nullptr);
    set_gamepad_stick(preview.manager(), gamepad, {}, preview.runtime().now());

    preview.start_capture("Stick", "Gamepad");
    set_gamepad_stick(preview.manager(), gamepad, {0.8f, -0.4f}, preview.runtime().now());
    preview.tick();
    captured = preview.take_capture();
    REQUIRE(captured.has_value());
    REQUIRE(captured->path == "<Gamepad>/leftStick");
    REQUIRE(captured->isButton == false);
    REQUIRE(captured->value > 0.5f);
}

TEST_CASE("Preview capture filters expected control types") {
    editor::PreviewRuntimeOptions options;
    options.enableHid = false;
    editor::PreviewRuntime preview(std::move(options));
    preview.start();
    preview.update_asset(kJumpAsset);

    preview.start_capture("Axis", "Keyboard");
    REQUIRE(preview.input_key("Space", true, false));
    REQUIRE_FALSE(preview.take_capture().has_value());
}
