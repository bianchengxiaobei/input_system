#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "insys/backend/mobile_input.h"
#include "insys/backend/runtime.h"
#include "insys/device.h"
#include "insys/device_states.h"
#include "insys/manager.h"

using namespace insys;
using namespace insys::backend;

namespace {

Device* by_serial(Manager& manager, const char* serial) {
    for (Device* device : manager.devices()) {
        if (device->description.serial == serial)
            return device;
    }
    return nullptr;
}

} // namespace

TEST_CASE("MobileInputBridge routes touch input through TouchBackend") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    REQUIRE(bridge.touch_began(1, {10.0f, 20.0f}));
    REQUIRE(bridge.touch_moved(1, {15.0f, 25.0f}));
    REQUIRE(bridge.touch_ended(1));
    bridge.poll();
    manager.update(UpdateType::Dynamic);

    Device* touchscreen = by_serial(manager, "Touchscreen");
    REQUIRE(touchscreen != nullptr);
    TouchscreenState state;
    std::memcpy(&state, touchscreen->current_state(), sizeof(state));
    REQUIRE(state.touches[0].position == Vector2{15.0f, 25.0f});
    REQUIRE(state.touches[0].delta == Vector2{5.0f, 5.0f});
    REQUIRE(state.touches[0].phase == static_cast<float>(TouchPhase::Ended));
}

TEST_CASE("MobileInputBridge updates all motion sensors") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    MobileMotionSample sample;
    sample.hasAngularVelocity = true;
    sample.angularVelocity = {1.0f, 2.0f, 3.0f};
    sample.hasAcceleration = true;
    sample.acceleration = {4.0f, 5.0f, 6.0f};
    sample.hasGravity = true;
    sample.gravity = {0.0f, -9.8f, 0.0f};
    sample.hasLinearAcceleration = true;
    sample.linearAcceleration = {7.0f, 8.0f, 9.0f};
    sample.hasMagneticField = true;
    sample.magneticField = {1.0f, 2.0f, 3.0f};
    sample.hasAttitude = true;
    sample.attitude = {0.1f, 0.2f, 0.3f, 0.9f};

    REQUIRE(bridge.update_motion(sample));
    REQUIRE(manager.devices().size() == 6);
    bridge.poll();
    manager.update(UpdateType::Dynamic);

    GyroscopeState gyro;
    std::memcpy(&gyro, by_serial(manager, "MobileGyroscope")->current_state(),
                sizeof(gyro));
    REQUIRE(gyro.angularVelocity == Vector3{1.0f, 2.0f, 3.0f});

    AccelerometerState accel;
    std::memcpy(&accel, by_serial(manager, "MobileAccelerometer")->current_state(),
                sizeof(accel));
    REQUIRE(accel.acceleration == Vector3{4.0f, 5.0f, 6.0f});

    GravityState gravity;
    std::memcpy(&gravity, by_serial(manager, "MobileGravity")->current_state(),
                sizeof(gravity));
    REQUIRE(gravity.gravity == Vector3{0.0f, -9.8f, 0.0f});

    LinearAccelerationState linear;
    std::memcpy(&linear,
                by_serial(manager, "MobileLinearAcceleration")->current_state(),
                sizeof(linear));
    REQUIRE(linear.acceleration == Vector3{7.0f, 8.0f, 9.0f});

    MagneticFieldState magnetic;
    std::memcpy(&magnetic,
                by_serial(manager, "MobileMagneticField")->current_state(),
                sizeof(magnetic));
    REQUIRE(magnetic.magneticField == Vector3{1.0f, 2.0f, 3.0f});

    AttitudeState attitude;
    std::memcpy(&attitude, by_serial(manager, "MobileAttitude")->current_state(),
                sizeof(attitude));
    REQUIRE(attitude.attitude.x == 0.1f);
    REQUIRE(attitude.attitude.w == 0.9f);
}

TEST_CASE("MobileInputBridge creates sensors lazily and coalesces motion") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    REQUIRE(bridge.update_gyroscope({1.0f, 2.0f, 3.0f}));
    REQUIRE_FALSE(bridge.update_gyroscope({1.0f, 2.0f, 3.0f}));
    REQUIRE(manager.devices().size() == 1);
    REQUIRE_FALSE(bridge.update_motion({}));

    MobileMotionSample sample;
    sample.hasAngularVelocity = true;
    sample.angularVelocity = {4.0f, 5.0f, 6.0f};
    sample.hasGravity = true;
    sample.gravity = {0.0f, -1.0f, 0.0f};
    REQUIRE(bridge.update_motion(sample));
    REQUIRE(manager.devices().size() == 2);

    bridge.poll();
    manager.update(UpdateType::Dynamic);
    GyroscopeState gyro;
    std::memcpy(&gyro, by_serial(manager, "MobileGyroscope")->current_state(),
                sizeof(gyro));
    REQUIRE(gyro.angularVelocity == Vector3{4.0f, 5.0f, 6.0f});
    REQUIRE(by_serial(manager, "MobileAccelerometer") == nullptr);
    REQUIRE(by_serial(manager, "MobileAttitude") == nullptr);
}

