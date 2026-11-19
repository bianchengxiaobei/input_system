#include <catch2/catch_test_macros.hpp>

#include "insys/control_path.h"
#include "insys/device.h"
#include "insys/device_builder.h"
#include "insys/layout_registry.h"

using namespace insys;

namespace {

DeviceDescription description() {
    DeviceDescription value;
    value.interfaceName = "Test";
    value.product = "Test Device";
    return value;
}

} // namespace

TEST_CASE("ControlPath parses names, layouts and usages") {
    const auto components = parse_path("Gamepad/LeftStick/X");
    REQUIRE(components.size() == 3);
    REQUIRE(components[0].name.str() == "Gamepad");
    REQUIRE(components[1].name.str() == "LeftStick");
    REQUIRE(components[2].name.str() == "X");

    const auto styled = parse_path("*/{PrimaryAction}");
    REQUIRE(styled.size() == 2);
    REQUIRE(styled[0].wildcard);
    REQUIRE(styled[1].usages.size() == 1);
    REQUIRE(styled[1].usages[0].str() == "PrimaryAction");

    const auto layoutOnly = parse_path("<Gamepad>/<Stick>");
    REQUIRE(layoutOnly.size() == 2);
    REQUIRE(layoutOnly[0].layout.str() == "Gamepad");
    REQUIRE(layoutOnly[1].layout.str() == "Stick");

    REQUIRE(parse_path("").empty());
    REQUIRE(parse_path("Gamepad//X").empty());
    REQUIRE(parse_path("Gamepad/{Unclosed").empty());
}

TEST_CASE("ControlPath finds keyboard and gamepad controls") {
    LayoutRegistry registry;
    register_default_layouts(registry);
    DeviceBuilder builder(registry);

    auto keyboardDevice = builder.build("Keyboard", "", 1, description());
    auto gamepadDevice = builder.build("Gamepad", "", 2, description());
    REQUIRE(keyboardDevice != nullptr);
    REQUIRE(gamepadDevice != nullptr);

    auto* keyboard = static_cast<Keyboard*>(keyboardDevice.get());
    auto* gamepad = static_cast<Gamepad*>(gamepadDevice.get());

    Control* space = find_control(*keyboard, "Keyboard/Space");
    REQUIRE(space != nullptr);
    REQUIRE(space == keyboard->key_control(Key::Space));
    REQUIRE(path_matches("Keyboard/Space", *space));
    REQUIRE(path_matches("keyboard/space", *space));

    Control* leftStick = find_control(*gamepad, "Gamepad/LeftStick");
    REQUIRE(leftStick != nullptr);
    REQUIRE(leftStick == gamepad->left_stick());

    Control* primary = find_control(*gamepad, "*/{PrimaryAction}");
    REQUIRE(primary != nullptr);
    REQUIRE(primary == gamepad->button("buttonSouth"));

    Control* byAlias = find_control(*gamepad, "Gamepad/a");
    REQUIRE(byAlias == gamepad->button("buttonSouth"));

    REQUIRE(find_control(*gamepad, "Gamepad/DoesNotExist") == nullptr);
    REQUIRE(find_control(*gamepad, "*") == gamepad);
}

TEST_CASE("ControlPath matches wildcard and aliases") {
    LayoutRegistry registry;
    register_default_layouts(registry);
    DeviceBuilder builder(registry);

    auto device = builder.build("Gamepad", "", 1, description());
    auto* gamepad = static_cast<Gamepad*>(device.get());
    Control* south = gamepad->button("buttonSouth");

    REQUIRE(path_matches("*", *device));
    REQUIRE(path_matches("*/{PrimaryAction}", *south));
    REQUIRE(path_matches("Gamepad/{PrimaryAction}", *south));
    REQUIRE_FALSE(path_matches("Keyboard/{PrimaryAction}", *south));
    REQUIRE_FALSE(path_matches("Gamepad/buttonNorth", *south));
}

TEST_CASE("DeviceMatcher scores exact, prefix and regex rules") {
    DeviceDescription value;
    value.interfaceName = "XInputController";
    value.product = "Microsoft XBOX Gamepad";

    DeviceMatcher exact;
    exact.add("interface", "XInputController");
    REQUIRE(exact.match_score(value) == 1.0f);

    DeviceMatcher prefix;
    prefix.add("interface", "XInput*");
    REQUIRE(prefix.match_score(value) == 1.0f);

    DeviceMatcher regex;
    regex.add("product", ".*gamepad.*", true);
    REQUIRE(regex.match_score(value) == 1.0f);

    DeviceMatcher partial;
    partial.add("interface", "XInputController");
    partial.add("manufacturer", "Microsoft");
    REQUIRE(partial.match_score(value) > 0.0f);
    REQUIRE(partial.match_score(value) < 1.0f);

    DeviceMatcher failing;
    failing.add("interface", "DualSense");
    REQUIRE(failing.match_score(value) == 0.0f);

    DeviceMatcher missingField;
    missingField.add("manufacturer", "Microsoft");
    REQUIRE(missingField.match_score(value) == 0.0f);

    DeviceMatcher empty;
    REQUIRE(empty.match_score(value) == 0.0f);
}
