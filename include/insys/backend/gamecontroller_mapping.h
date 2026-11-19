#pragma once

#include "insys/device_states.h"

namespace insys::backend {

struct GameControllerState {
    bool buttonA = false;
    bool buttonB = false;
    bool buttonX = false;
    bool buttonY = false;
    bool leftShoulder = false;
    bool rightShoulder = false;
    float leftTrigger = 0.0f;
    float rightTrigger = 0.0f;
    float leftThumbstickX = 0.0f;
    float leftThumbstickY = 0.0f;
    float rightThumbstickX = 0.0f;
    float rightThumbstickY = 0.0f;
    bool dpadUp = false;
    bool dpadDown = false;
    bool dpadLeft = false;
    bool dpadRight = false;
    bool buttonMenu = false;
    bool buttonOptions = false;
    bool leftThumbstickButton = false;
    bool rightThumbstickButton = false;
};

GamepadState map_gamecontroller_state(const GameControllerState& state);

} // namespace insys::backend