TEST_CASE("MobileInputBridge ignores calls before initialization and shuts down") {
    MobileInputBridge bridge;
    REQUIRE_FALSE(bridge.touch_began(1, {}));
    REQUIRE_FALSE(bridge.update_gyroscope({}));
    bridge.poll();
    bridge.shutdown();

    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    REQUIRE(bridge.initialize(runtime, manager));
    REQUIRE(bridge.touch_began(1, {}));
    REQUIRE(bridge.update_gravity({0.0f, -9.8f, 0.0f}));
    bridge.shutdown();
    REQUIRE(manager.devices().empty());
}

TEST_CASE("MobileInputBridge cancels active touches on suspend") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    REQUIRE(bridge.touch_began(1, {10.0f, 20.0f}));
    REQUIRE(bridge.touch_began(2, {30.0f, 40.0f}));
    REQUIRE(bridge.suspend() == 2);
    REQUIRE(bridge.suspend() == 0);

    bridge.poll();
    manager.update(UpdateType::Dynamic);
    Device* touchscreen = by_serial(manager, "Touchscreen");
    TouchscreenState state;
    std::memcpy(&state, touchscreen->current_state(), sizeof(state));
    REQUIRE(state.touches[0].phase == static_cast<float>(TouchPhase::Canceled));
    REQUIRE(state.touches[1].phase == static_cast<float>(TouchPhase::Canceled));

    REQUIRE(bridge.touch_began(3, {50.0f, 60.0f}));
}


TEST_CASE("MobileInputBridge updates scalar sensors and step counter") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    MobileMotionSample sample;
    sample.hasLightLevel = true;
    sample.lightLevel = 123.0f;
    sample.hasAtmosphericPressure = true;
    sample.atmosphericPressure = 1002.0f;
    sample.hasProximityDistance = true;
    sample.proximityDistance = 5.0f;
    sample.hasRelativeHumidity = true;
    sample.relativeHumidity = 60.0f;
    sample.hasAmbientTemperature = true;
    sample.ambientTemperature = 19.5f;
    sample.hasHingeAngle = true;
    sample.hingeAngle = 45.0f;
    sample.hasStepCounter = true;
    sample.stepCounter = 321;

    REQUIRE(bridge.update_motion(sample));
    REQUIRE(manager.devices().size() == 7);
    bridge.poll();
    manager.update(UpdateType::Dynamic);

    LightSensorState light;
    std::memcpy(&light, by_serial(manager, "MobileLight")->current_state(),
                sizeof(light));
    REQUIRE(light.lightLevel == 123.0f);

    PressureSensorState pressure;
    std::memcpy(&pressure, by_serial(manager, "MobilePressure")->current_state(),
                sizeof(pressure));
    REQUIRE(pressure.atmosphericPressure == 1002.0f);

    ProximitySensorState proximity;
    std::memcpy(&proximity, by_serial(manager, "MobileProximity")->current_state(),
                sizeof(proximity));
    REQUIRE(proximity.distance == 5.0f);

    HumiditySensorState humidity;
    std::memcpy(&humidity, by_serial(manager, "MobileHumidity")->current_state(),
                sizeof(humidity));
    REQUIRE(humidity.relativeHumidity == 60.0f);

    AmbientTemperatureSensorState temperature;
    std::memcpy(&temperature,
                by_serial(manager, "MobileAmbientTemperature")->current_state(),
                sizeof(temperature));
    REQUIRE(temperature.ambientTemperature == 19.5f);

    HingeAngleState hinge;
    std::memcpy(&hinge, by_serial(manager, "MobileHingeAngle")->current_state(),
                sizeof(hinge));
    REQUIRE(hinge.angle == 45.0f);

    StepCounterState steps;
    std::memcpy(&steps, by_serial(manager, "MobileStepCounter")->current_state(),
                sizeof(steps));
    REQUIRE(steps.stepCounter == 321);
}
