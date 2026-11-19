#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "insys/backend/runtime.h"
#include "insys/backend/sensor_backend.h"
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

TEST_CASE("SensorBackend creates and updates all sensor layouts") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    SensorBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    REQUIRE(backend.add_sensor("gyro", SensorType::Gyroscope));
    REQUIRE(backend.add_sensor("accel", SensorType::Accelerometer));
    REQUIRE(backend.add_sensor("gravity", SensorType::Gravity));
    REQUIRE(backend.add_sensor("linear", SensorType::LinearAcceleration));
    REQUIRE(backend.add_sensor("magnetic", SensorType::MagneticField));
    REQUIRE(backend.add_sensor("attitude", SensorType::Attitude));
    REQUIRE(manager.devices().size() == 6);

    REQUIRE(backend.update_gyroscope("gyro", {1.0f, 2.0f, 3.0f}));
    REQUIRE(backend.update_accelerometer("accel", {4.0f, 5.0f, 6.0f}));
    REQUIRE(backend.update_gravity("gravity", {0.0f, -9.8f, 0.0f}));
    REQUIRE(backend.update_linear_acceleration("linear", {7.0f, 8.0f, 9.0f}));
    REQUIRE(backend.update_magnetic_field("magnetic", {1.0f, 2.0f, 3.0f}));
    REQUIRE(backend.update_attitude("attitude", {0.1f, 0.2f, 0.3f, 0.9f}));
    backend.poll();
    manager.update(UpdateType::Dynamic);

    GyroscopeState gyro;
    std::memcpy(&gyro, by_serial(manager, "gyro")->current_state(), sizeof(gyro));
    REQUIRE(gyro.angularVelocity == Vector3{1.0f, 2.0f, 3.0f});

    AccelerometerState accel;
    std::memcpy(&accel, by_serial(manager, "accel")->current_state(), sizeof(accel));
    REQUIRE(accel.acceleration == Vector3{4.0f, 5.0f, 6.0f});

    GravityState gravity;
    std::memcpy(&gravity, by_serial(manager, "gravity")->current_state(), sizeof(gravity));
    REQUIRE(gravity.gravity == Vector3{0.0f, -9.8f, 0.0f});

    LinearAccelerationState linear;
    std::memcpy(&linear, by_serial(manager, "linear")->current_state(), sizeof(linear));
    REQUIRE(linear.acceleration == Vector3{7.0f, 8.0f, 9.0f});

    MagneticFieldState magnetic;
    std::memcpy(&magnetic, by_serial(manager, "magnetic")->current_state(),
                sizeof(magnetic));
    REQUIRE(magnetic.magneticField == Vector3{1.0f, 2.0f, 3.0f});

    AttitudeState attitude;
    std::memcpy(&attitude, by_serial(manager, "attitude")->current_state(),
                sizeof(attitude));
    REQUIRE(attitude.attitude.x == 0.1f);
    REQUIRE(attitude.attitude.y == 0.2f);
    REQUIRE(attitude.attitude.z == 0.3f);
    REQUIRE(attitude.attitude.w == 0.9f);
}

TEST_CASE("SensorBackend coalesces updates and rejects mismatched types") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    SensorBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    REQUIRE(backend.add_sensor("gyro", SensorType::Gyroscope));
    REQUIRE_FALSE(backend.add_sensor("gyro", SensorType::Gyroscope));
    REQUIRE_FALSE(backend.add_sensor("", SensorType::Gravity));
    REQUIRE(backend.connected("gyro"));
    REQUIRE(backend.device_id("gyro") == manager.devices()[0]->deviceId);

    REQUIRE(backend.update_gyroscope("gyro", {1.0f, 2.0f, 3.0f}));
    REQUIRE_FALSE(backend.update_gyroscope("gyro", {1.0f, 2.0f, 3.0f}));
    REQUIRE(backend.update_gyroscope("gyro", {4.0f, 5.0f, 6.0f}));
    REQUIRE_FALSE(backend.update_accelerometer("gyro", {1.0f, 1.0f, 1.0f}));
    REQUIRE_FALSE(backend.update_linear_acceleration("gyro", {1.0f, 1.0f, 1.0f}));
    REQUIRE_FALSE(backend.update_magnetic_field("gyro", {1.0f, 1.0f, 1.0f}));
    REQUIRE_FALSE(backend.update_attitude("gyro", {}));
    backend.poll();
    manager.update(UpdateType::Dynamic);

    GyroscopeState gyro;
    std::memcpy(&gyro, manager.devices()[0]->current_state(), sizeof(gyro));
    REQUIRE(gyro.angularVelocity == Vector3{4.0f, 5.0f, 6.0f});
}

