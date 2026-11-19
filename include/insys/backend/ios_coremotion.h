#pragma once

#include "insys/backend/ios_motion.h"

namespace insys::backend {

struct IosCoreMotionCallbacks {
    void (*deviceMotion)(void* user, const IosDeviceMotionSample& sample) = nullptr;
    void (*accelerometer)(void* user, const IosAccelerometerSample& sample) = nullptr;
    void (*gyroscope)(void* user, const IosGyroscopeSample& sample) = nullptr;
};

// Starts CoreMotion on the main queue. Callbacks receive values in the same
// units as feed_ios_* and may push them into MobileInputQueue. Returns false
// when CoreMotion is unavailable or no callback is provided.
bool ios_coremotion_start(const IosCoreMotionCallbacks& callbacks, void* user,
                          double interval = 1.0 / 60.0);
void ios_coremotion_stop();
bool ios_coremotion_running();

} // namespace insys::backend
