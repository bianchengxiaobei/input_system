#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "insys/backend/mobile_input.h"

namespace insys::backend {

// Values match Android's Sensor.TYPE_* constants and Unity's
// AndroidSensorType in AndroidSensors.cs.
enum class AndroidSensorType : int {
    None = 0,
    Accelerometer = 1,
    MagneticField = 2,
    Orientation = 3,
    Gyroscope = 4,
    Light = 5,
    Pressure = 6,
    Temperature = 7,
    Proximity = 8,
    Gravity = 9,
    LinearAcceleration = 10,
    RotationVector = 11,
    RelativeHumidity = 12,
    AmbientTemperature = 13,
    MagneticFieldUncalibrated = 14,
    GameRotationVector = 15,
    GyroscopeUncalibrated = 16,
    SignificantMotion = 17,
    StepDetector = 18,
    StepCounter = 19,
    GeomagneticRotationVector = 20,
    HeartRate = 21,
    Pose6DOF = 28,
    StationaryDetect = 29,
    MotionDetect = 30,
    HeartBeat = 31,
    LowLatencyOffBodyDetect = 34,
    AccelerometerUncalibrated = 35,
    HingeAngle = 36,
};

struct AndroidSensorEvent {
    AndroidSensorType type = AndroidSensorType::None;
    std::array<float, 16> values{};
};

// Mirrors AndroidSensorState.WithData: only the first values are used and
// the remaining slots stay zero. Values are forwarded in Android units
// (m/s^2 for acceleration, rad/s for gyroscope); use AndroidCompensate*
// processors when binding if Unity-compatible orientation is required.
bool feed_android_sensor(MobileInputBridge& bridge, const AndroidSensorEvent& event);

// Converts the shared sensor slots into the portable motion sample used by
// MobileInputQueue. Returns false for sensor types without a core device.
bool make_android_motion_sample(const AndroidSensorEvent& event,
                                MobileMotionSample& sample);

} // namespace insys::backend

