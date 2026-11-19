#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "insys/backend/android_sensor.h"
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

TEST_CASE("Android sensor events forward values to the mobile bridge") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    REQUIRE(feed_android_sensor(
        bridge, {AndroidSensorType::Accelerometer, {0.0f, 9.80665f, 0.0f}}));
    REQUIRE(feed_android_sensor(
        bridge, {AndroidSensorType::Gyroscope, {1.0f, 2.0f, 3.0f}}));
    REQUIRE(feed_android_sensor(
        bridge, {AndroidSensorType::MagneticField, {4.0f, 5.0f, 6.0f}}));
    REQUIRE(feed_android_sensor(
        bridge, {AndroidSensorType::Gravity, {0.0f, -9.80665f, 0.0f}}));
    REQUIRE(feed_android_sensor(
        bridge, {AndroidSensorType::LinearAcceleration, {1.0f, 2.0f, 3.0f}}));
    REQUIRE(feed_android_sensor(
        bridge, {AndroidSensorType::RotationVector, {0.0f, 0.0f, 0.6f, 0.0f}}));

    bridge.poll();
    manager.update(UpdateType::Dynamic);

    AccelerometerState accel;
    std::memcpy(&accel, by_serial(manager, "MobileAccelerometer")->current_state(),
                sizeof(accel));
    REQUIRE(accel.acceleration == Vector3{0.0f, 9.80665f, 0.0f});

    GyroscopeState gyro;
    std::memcpy(&gyro, by_serial(manager, "MobileGyroscope")->current_state(),
                sizeof(gyro));
    REQUIRE(gyro.angularVelocity == Vector3{1.0f, 2.0f, 3.0f});

    MagneticFieldState magnetic;
    std::memcpy(&magnetic,
                by_serial(manager, "MobileMagneticField")->current_state(),
                sizeof(magnetic));
    REQUIRE(magnetic.magneticField == Vector3{4.0f, 5.0f, 6.0f});

    GravityState gravity;
    std::memcpy(&gravity, by_serial(manager, "MobileGravity")->current_state(),
                sizeof(gravity));
    REQUIRE(gravity.gravity == Vector3{0.0f, -9.80665f, 0.0f});

    LinearAccelerationState linear;
    std::memcpy(&linear,
                by_serial(manager, "MobileLinearAcceleration")->current_state(),
                sizeof(linear));
    REQUIRE(linear.acceleration == Vector3{1.0f, 2.0f, 3.0f});

    AttitudeState attitude;
    std::memcpy(&attitude, by_serial(manager, "MobileAttitude")->current_state(),
                sizeof(attitude));
    REQUIRE(attitude.attitude.x == 0.0f);
    REQUIRE(attitude.attitude.z == 0.6f);
    REQUIRE(attitude.attitude.w == 0.0f);
}

TEST_CASE("Android sensor events ignore unsupported sensor types") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    REQUIRE_FALSE(feed_android_sensor(
        bridge, {AndroidSensorType::HeartRate, {72.0f}}));
    REQUIRE_FALSE(feed_android_sensor(
        bridge, {AndroidSensorType::SignificantMotion, {1.0f}}));
    REQUIRE(manager.devices().empty());
    bridge.poll();
    manager.update(UpdateType::Dynamic);
    REQUIRE(manager.devices().empty());
}

TEST_CASE("Android game rotation vector feeds the attitude sensor") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    REQUIRE(feed_android_sensor(
        bridge, {AndroidSensorType::GameRotationVector, {0.1f, 0.2f, 0.3f, 0.0f}}));
    bridge.poll();
    manager.update(UpdateType::Dynamic);

    AttitudeState attitude;
    std::memcpy(&attitude, by_serial(manager, "MobileAttitude")->current_state(),
                sizeof(attitude));
    REQUIRE(attitude.attitude.x == 0.1f);
    REQUIRE(attitude.attitude.y == 0.2f);
    REQUIRE(attitude.attitude.z == 0.3f);
    REQUIRE(attitude.attitude.w == 0.0f);
}

