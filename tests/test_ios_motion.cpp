#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "insys/backend/ios_motion.h"
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

TEST_CASE("iOS CoreMotion gyroscope and accelerometer feed the mobile bridge") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    REQUIRE(feed_ios_gyroscope(bridge, {{1.0f, 2.0f, 3.0f}}));
    REQUIRE(feed_ios_accelerometer(bridge, {{0.0f, 1.0f, 0.0f}}));
    bridge.poll();
    manager.update(UpdateType::Dynamic);

    GyroscopeState gyro;
    std::memcpy(&gyro, by_serial(manager, "MobileGyroscope")->current_state(),
                sizeof(gyro));
    REQUIRE(gyro.angularVelocity == Vector3{1.0f, 2.0f, 3.0f});

    AccelerometerState accel;
    std::memcpy(&accel, by_serial(manager, "MobileAccelerometer")->current_state(),
                sizeof(accel));
    REQUIRE(accel.acceleration == Vector3{0.0f, 1.0f, 0.0f});
}

TEST_CASE("iOS device motion feeds rotation, gravity, linear acceleration, magnetic field and attitude") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    IosDeviceMotionSample sample;
    sample.hasRotationRate = true;
    sample.rotationRate = {0.1f, 0.2f, 0.3f};
    sample.hasGravity = true;
    sample.gravity = {0.0f, -1.0f, 0.0f};
    sample.hasUserAcceleration = true;
    sample.userAcceleration = {1.0f, 2.0f, 3.0f};
    sample.hasMagneticField = true;
    sample.magneticField = {4.0f, 5.0f, 6.0f};
    sample.hasAttitude = true;
    sample.attitude = {0.1f, 0.2f, 0.3f, 0.9f};
    REQUIRE(feed_ios_device_motion(bridge, sample));
    REQUIRE(manager.devices().size() == 5);

    bridge.poll();
    manager.update(UpdateType::Dynamic);

    GyroscopeState gyro;
    std::memcpy(&gyro, by_serial(manager, "MobileGyroscope")->current_state(),
                sizeof(gyro));
    REQUIRE(gyro.angularVelocity == Vector3{0.1f, 0.2f, 0.3f});

    GravityState gravity;
    std::memcpy(&gravity, by_serial(manager, "MobileGravity")->current_state(),
                sizeof(gravity));
    REQUIRE(gravity.gravity == Vector3{0.0f, -1.0f, 0.0f});

    LinearAccelerationState linear;
    std::memcpy(&linear,
                by_serial(manager, "MobileLinearAcceleration")->current_state(),
                sizeof(linear));
    REQUIRE(linear.acceleration == Vector3{1.0f, 2.0f, 3.0f});

    MagneticFieldState magnetic;
    std::memcpy(&magnetic,
                by_serial(manager, "MobileMagneticField")->current_state(),
                sizeof(magnetic));
    REQUIRE(magnetic.magneticField == Vector3{4.0f, 5.0f, 6.0f});

    AttitudeState attitude;
    std::memcpy(&attitude, by_serial(manager, "MobileAttitude")->current_state(),
                sizeof(attitude));
    REQUIRE(attitude.attitude.x == 0.1f);
    REQUIRE(attitude.attitude.w == 0.9f);
}

TEST_CASE("iOS device motion ignores absent components") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    REQUIRE_FALSE(feed_ios_device_motion(bridge, {}));
    REQUIRE(manager.devices().empty());

    IosDeviceMotionSample sample;
    sample.hasGravity = true;
    sample.gravity = {0.0f, -0.5f, 0.0f};
    REQUIRE(feed_ios_device_motion(bridge, sample));
    REQUIRE(manager.devices().size() == 1);
    REQUIRE(by_serial(manager, "MobileGyroscope") == nullptr);
    REQUIRE(by_serial(manager, "MobileGravity") != nullptr);
    REQUIRE(by_serial(manager, "MobileAttitude") == nullptr);
}
