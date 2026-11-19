#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "insys/backend/evdev_mapping.h"

using namespace insys;
using namespace insys::backend::evdev;

TEST_CASE("Evdev key codes map to internal keys") {
    Key key = Key::A;
    REQUIRE(map_key(30, key));
    REQUIRE(key == Key::A);
    REQUIRE(map_key(57, key));
    REQUIRE(key == Key::Space);
    REQUIRE(map_key(42, key));
    REQUIRE(key == Key::LeftShift);
    REQUIRE(map_key(103, key));
    REQUIRE(key == Key::ArrowUp);
    REQUIRE(map_key(88, key));
    REQUIRE(key == Key::F12);
    REQUIRE_FALSE(map_key(9999, key));
}

TEST_CASE("Evdev mouse mapper handles deltas, wheel and buttons") {
    MouseMapper mapper;
    REQUIRE(mapper.process(kEvRel, kRelX, 5));
    REQUIRE(mapper.process(kEvRel, kRelY, -3));
    REQUIRE(mapper.process(kEvRel, kRelWheel, 2));
    REQUIRE(mapper.process(kEvRel, kRelHWheel, -1));
    REQUIRE(mapper.state.delta.x == 5.0f);
    REQUIRE(mapper.state.delta.y == -3.0f);
    REQUIRE(mapper.state.scroll.x == -1.0f);
    REQUIRE(mapper.state.scroll.y == 2.0f);

    REQUIRE(mapper.process(kEvKey, kBtnLeft, 1));
    REQUIRE((mapper.state.buttons[0] & (1u << 0)) != 0);
    REQUIRE(mapper.process(kEvKey, kBtnLeft, 0));
    REQUIRE((mapper.state.buttons[0] & (1u << 0)) == 0);
    REQUIRE(mapper.process(kEvKey, kBtnSide, 1));
    REQUIRE((mapper.state.buttons[0] & (1u << 3)) != 0);
    REQUIRE(mapper.process(kEvKey, kBtnExtra, 1));
    REQUIRE((mapper.state.buttons[0] & (1u << 4)) != 0);
    REQUIRE_FALSE(mapper.process(kEvKey, 0x2ff, 1));
}

TEST_CASE("Evdev gamepad mapper normalizes axes, triggers and buttons") {
    GamepadMapper mapper;
    mapper.axisMin = -32768.0f;
    mapper.axisMax = 32767.0f;
    mapper.triggerMin = 0.0f;
    mapper.triggerMax = 255.0f;

    mapper.process(kEvAbs, kAbsX, 32767);
    mapper.process(kEvAbs, kAbsY, -32768);
    mapper.process(kEvAbs, kAbsRX, 0);
    mapper.process(kEvAbs, kAbsRY, 16384);
    mapper.process(kEvAbs, kAbsZ, 255);
    mapper.process(kEvAbs, kAbsRZ, 128);
    REQUIRE(mapper.state.leftStick.x == Catch::Approx(1.0f));
    REQUIRE(mapper.state.leftStick.y == Catch::Approx(-1.0f));
    REQUIRE(mapper.state.rightStick.x == Catch::Approx(0.0f).margin(0.001f));
    REQUIRE(mapper.state.rightStick.y > 0.49f);
    REQUIRE(mapper.state.leftTrigger == Catch::Approx(1.0f));
    REQUIRE(mapper.state.rightTrigger == Catch::Approx(128.0f / 255.0f));

    mapper.process(kEvKey, kBtnSouth, 1);
    mapper.process(kEvKey, kBtnStart, 1);
    mapper.process(kEvKey, kBtnDpadUp, 1);
    REQUIRE((mapper.state.buttons & (1u << 6)) != 0);
    REQUIRE((mapper.state.buttons & (1u << 12)) != 0);
    REQUIRE((mapper.state.buttons & (1u << 0)) != 0);

    mapper.process(kEvKey, kBtnSouth, 0);
    REQUIRE((mapper.state.buttons & (1u << 6)) == 0);

    mapper.process(kEvAbs, kAbsHat0X, -1);
    REQUIRE((mapper.state.buttons & (1u << 2)) != 0);
    mapper.process(kEvAbs, kAbsHat0X, 0);
    REQUIRE((mapper.state.buttons & (1u << 2)) == 0);
}
