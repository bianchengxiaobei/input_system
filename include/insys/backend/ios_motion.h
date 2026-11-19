#pragma once

#include "insys/backend/mobile_input.h"

namespace insys::backend {

// CoreMotion values use the same units as Unity's iOS sensor devices:
// acceleration/gravity in g and rotation rate in radians per second.
// CMDeviceMotion.userAcceleration is linear acceleration (gravity removed)
// and maps to LinearAcceleration, not Accelerometer.
struct IosGyroscopeSample {
    Vector3 rotationRate{};
};

struct IosAccelerometerSample {
    Vector3 acceleration{};
};

struct IosDeviceMotionSample {
    Vector3 rotationRate{};
    Vector3 gravity{};
    Vector3 userAcceleration{};
    Vector3 magneticField{};
    Quaternion attitude{};
    bool hasRotationRate = false;
    bool hasGravity = false;
    bool hasUserAcceleration = false;
    bool hasMagneticField = false;
    bool hasAttitude = false;
};

bool feed_ios_gyroscope(MobileInputBridge& bridge, const IosGyroscopeSample& sample);
bool feed_ios_accelerometer(MobileInputBridge& bridge,
                            const IosAccelerometerSample& sample);
bool feed_ios_device_motion(MobileInputBridge& bridge,
                            const IosDeviceMotionSample& sample);

} // namespace insys::backend
