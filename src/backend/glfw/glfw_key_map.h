#pragma once

#include "insys/low/key.h"

namespace insys::backend {

bool map_glfw_key(int glfwKey, Key& outKey);

} // namespace insys::backend
