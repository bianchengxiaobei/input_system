#include <catch2/catch_test_macros.hpp>

#include "insys/backend/gamecontroller_mapping.h"

using namespace insys;
using namespace insys::backend;

TEST_CASE("GameController state maps buttons and dpad") {
    GameControllerState source;
    source.buttonA = true;
    source.buttonB = true;
    source.buttonX = true;
    source.buttonY = true;
    source.leftShoulder = true;
    source.rightShoulder = true;
    source.dpadUp = true;
    source.dpadRight = true;
    source.buttonMenu = true;
    source.buttonOptions = true;
    source.leftThumbstickButton = true;
    source.rightThumbstickButton = true;

    const GamepadState state = map_gamecontroller_state(source);
    REQUIRE((state.buttons & (1u << 6)) != 0);
    REQUIRE((state.buttons & (1u << 5)) != 0);
    REQUIRE((state.buttons & (1u << 7)) != 0);
    REQUIRE((state.buttons & (1u << 4)) != 0);
    REQUIRE((state.buttons & (1u << 10)) != 0);
    REQUIRE((state.buttons & (1u << 11)) != 0);
    REQUIRE((state.buttons & (1u << 0)) != 0);
    REQUIRE((state.buttons & (1u << 3)) != 0);
    REQUIRE((state.buttons & (1u << 12)) != 0);
    REQUIRE((state.buttons & (1u << 13)) != 0);
    REQUIRE((state.buttons & (1u << 8)) != 0);
    REQUIRE((state.buttons & (1u << 9)) != 0);
    REQUIRE((state.buttons & (1u << 1)) == 0);
    REQUIRE((state.buttons & (1u << 2)) == 0);
}

TEST_CASE("GameController axes and triggers are clamped") {
    GameControllerState source;
    source.leftThumbstickX = 1.5f;
    source.leftThumbstickY = -1.5f;
    source.rightThumbstickX = 0.25f;
    source.rightThumbstickY = -0.5f;
    source.leftTrigger = 2.0f;
    source.rightTrigger = -1.0f;

    const GamepadState state = map_gamecontroller_state(source);
    REQUIRE(state.leftStick.x == 1.0f);
    REQUIRE(state.leftStick.y == -1.0f);
    REQUIRE(state.rightStick.x == 0.25f);
    REQUIRE(state.rightStick.y == -0.5f);
    REQUIRE(state.leftTrigger == 1.0f);
    REQUIRE(state.rightTrigger == 0.0f);
}
