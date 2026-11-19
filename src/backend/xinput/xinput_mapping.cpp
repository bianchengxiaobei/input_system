#include "insys/backend/xinput_mapping.h"

#include <algorithm>
#include <cctype>

namespace insys::backend {
namespace {

float normalize_axis(int16_t value) {
    return std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
}

bool contains_case_insensitive(std::string_view text, std::string_view needle) {
    if (needle.empty() || needle.size() > text.size())
        return false;
    for (std::size_t i = 0; i + needle.size() <= text.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            const auto lhs = static_cast<unsigned char>(text[i + j]);
            const auto rhs = static_cast<unsigned char>(needle[j]);
            if (std::tolower(lhs) != std::tolower(rhs)) {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

} // namespace

GamepadState map_xinput_state(const XInputPadState& state) {
    GamepadState result;

    struct ButtonMap {
        uint16_t xinput;
        uint32_t gamepad;
    };

    static constexpr ButtonMap buttons[] = {
        {kXInputDpadUp, 1u << 0},
        {kXInputDpadDown, 1u << 1},
        {kXInputDpadLeft, 1u << 2},
        {kXInputDpadRight, 1u << 3},
        {kXInputButtonY, 1u << 4},
        {kXInputButtonB, 1u << 5},
        {kXInputButtonA, 1u << 6},
        {kXInputButtonX, 1u << 7},
        {kXInputLeftThumb, 1u << 8},
        {kXInputRightThumb, 1u << 9},
        {kXInputLeftShoulder, 1u << 10},
        {kXInputRightShoulder, 1u << 11},
        {kXInputStart, 1u << 12},
        {kXInputBack, 1u << 13},
    };

    for (const ButtonMap& button : buttons) {
        if ((state.buttons & button.xinput) != 0)
            result.buttons |= button.gamepad;
    }

    result.leftStick = {normalize_axis(state.leftThumbX), normalize_axis(state.leftThumbY)};
    result.rightStick = {normalize_axis(state.rightThumbX), normalize_axis(state.rightThumbY)};
    result.leftTrigger = static_cast<float>(state.leftTrigger) / 255.0f;
    result.rightTrigger = static_cast<float>(state.rightTrigger) / 255.0f;
    return result;
}

bool is_xinput_gamepad_name(std::string_view name) {
    return contains_case_insensitive(name, "xinput") ||
           contains_case_insensitive(name, "xbox") ||
           contains_case_insensitive(name, "x-box");
}

} // namespace insys::backend
