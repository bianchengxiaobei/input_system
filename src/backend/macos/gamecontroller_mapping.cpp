#include "insys/backend/gamecontroller_mapping.h"

#include <algorithm>

namespace insys::backend {
namespace {

void set_button(GamepadState& state, bool pressed, uint32_t bit) {
    if (pressed)
        state.buttons |= 1u << bit;
}

float clamp_trigger(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float clamp_axis(float value) {
    return std::clamp(value, -1.0f, 1.0f);
}

} // namespace

GamepadState map_gamecontroller_state(const GameControllerState& state) {
    GamepadState result;
    set_button(result, state.dpadUp, 0);
    set_button(result, state.dpadDown, 1);
    set_button(result, state.dpadLeft, 2);
    set_button(result, state.dpadRight, 3);
    set_button(result, state.buttonY, 4);
    set_button(result, state.buttonB, 5);
    set_button(result, state.buttonA, 6);
    set_button(result, state.buttonX, 7);
    set_button(result, state.leftThumbstickButton, 8);
    set_button(result, state.rightThumbstickButton, 9);
    set_button(result, state.leftShoulder, 10);
    set_button(result, state.rightShoulder, 11);
    set_button(result, state.buttonMenu, 12);
    set_button(result, state.buttonOptions, 13);

    result.leftStick = {clamp_axis(state.leftThumbstickX),
                        clamp_axis(state.leftThumbstickY)};
    result.rightStick = {clamp_axis(state.rightThumbstickX),
                         clamp_axis(state.rightThumbstickY)};
    result.leftTrigger = clamp_trigger(state.leftTrigger);
    result.rightTrigger = clamp_trigger(state.rightTrigger);
    return result;
}

} // namespace insys::backend
