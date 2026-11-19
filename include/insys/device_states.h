#pragma once

#include <cstdint>

#include "insys/types.h"

namespace insys {

struct MouseState {
    Vector2 position;
    Vector2 delta;
    Vector2 scroll;
    uint8_t buttons[4] = {};
};
static_assert(sizeof(MouseState) == 28, "MouseState must match the Mouse layout");

struct GamepadState {
    uint32_t buttons = 0;
    Vector2 leftStick;
    Vector2 rightStick;
    float leftTrigger = 0.0f;
    float rightTrigger = 0.0f;
};
static_assert(sizeof(GamepadState) == 28, "GamepadState must match the Gamepad layout");

struct JoystickState {
    uint32_t buttons = 0;
    Vector2 stick;
    float trigger = 0.0f;
};
static_assert(sizeof(JoystickState) == 16, "JoystickState must match the Joystick layout");

struct GyroscopeState {
    Vector3 angularVelocity;
};
static_assert(sizeof(GyroscopeState) == 12, "GyroscopeState must match the Gyroscope layout");

struct AccelerometerState {
    Vector3 acceleration;
};
static_assert(sizeof(AccelerometerState) == 12,
              "AccelerometerState must match the Accelerometer layout");

struct GravityState {
    Vector3 gravity;
};
static_assert(sizeof(GravityState) == 12, "GravityState must match the Gravity layout");

struct LinearAccelerationState {
    Vector3 acceleration;
};
static_assert(sizeof(LinearAccelerationState) == 12,
              "LinearAccelerationState must match the LinearAcceleration layout");

struct MagneticFieldState {
    Vector3 magneticField;
};
static_assert(sizeof(MagneticFieldState) == 12,
              "MagneticFieldState must match the MagneticField layout");

struct LightSensorState {
    float lightLevel = 0.0f;
};
static_assert(sizeof(LightSensorState) == 4,
              "LightSensorState must match the Light layout");

struct PressureSensorState {
    float atmosphericPressure = 0.0f;
};
static_assert(sizeof(PressureSensorState) == 4,
              "PressureSensorState must match the Pressure layout");

struct ProximitySensorState {
    float distance = 0.0f;
};
static_assert(sizeof(ProximitySensorState) == 4,
              "ProximitySensorState must match the Proximity layout");

struct HumiditySensorState {
    float relativeHumidity = 0.0f;
};
static_assert(sizeof(HumiditySensorState) == 4,
              "HumiditySensorState must match the Humidity layout");

struct AmbientTemperatureSensorState {
    float ambientTemperature = 0.0f;
};
static_assert(sizeof(AmbientTemperatureSensorState) == 4,
              "AmbientTemperatureSensorState must match the Ambient Temperature layout");

struct StepCounterState {
    int32_t stepCounter = 0;
};
static_assert(sizeof(StepCounterState) == 4,
              "StepCounterState must match the Step Counter layout");

struct HingeAngleState {
    float angle = 0.0f;
};
static_assert(sizeof(HingeAngleState) == 4,
              "HingeAngleState must match the Hinge Angle layout");

struct AttitudeState {
    Quaternion attitude;
};
static_assert(sizeof(AttitudeState) == 16, "AttitudeState must match the Attitude layout");

enum class TouchPhase {
    Began = 0,
    Moved = 1,
    Stationary = 2,
    Ended = 3,
    Canceled = 4,
};

struct TouchPointState {
    Vector2 position;
    Vector2 delta;
    Vector2 startPosition;
    float phase = 0.0f;
    float tapCount = 0.0f;
    float press = 0.0f;
};
static_assert(sizeof(TouchPointState) == 36,
              "TouchPointState must match the per-touch layout");

inline constexpr int kMaxTouches = 10;

enum class PenButton {
    Tip = 0,
    Eraser = 1,
    BarrelFirst = 2,
    BarrelSecond = 3,
    InRange = 4,
    BarrelThird = 5,
    BarrelFourth = 6,
};

struct PenState {
    Vector2 position;
    Vector2 delta;
    Vector2 tilt;
    float pressure = 0.0f;
    float twist = 0.0f;
    uint16_t buttons = 0;
    uint16_t displayIndex = 0;
};
static_assert(sizeof(PenState) == 36, "PenState must match the Pen layout");

struct TrackedDeviceState {
    int32_t trackingState = 0;
    uint32_t buttons = 0;
    Vector3 devicePosition;
    Quaternion deviceRotation;
};
static_assert(sizeof(TrackedDeviceState) == 36,
              "TrackedDeviceState must match the TrackedDevice layout");

struct TouchscreenState {
    TouchPointState touches[kMaxTouches];
};
static_assert(sizeof(TouchscreenState) == 360,
              "TouchscreenState must match the Touchscreen layout");

} // namespace insys

