#pragma once

#include <GLFW/glfw3.h>

#include "insys/device_states.h"

namespace insys::backend {

GamepadState map_gamepad_state(const GLFWgamepadstate& state);

} // namespace insys::backend
