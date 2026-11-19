#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <memory>

#include "insys/device.h"
#include "insys/device_builder.h"
#include "insys/device_states.h"
#include "insys/layout_registry.h"

using namespace insys;

namespace {

DeviceDescription description() {
    DeviceDescription value;
    value.interfaceName = "Test";
    value.product = "Test Device";
    return value;
}

std::unique_ptr<Device> build(LayoutRegistry& registry, const char* layout,
                              int id = 1) {
    DeviceBuilder builder(registry);
    return builder.build(layout, "", id, description());
}

} // namespace

TEST_CASE("Scalar sensor layouts expose one axis control") {
    LayoutRegistry registry;
    register_default_layouts(registry);
    DeviceBuilder builder(registry);

    struct Spec {
        const char* layout;
        const char* control;
        std::uint32_t size;
    };
    const Spec specs[] = {
        {"Light", "lightLevel", 4},
        {"Pressure", "atmosphericPressure", 4},
        {"Proximity", "distance", 4},
        {"Humidity", "relativeHumidity", 4},
        {"AmbientTemperature", "ambientTemperature", 4},
        {"HingeAngle", "angle", 4},
    };

    std::uint32_t nextId = 1;
    for (const Spec& spec : specs) {
        auto device = builder.build(spec.layout, "", nextId++, description());
        REQUIRE(device != nullptr);
        auto* control = dynamic_cast<AxisControl*>(device->find_control(spec.control));
        REQUIRE(control != nullptr);
        REQUIRE(device->state_size_bytes() == spec.size);
        REQUIRE(control->stateBlock.byteOffset == 0);
    }
}

TEST_CASE("StepCounter uses an integer control") {
    LayoutRegistry registry;
    register_default_layouts(registry);
    auto device = build(registry, "StepCounter");
    REQUIRE(device != nullptr);
    auto* stepCounter = static_cast<StepCounter*>(device.get());
    REQUIRE(stepCounter->step_counter() != nullptr);
    REQUIRE(dynamic_cast<IntegerControl*>(stepCounter->step_counter()) != nullptr);
    REQUIRE(stepCounter->state_size_bytes() == 4);

    StepCounterState state{777};
    std::uint8_t bytes[4]{};
    std::memcpy(bytes, &state, sizeof(state));
    REQUIRE(stepCounter->step_counter()->read_integer(bytes) == 777);
    REQUIRE(stepCounter->step_counter()->read_float(bytes) == 777.0f);
}

TEST_CASE("Pen layout exposes pressure, tilt, twist and buttons") {
    LayoutRegistry registry;
    register_default_layouts(registry);
    auto device = build(registry, "Pen");
    REQUIRE(device != nullptr);
    auto* pen = static_cast<Pen*>(device.get());

    REQUIRE(pen->position() != nullptr);
    REQUIRE(pen->delta() != nullptr);
    REQUIRE(dynamic_cast<DeltaControl*>(pen->delta()) != nullptr);
    REQUIRE(pen->tilt() != nullptr);
    REQUIRE(pen->pressure() != nullptr);
    REQUIRE(pen->twist() != nullptr);
    REQUIRE(pen->tip() != nullptr);
    REQUIRE(pen->eraser() != nullptr);
    REQUIRE(pen->in_range() != nullptr);
    REQUIRE(pen->barrel_button(1) != nullptr);
    REQUIRE(pen->barrel_button(4) != nullptr);
    REQUIRE(pen->barrel_button(0) == nullptr);
    REQUIRE(pen->state_size_bytes() == 36);

    PenState state{};
    state.position = {100.0f, 200.0f};
    state.delta = {2.0f, -3.0f};
    state.tilt = {0.25f, -0.5f};
    state.pressure = 0.75f;
    state.twist = 0.125f;
    state.buttons = 1u << static_cast<unsigned>(PenButton::Tip);
    state.buttons |= 1u << static_cast<unsigned>(PenButton::BarrelFirst);
    state.displayIndex = 2;

    std::uint8_t bytes[36]{};
    std::memcpy(bytes, &state, sizeof(state));

    REQUIRE(pen->position()->read_vector2(bytes) == state.position);
    REQUIRE(pen->delta()->read_vector2(bytes) == state.delta);
    REQUIRE(pen->tilt()->read_vector2(bytes) == state.tilt);
    REQUIRE(pen->pressure()->read_float(bytes) == 0.75f);
    REQUIRE(pen->twist()->read_float(bytes) == 0.125f);
    REQUIRE(pen->tip()->read_float(bytes) == 1.0f);
    REQUIRE(pen->eraser()->read_float(bytes) == 0.0f);
    REQUIRE(pen->barrel_button(1)->read_float(bytes) == 1.0f);
    REQUIRE(pen->find_control("displayIndex")->read_integer(bytes) == 2);
}

TEST_CASE("TrackedDevice layout exposes tracking state and pose") {
    LayoutRegistry registry;
    register_default_layouts(registry);
    auto device = build(registry, "TrackedDevice");
    REQUIRE(device != nullptr);
    auto* tracked = static_cast<TrackedDevice*>(device.get());

    REQUIRE(tracked->tracking_state() != nullptr);
    REQUIRE(tracked->is_tracked() != nullptr);
    REQUIRE(tracked->device_position() != nullptr);
    REQUIRE(tracked->device_rotation() != nullptr);
    REQUIRE(tracked->state_size_bytes() == 36);

    TrackedDeviceState state{};
    state.trackingState = 3;
    state.buttons = 1;
    state.devicePosition = {1.0f, 2.0f, 3.0f};
    state.deviceRotation = {0.1f, 0.2f, 0.3f, 0.9f};

    std::uint8_t bytes[36]{};
    std::memcpy(bytes, &state, sizeof(state));

    REQUIRE(tracked->tracking_state()->read_integer(bytes) == 3);
    REQUIRE(tracked->is_tracked()->read_float(bytes) == 1.0f);
    REQUIRE(tracked->device_position()->read_vector3(bytes) ==
            Vector3{1.0f, 2.0f, 3.0f});
    const Quaternion rotation = tracked->device_rotation()->read_quaternion(bytes);
    REQUIRE(rotation.x == 0.1f);
    REQUIRE(rotation.y == 0.2f);
    REQUIRE(rotation.z == 0.3f);
    REQUIRE(rotation.w == 0.9f);
}





