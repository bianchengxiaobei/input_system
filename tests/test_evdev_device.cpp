#include <catch2/catch_test_macros.hpp>

#include "insys/backend/evdev_device.h"

using namespace insys;
using namespace insys::backend::evdev;

TEST_CASE("Evdev device state tracks keyboard frames") {
    EvdevDeviceState state(EvdevDeviceType::Keyboard);
    REQUIRE(state.process(kEvKey, 30, 1));
    const auto bit = static_cast<std::uint32_t>(Key::A);
    REQUIRE((state.keyboard_bits()[bit / 8] & (1u << (bit % 8))) != 0);
    REQUIRE_FALSE(state.process(kEvKey, 30, 1));
    REQUIRE(state.end_frame());
    REQUIRE_FALSE(state.end_frame());

    REQUIRE(state.process(kEvKey, 30, 0));
    REQUIRE((state.keyboard_bits()[bit / 8] & (1u << (bit % 8))) == 0);
    REQUIRE(state.end_frame());
    REQUIRE_FALSE(state.process(kEvKey, 9999, 1));
}

TEST_CASE("Evdev device state tracks mouse and gamepad frames") {
    EvdevDeviceState mouse(EvdevDeviceType::Mouse);
    REQUIRE(mouse.process(kEvRel, kRelX, 4));
    REQUIRE(mouse.process(kEvRel, kRelY, -2));
    REQUIRE(mouse.mouse().delta.x == 4.0f);
    REQUIRE(mouse.mouse().delta.y == -2.0f);
    REQUIRE(mouse.end_frame());
    REQUIRE_FALSE(mouse.end_frame());

    EvdevDeviceState gamepad(EvdevDeviceType::Gamepad);
    REQUIRE(gamepad.process(kEvAbs, kAbsX, 32767));
    REQUIRE(gamepad.process(kEvKey, kBtnSouth, 1));
    REQUIRE(gamepad.gamepad().leftStick.x > 0.99f);
    REQUIRE((gamepad.gamepad().buttons & (1u << 6)) != 0);
    REQUIRE(gamepad.end_frame());
    REQUIRE_FALSE(gamepad.end_frame());

    gamepad.reset();
    REQUIRE(gamepad.gamepad().leftStick.x == 0.0f);
    REQUIRE(gamepad.gamepad().buttons == 0);
}
