#include "insys/backend/ios_motion.h"

namespace insys::backend {

bool feed_ios_gyroscope(MobileInputBridge& bridge, const IosGyroscopeSample& sample) {
    return bridge.update_gyroscope(sample.rotationRate);
}

bool feed_ios_accelerometer(MobileInputBridge& bridge,
                            const IosAccelerometerSample& sample) {
    return bridge.update_accelerometer(sample.acceleration);
}

bool feed_ios_device_motion(MobileInputBridge& bridge,
                            const IosDeviceMotionSample& sample) {
    MobileMotionSample motion;
    motion.hasAngularVelocity = sample.hasRotationRate;
    motion.angularVelocity = sample.rotationRate;
    motion.hasGravity = sample.hasGravity;
    motion.gravity = sample.gravity;
    motion.hasLinearAcceleration = sample.hasUserAcceleration;
    motion.linearAcceleration = sample.userAcceleration;
    motion.hasMagneticField = sample.hasMagneticField;
    motion.magneticField = sample.magneticField;
    motion.hasAttitude = sample.hasAttitude;
    motion.attitude = sample.attitude;
    return bridge.update_motion(motion);
}

} // namespace insys::backend
