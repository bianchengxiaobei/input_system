#include "glfw_gamepad_map.h"

namespace insys::backend {

GamepadState map_gamepad_state(const GLFWgamepadstate& state) {
    GamepadState result;

    auto set_bit = [&](uint32_t bit, int button) {
        if (state.buttons[button] == GLFW_PRESS)
            result.buttons |= (1u << bit);
    };

    set_bit(0, GLFW_GAMEPAD_BUTTON_DPAD_UP);
    set_bit(1, GLFW_GAMEPAD_BUTTON_DPAD_DOWN);
    set_bit(2, GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
    set_bit(3, GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
    set_bit(4, GLFW_GAMEPAD_BUTTON_Y);
    set_bit(5, GLFW_GAMEPAD_BUTTON_B);
    set_bit(6, GLFW_GAMEPAD_BUTTON_A);
    set_bit(7, GLFW_GAMEPAD_BUTTON_X);
    set_bit(8, GLFW_GAMEPAD_BUTTON_LEFT_THUMB);
    set_bit(9, GLFW_GAMEPAD_BUTTON_RIGHT_THUMB);
    set_bit(10, GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
    set_bit(11, GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
    set_bit(12, GLFW_GAMEPAD_BUTTON_START);
    set_bit(13, GLFW_GAMEPAD_BUTTON_BACK);

    result.leftStick = {state.axes[GLFW_GAMEPAD_AXIS_LEFT_X],
                        state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]};
    result.rightStick = {state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X],
                         state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]};
    result.leftTrigger =
        (state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f;
    result.rightTrigger =
        (state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f;

    return result;
}

} // namespace insys::backend
