#pragma once

#include "insys/backend/mobile_input.h"

namespace insys::backend {

enum class IosTouchPhase : int {
    Began = 0,
    Moved = 1,
    Stationary = 2,
    Ended = 3,
    Canceled = 4,
};

struct IosTouchSample {
    int id = 0;
    Vector2 position{};
    float pressure = 1.0f;
    IosTouchPhase phase = IosTouchPhase::Began;
};

bool feed_ios_touch(MobileInputBridge& bridge, const IosTouchSample& sample);

struct IosTouchCallbacks {
    void (*touch)(void* user, const IosTouchSample& sample) = nullptr;
};

bool ios_touch_attach(void* uiView, const IosTouchCallbacks& callbacks, void* user);
void ios_touch_detach();
bool ios_touch_attached();

} // namespace insys::backend
