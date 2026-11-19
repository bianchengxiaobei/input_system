#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "insys/backend/xinput_mapping.h"

using namespace insys;
using namespace insys::backend;

TEST_CASE("XInput mapping converts buttons, sticks and triggers") {
    XInputPadState raw;
    raw.buttons = kXInputDpadUp | kXInputDpadDown | kXInputDpadLeft | kXInputDpadRight |
                  kXInputStart | kXInputBack | kXInputLeftThumb | kXInputRightThumb |
                  kXInputLeftShoulder | kXInputRightShoulder | kXInputButtonA |
                  kXInputButtonB | kXInputButtonX | kXInputButtonY;
    raw.leftTrigger = 255;
    raw.rightTrigger = 128;
    raw.leftThumbX = 32767;
    raw.leftThumbY = -32768;
    raw.rightThumbX = 0;
    raw.rightThumbY = 16384;

    const GamepadState state = map_xinput_state(raw);
    REQUIRE(state.buttons == 0x3fffu);

    REQUIRE(state.leftStick.x == Catch::Approx(1.0f));
    REQUIRE(state.leftStick.y == Catch::Approx(-1.0f));
    REQUIRE(state.rightStick.x == Catch::Approx(0.0f));
    REQUIRE(state.rightStick.y == Catch::Approx(16384.0f / 32767.0f));
    REQUIRE(state.leftTrigger == Catch::Approx(1.0f));
    REQUIRE(state.rightTrigger == Catch::Approx(128.0f / 255.0f));
}

TEST_CASE("XInput button bits map to the gamepad layout semantics") {
    XInputPadState raw;
    raw.buttons = kXInputButtonA;
    REQUIRE((map_xinput_state(raw).buttons & (1u << 6)) != 0);

    raw.buttons = kXInputButtonB;
    REQUIRE((map_xinput_state(raw).buttons & (1u << 5)) != 0);

    raw.buttons = kXInputButtonX;
    REQUIRE((map_xinput_state(raw).buttons & (1u << 7)) != 0);

    raw.buttons = kXInputButtonY;
    REQUIRE((map_xinput_state(raw).buttons & (1u << 4)) != 0);
}

TEST_CASE("XInput gamepad name detection") {
    REQUIRE(is_xinput_gamepad_name("Xbox 360 Controller"));
    REQUIRE(is_xinput_gamepad_name("XInput Controller"));
    REQUIRE(is_xinput_gamepad_name("Microsoft X-Box One pad"));
    REQUIRE_FALSE(is_xinput_gamepad_name("Wireless Controller"));
    REQUIRE_FALSE(is_xinput_gamepad_name(""));
}
