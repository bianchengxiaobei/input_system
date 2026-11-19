#pragma once

#include "insys/update_type.h"

namespace insys {

struct Settings {
    float defaultDeadzoneMin = 0.125f;
    float defaultDeadzoneMax = 0.925f;
    float defaultButtonPressPoint = 0.5f;
    float buttonReleaseThreshold = 0.75f;
    float defaultTapTime = 0.2f;
    float defaultSlowTapTime = 0.5f;
    float defaultHoldTime = 0.4f;
    float tapRadius = 5.0f;
    float multiTapDelayTime = 0.75f;

    int maxQueuedEventsPerUpdate = 1000;
    int maxEventBytesPerUpdate = 5 * 1024 * 1024;

    float pollingFrequency = 60.0f;

    bool compensateForScreenOrientation = true;
    bool shortcutKeysConsumeInput = false;

    bool useRawInput = false;
    bool useXInput = false;
    bool useHid = false;

    UpdateMask updateMask = static_cast<UpdateMask>(UpdateType::Default);
};

} // namespace insys