TEST_CASE("SensorBackend removes sensors and clears pending updates") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    SensorBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    REQUIRE(backend.add_sensor("attitude", SensorType::Attitude));
    REQUIRE(backend.update_attitude("attitude", {0.1f, 0.0f, 0.0f, 0.9f}));
    REQUIRE(backend.remove_sensor("attitude"));
    REQUIRE_FALSE(backend.remove_sensor("attitude"));
    REQUIRE_FALSE(backend.connected("attitude"));
    REQUIRE(manager.devices().empty());

    backend.poll();
    manager.update(UpdateType::Dynamic);
    REQUIRE(manager.devices().empty());
    REQUIRE_FALSE(backend.update_attitude("attitude", {}));

    backend.shutdown();
    REQUIRE(manager.devices().empty());
}

TEST_CASE("SensorBackend ignores calls before initialization") {
    SensorBackend backend;
    REQUIRE_FALSE(backend.add_sensor("gyro", SensorType::Gyroscope));
    REQUIRE_FALSE(backend.update_gyroscope("gyro", {}));
    REQUIRE_FALSE(backend.remove_sensor("gyro"));
    backend.poll();
    backend.shutdown();
}



TEST_CASE("SensorBackend creates scalar sensors and step counter") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    SensorBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    REQUIRE(backend.add_sensor("light", SensorType::Light));
    REQUIRE(backend.add_sensor("pressure", SensorType::Pressure));
    REQUIRE(backend.add_sensor("proximity", SensorType::Proximity));
    REQUIRE(backend.add_sensor("humidity", SensorType::Humidity));
    REQUIRE(backend.add_sensor("temperature", SensorType::AmbientTemperature));
    REQUIRE(backend.add_sensor("steps", SensorType::StepCounter));
    REQUIRE(backend.add_sensor("hinge", SensorType::HingeAngle));
    REQUIRE(manager.devices().size() == 7);

    REQUIRE(backend.update_light("light", 250.0f));
    REQUIRE(backend.update_pressure("pressure", 1005.0f));
    REQUIRE(backend.update_proximity("proximity", 3.0f));
    REQUIRE(backend.update_humidity("humidity", 55.0f));
    REQUIRE(backend.update_ambient_temperature("temperature", 21.0f));
    REQUIRE(backend.update_step_counter("steps", 9001));
    REQUIRE(backend.update_hinge_angle("hinge", 90.0f));
    backend.poll();
    manager.update(UpdateType::Dynamic);

    LightSensorState light;
    std::memcpy(&light, by_serial(manager, "light")->current_state(), sizeof(light));
    REQUIRE(light.lightLevel == 250.0f);

    PressureSensorState pressure;
    std::memcpy(&pressure, by_serial(manager, "pressure")->current_state(),
                sizeof(pressure));
    REQUIRE(pressure.atmosphericPressure == 1005.0f);

    ProximitySensorState proximity;
    std::memcpy(&proximity, by_serial(manager, "proximity")->current_state(),
                sizeof(proximity));
    REQUIRE(proximity.distance == 3.0f);

    HumiditySensorState humidity;
    std::memcpy(&humidity, by_serial(manager, "humidity")->current_state(),
                sizeof(humidity));
    REQUIRE(humidity.relativeHumidity == 55.0f);

    AmbientTemperatureSensorState temperature;
    std::memcpy(&temperature, by_serial(manager, "temperature")->current_state(),
                sizeof(temperature));
    REQUIRE(temperature.ambientTemperature == 21.0f);

    StepCounterState steps;
    std::memcpy(&steps, by_serial(manager, "steps")->current_state(), sizeof(steps));
    REQUIRE(steps.stepCounter == 9001);

    HingeAngleState hinge;
    std::memcpy(&hinge, by_serial(manager, "hinge")->current_state(), sizeof(hinge));
    REQUIRE(hinge.angle == 90.0f);
}
