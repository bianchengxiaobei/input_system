#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "action_test_helpers.h"
#include "insys/actions/action_asset.h"
#include "insys/actions/action_state.h"
#include "insys/device.h"
#include "insys/device_builder.h"
#include "insys/control_path.h"
#include "insys/device_states.h"
#include "insys/layout_registry.h"
#include "insys/low/event.h"
#include "insys/low/state_format.h"
#include "insys/manager.h"

using namespace insys;

namespace {

DeviceDescription description(std::string product) {
    DeviceDescription value;
    value.interfaceName = "Test";
    value.product = std::move(product);
    return value;
}

} // namespace

TEST_CASE("Sensor layouts expose vector controls") {
    LayoutRegistry registry;
    register_default_layouts(registry);
    DeviceBuilder builder(registry);

    auto gyroDevice = builder.build("Gyroscope", "", 1, description("Gyroscope"));
    auto accelerometerDevice =
        builder.build("Accelerometer", "", 2, description("Accelerometer"));
    auto gravityDevice = builder.build("Gravity", "", 3, description("Gravity"));
    auto linearDevice =
        builder.build("LinearAcceleration", "", 5, description("LinearAcceleration"));
    auto magneticDevice =
        builder.build("MagneticField", "", 6, description("MagneticField"));

    REQUIRE(gyroDevice != nullptr);
    REQUIRE(accelerometerDevice != nullptr);
    REQUIRE(gravityDevice != nullptr);
    REQUIRE(linearDevice != nullptr);
    REQUIRE(magneticDevice != nullptr);

    auto* gyro = static_cast<Gyroscope*>(gyroDevice.get());
    auto* accelerometer = static_cast<Accelerometer*>(accelerometerDevice.get());
    auto* gravity = static_cast<Gravity*>(gravityDevice.get());
    auto* linear = static_cast<LinearAcceleration*>(linearDevice.get());
    auto* magnetic = static_cast<MagneticField*>(magneticDevice.get());
    REQUIRE(gyro->angular_velocity() != nullptr);
    REQUIRE(accelerometer->acceleration() != nullptr);
    REQUIRE(gravity->gravity() != nullptr);
    REQUIRE(linear->acceleration() != nullptr);
    REQUIRE(magnetic->magnetic_field() != nullptr);
    REQUIRE(gyro->state_size_bytes() == 12);
    REQUIRE(accelerometer->state_size_bytes() == 12);
    REQUIRE(gravity->state_size_bytes() == 12);
    REQUIRE(linear->state_size_bytes() == 12);
    REQUIRE(magnetic->state_size_bytes() == 12);

    GyroscopeState gyroState{{1.0f, 2.0f, 3.0f}};
    AccelerometerState accelerometerState{{4.0f, 5.0f, 6.0f}};
    GravityState gravityState{{0.0f, -9.8f, 0.0f}};
    LinearAccelerationState linearState{{1.0f, 2.0f, 3.0f}};
    MagneticFieldState magneticState{{4.0f, 5.0f, 6.0f}};
    std::vector<std::uint8_t> gyroBytes(gyro->state_size_bytes());
    std::vector<std::uint8_t> accelerometerBytes(accelerometer->state_size_bytes());
    std::vector<std::uint8_t> gravityBytes(gravity->state_size_bytes());
    std::vector<std::uint8_t> linearBytes(linear->state_size_bytes());
    std::vector<std::uint8_t> magneticBytes(magnetic->state_size_bytes());

    gyro->write_value_to_state(gyroBytes.data(), &gyroState);
    accelerometer->write_value_to_state(accelerometerBytes.data(), &accelerometerState);
    gravity->write_value_to_state(gravityBytes.data(), &gravityState);
    linear->write_value_to_state(linearBytes.data(), &linearState);
    magnetic->write_value_to_state(magneticBytes.data(), &magneticState);

    REQUIRE(gyro->angular_velocity()->read_vector3(gyroBytes.data()) ==
            Vector3{1.0f, 2.0f, 3.0f});
    REQUIRE(accelerometer->acceleration()->read_vector3(accelerometerBytes.data()) ==
            Vector3{4.0f, 5.0f, 6.0f});
    REQUIRE(gravity->gravity()->read_vector3(gravityBytes.data()) ==
            Vector3{0.0f, -9.8f, 0.0f});
    REQUIRE(linear->acceleration()->read_vector3(linearBytes.data()) ==
            Vector3{1.0f, 2.0f, 3.0f});
    REQUIRE(magnetic->magnetic_field()->read_vector3(magneticBytes.data()) ==
            Vector3{4.0f, 5.0f, 6.0f});
}

TEST_CASE("Attitude layout exposes a quaternion control") {
    LayoutRegistry registry;
    register_default_layouts(registry);
    DeviceBuilder builder(registry);

    auto attitudeDevice = builder.build("Attitude", "", 4, description("Attitude"));
    REQUIRE(attitudeDevice != nullptr);
    auto* attitude = static_cast<Attitude*>(attitudeDevice.get());
    REQUIRE(attitude->attitude() != nullptr);
    REQUIRE(attitude->state_size_bytes() == 16);

    AttitudeState state{{0.0f, 0.0f, 0.70710678f, 0.70710678f}};
    std::vector<std::uint8_t> bytes(attitude->state_size_bytes());
    attitude->write_value_to_state(bytes.data(), &state);
    const Quaternion value = attitude->attitude()->read_quaternion(bytes.data());
    REQUIRE(value.x == 0.0f);
    REQUIRE(value.y == 0.0f);
    REQUIRE(value.z == Catch::Approx(0.70710678f));
    REQUIRE(value.w == Catch::Approx(0.70710678f));
}