TEST_CASE("Android sensor events convert to portable motion samples") {
    MobileMotionSample sample;
    REQUIRE(make_android_motion_sample(
        {AndroidSensorType::Accelerometer, {1.0f, 2.0f, 3.0f}}, sample));
    REQUIRE(sample.hasAcceleration);
    REQUIRE_FALSE(sample.hasGravity);
    REQUIRE(sample.acceleration == Vector3{1.0f, 2.0f, 3.0f});

    REQUIRE(make_android_motion_sample(
        {AndroidSensorType::Gyroscope, {4.0f, 5.0f, 6.0f}}, sample));
    REQUIRE(sample.hasAngularVelocity);
    REQUIRE_FALSE(sample.hasAcceleration);
    REQUIRE(sample.angularVelocity == Vector3{4.0f, 5.0f, 6.0f});

    REQUIRE(make_android_motion_sample(
        {AndroidSensorType::MagneticField, {1.0f, 2.0f, 3.0f}}, sample));
    REQUIRE(sample.hasMagneticField);
    REQUIRE(sample.magneticField == Vector3{1.0f, 2.0f, 3.0f});

    REQUIRE(make_android_motion_sample(
        {AndroidSensorType::Gravity, {0.0f, -9.8f, 0.0f}}, sample));
    REQUIRE(sample.hasGravity);
    REQUIRE(sample.gravity == Vector3{0.0f, -9.8f, 0.0f});

    REQUIRE(make_android_motion_sample(
        {AndroidSensorType::LinearAcceleration, {7.0f, 8.0f, 9.0f}}, sample));
    REQUIRE(sample.hasLinearAcceleration);
    REQUIRE(sample.linearAcceleration == Vector3{7.0f, 8.0f, 9.0f});

    REQUIRE(make_android_motion_sample(
        {AndroidSensorType::RotationVector, {0.1f, 0.2f, 0.3f, 0.9f}}, sample));
    REQUIRE(sample.hasAttitude);
    REQUIRE(sample.attitude.x == 0.1f);
    REQUIRE(sample.attitude.z == 0.3f);
    REQUIRE(sample.attitude.w == 0.0f);

    REQUIRE_FALSE(make_android_motion_sample(
        {AndroidSensorType::HeartRate, {72.0f}}, sample));
}



TEST_CASE("Android scalar sensors forward values through the mobile bridge") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    REQUIRE(feed_android_sensor(
        bridge, {AndroidSensorType::Light, {320.0f}}));
    REQUIRE(feed_android_sensor(
        bridge, {AndroidSensorType::Pressure, {1013.25f}}));
    REQUIRE(feed_android_sensor(
        bridge, {AndroidSensorType::Proximity, {4.0f}}));
    REQUIRE(feed_android_sensor(
        bridge, {AndroidSensorType::RelativeHumidity, {45.0f}}));
    REQUIRE(feed_android_sensor(
        bridge, {AndroidSensorType::AmbientTemperature, {22.5f}}));
    REQUIRE(feed_android_sensor(
        bridge, {AndroidSensorType::HingeAngle, {135.0f}}));
    REQUIRE(feed_android_sensor(
        bridge, {AndroidSensorType::StepCounter, {12345.0f}}));

    bridge.poll();
    manager.update(UpdateType::Dynamic);

    LightSensorState light;
    std::memcpy(&light, by_serial(manager, "MobileLight")->current_state(),
                sizeof(light));
    REQUIRE(light.lightLevel == 320.0f);

    PressureSensorState pressure;
    std::memcpy(&pressure, by_serial(manager, "MobilePressure")->current_state(),
                sizeof(pressure));
    REQUIRE(pressure.atmosphericPressure == 1013.25f);

    ProximitySensorState proximity;
    std::memcpy(&proximity, by_serial(manager, "MobileProximity")->current_state(),
                sizeof(proximity));
    REQUIRE(proximity.distance == 4.0f);

    HumiditySensorState humidity;
    std::memcpy(&humidity, by_serial(manager, "MobileHumidity")->current_state(),
                sizeof(humidity));
    REQUIRE(humidity.relativeHumidity == 45.0f);

    AmbientTemperatureSensorState temperature;
    std::memcpy(&temperature,
                by_serial(manager, "MobileAmbientTemperature")->current_state(),
                sizeof(temperature));
    REQUIRE(temperature.ambientTemperature == 22.5f);

    HingeAngleState hinge;
    std::memcpy(&hinge, by_serial(manager, "MobileHingeAngle")->current_state(),
                sizeof(hinge));
    REQUIRE(hinge.angle == 135.0f);

    StepCounterState steps;
    std::memcpy(&steps, by_serial(manager, "MobileStepCounter")->current_state(),
                sizeof(steps));
    REQUIRE(steps.stepCounter == 12345);
}

TEST_CASE("Android scalar sensor events create portable motion fields") {
    MobileMotionSample sample;
    REQUIRE(make_android_motion_sample(
        {AndroidSensorType::Light, {10.0f}}, sample));
    REQUIRE(sample.hasLightLevel);
    REQUIRE(sample.lightLevel == 10.0f);

    REQUIRE(make_android_motion_sample(
        {AndroidSensorType::Pressure, {1000.0f}}, sample));
    REQUIRE(sample.hasAtmosphericPressure);
    REQUIRE(sample.atmosphericPressure == 1000.0f);

    REQUIRE(make_android_motion_sample(
        {AndroidSensorType::StepCounter, {42.0f}}, sample));
    REQUIRE(sample.hasStepCounter);
    REQUIRE(sample.stepCounter == 42);
}
