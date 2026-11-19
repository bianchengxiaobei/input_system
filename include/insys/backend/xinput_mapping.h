#pragma once

#include <cstdint>
#include <string_view>

#include "insys/device_states.h"

namespace insys::backend {

struct XInputPadState {
    uint16_t buttons = 0;
    uint8_t leftTrigger = 0;
    uint8_t rightTrigger = 0;
    int16_t leftThumbX = 0;
    int16_t leftThumbY = 0;
    int16_t rightThumbX = 0;
    int16_t rightThumbY = 0;
};

inline constexpr uint16_t kXInputDpadUp = 0x0001;
inline constexpr uint16_t kXInputDpadDown = 0x0002;
inline constexpr uint16_t kXInputDpadLeft = 0x0004;
inline constexpr uint16_t kXInputDpadRight = 0x0008;
inline constexpr uint16_t kXInputStart = 0x0010;
inline constexpr uint16_t kXInputBack = 0x0020;
inline constexpr uint16_t kXInputLeftThumb = 0x0040;
inline constexpr uint16_t kXInputRightThumb = 0x0080;
inline constexpr uint16_t kXInputLeftShoulder = 0x0100;
inline constexpr uint16_t kXInputRightShoulder = 0x0200;
inline constexpr uint16_t kXInputButtonA = 0x1000;
inline constexpr uint16_t kXInputButtonB = 0x2000;
inline constexpr uint16_t kXInputButtonX = 0x4000;
inline constexpr uint16_t kXInputButtonY = 0x8000;

GamepadState map_xinput_state(const XInputPadState& state);
bool is_xinput_gamepad_name(std::string_view name);

} // namespace insys::backend