TEST_CASE("Touchscreen exposes ten touches and per-touch controls") {
    LayoutRegistry registry;
    register_default_layouts(registry);
    DeviceBuilder builder(registry);

    auto touchDevice = builder.build("Touchscreen", "", 5, description("Touchscreen"));
    REQUIRE(touchDevice != nullptr);
    auto* touchscreen = static_cast<Touchscreen*>(touchDevice.get());
    REQUIRE(touchscreen->touch_count() == kMaxTouches);
    REQUIRE(touchscreen->state_size_bytes() == sizeof(TouchscreenState));
    REQUIRE(touchscreen->touch(0) != nullptr);
    REQUIRE(touchscreen->touch(9) != nullptr);
    REQUIRE(touchscreen->touch(10) == nullptr);

    TouchscreenState state{};
    state.touches[0].position = {100.0f, 200.0f};
    state.touches[0].delta = {1.0f, 2.0f};
    state.touches[0].startPosition = {90.0f, 180.0f};
    state.touches[0].phase = static_cast<float>(TouchPhase::Moved);
    state.touches[0].tapCount = 1.0f;
    state.touches[0].press = 1.0f;
    state.touches[9].position = {640.0f, 360.0f};
    state.touches[9].press = 1.0f;

    std::vector<std::uint8_t> bytes(touchscreen->state_size_bytes());
    touchscreen->write_value_to_state(bytes.data(), &state);

    Control* touch0 = touchscreen->touch(0);
    REQUIRE(touch0 != nullptr);
    REQUIRE(touchscreen->find_control("touch0/position") ==
            find_control(*touchscreen, "Touchscreen/touch0/position"));
    REQUIRE(find_control(*touchscreen, "Touchscreen/touch0/position")->read_vector2(bytes.data()) ==
            Vector2{100.0f, 200.0f});
    REQUIRE(find_control(*touchscreen, "Touchscreen/touch0/delta")->read_vector2(bytes.data()) ==
            Vector2{1.0f, 2.0f});
    REQUIRE(find_control(*touchscreen, "Touchscreen/touch0/startPosition")->read_vector2(bytes.data()) ==
            Vector2{90.0f, 180.0f});
    REQUIRE(find_control(*touchscreen, "Touchscreen/touch0/phase")->read_float(bytes.data()) ==
            static_cast<float>(TouchPhase::Moved));
    REQUIRE(find_control(*touchscreen, "Touchscreen/touch0/tapCount")->read_float(bytes.data()) == 1.0f);
    REQUIRE(find_control(*touchscreen, "Touchscreen/touch0/press")->read_float(bytes.data()) == 1.0f);
    REQUIRE(find_control(*touchscreen, "Touchscreen/touch9/position")->read_vector2(bytes.data()) ==
            Vector2{640.0f, 360.0f});
}

TEST_CASE("Sensor and touch state events drive value actions") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);

    auto* gyro = static_cast<Gyroscope*>(
        manager.create_device("Gyroscope", description("Gyroscope")));
    auto* touchscreen = static_cast<Touchscreen*>(
        manager.create_device("Touchscreen", description("Touchscreen")));
    REQUIRE(gyro != nullptr);
    REQUIRE(touchscreen != nullptr);

    ActionAsset asset;
    ActionMap& map = asset.add_map("Sensors");
    ActionDef& look = map.add_action("Look", ActionType::Value);
    look.expectedControlType = "Vector3";
    map.add_binding("Look", "<Gyroscope>/angularVelocity");
    ActionDef& point = map.add_action("Point", ActionType::Value);
    point.expectedControlType = "Vector2";
    map.add_binding("Point", "<Touchscreen>/touch0/position");
    asset.enable(manager);

    ActionDef* lookAction = asset.find_action("Look");
    ActionDef* pointAction = asset.find_action("Point");
    REQUIRE(lookAction != nullptr);
    REQUIRE(pointAction != nullptr);

    GyroscopeState gyroState{{1.0f, 2.0f, 3.0f}};
    auto gyroEvent = low::make_state_event(kFormatGyroscope, &gyroState, sizeof(gyroState),
                                           gyro->deviceId, 1.0);
    manager.queue_event(reinterpret_cast<const low::Event*>(gyroEvent.data()));
    manager.update(UpdateType::Dynamic);

    REQUIRE(lookAction->state->phase(*lookAction) == ActionPhase::Started);
    REQUIRE(lookAction->state->read_value<Vector3>(*lookAction) ==
            Vector3{1.0f, 2.0f, 3.0f});

    TouchscreenState touchState{};
    touchState.touches[0].position = {320.0f, 240.0f};
    touchState.touches[0].press = 1.0f;
    auto touchEvent = low::make_state_event(kFormatTouchscreen, &touchState,
                                            sizeof(touchState), touchscreen->deviceId,
                                            1.1);
    manager.queue_event(reinterpret_cast<const low::Event*>(touchEvent.data()));
    manager.update(UpdateType::Dynamic);

    REQUIRE(pointAction->state->phase(*pointAction) == ActionPhase::Started);
    REQUIRE(pointAction->state->read_value<Vector2>(*pointAction) ==
            Vector2{320.0f, 240.0f});
}



