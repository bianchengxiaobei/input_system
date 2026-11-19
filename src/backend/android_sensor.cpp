#include "insys/backend/android_sensor.h"

namespace insys::backend {

bool make_android_motion_sample(const AndroidSensorEvent& event,
                                MobileMotionSample& sample) {
    const auto& v = event.values;
    sample = {};
    switch (event.type) {
    case AndroidSensorType::Accelerometer:
        sample.hasAcceleration = true;
        sample.acceleration = {v[0], v[1], v[2]};
        return true;
    case AndroidSensorType::Gyroscope:
        sample.hasAngularVelocity = true;
        sample.angularVelocity = {v[0], v[1], v[2]};
        return true;
    case AndroidSensorType::MagneticField:
        sample.hasMagneticField = true;
        sample.magneticField = {v[0], v[1], v[2]};
        return true;
    case AndroidSensorType::Gravity:
        sample.hasGravity = true;
        sample.gravity = {v[0], v[1], v[2]};
        return true;
    case AndroidSensorType::LinearAcceleration:
        sample.hasLinearAcceleration = true;
        sample.linearAcceleration = {v[0], v[1], v[2]};
        return true;
    case AndroidSensorType::Light:
        sample.hasLightLevel = true;
        sample.lightLevel = v[0];
        return true;
    case AndroidSensorType::Pressure:
        sample.hasAtmosphericPressure = true;
        sample.atmosphericPressure = v[0];
        return true;
    case AndroidSensorType::Proximity:
        sample.hasProximityDistance = true;
        sample.proximityDistance = v[0];
        return true;
    case AndroidSensorType::RelativeHumidity:
        sample.hasRelativeHumidity = true;
        sample.relativeHumidity = v[0];
        return true;
    case AndroidSensorType::AmbientTemperature:
        sample.hasAmbientTemperature = true;
        sample.ambientTemperature = v[0];
        return true;
    case AndroidSensorType::StepCounter:
        sample.hasStepCounter = true;
        sample.stepCounter = static_cast<std::int32_t>(v[0]);
        return true;
    case AndroidSensorType::HingeAngle:
        sample.hasHingeAngle = true;
        sample.hingeAngle = v[0];
        return true;
    case AndroidSensorType::RotationVector:
    case AndroidSensorType::GameRotationVector:
        sample.hasAttitude = true;
        sample.attitude = {v[0], v[1], v[2], 0.0f};
        return true;
    default:
        return false;
    }
}

bool feed_android_sensor(MobileInputBridge& bridge, const AndroidSensorEvent& event) {
    MobileMotionSample sample;
    if (!make_android_motion_sample(event, sample))
        return false;
    return bridge.update_motion(sample);
}

} // namespace insys::backend
