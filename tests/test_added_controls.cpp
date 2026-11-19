#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <memory>

#include "insys/control_layout.h"
#include "insys/device.h"
#include "insys/device_builder.h"
#include "insys/layout_registry.h"
#include "insys/low/state_format.h"

using namespace insys;

namespace {

DeviceDescription description() {
    DeviceDescription value;
    value.interfaceName = "Test";
    value.product = "Test Device";
    return value;
}

ControlItem make_item(std::string name, std::string layout, FourCC format,
                      uint32_t offset, uint32_t sizeInBits) {
    ControlItem item;
    item.name = std::move(name);
    item.layout = std::move(layout);
    item.format = format;
    item.offset = offset;
    item.sizeInBits = sizeInBits;
    return item;
}

} // namespace

TEST_CASE("Integer, Delta and Double controls round-trip state") {
    LayoutRegistry registry;
    auto layout = std::make_unique<Layout>();
    layout->name = "NumericControls";
    layout->stateFormat = FourCC('N', 'U', 'M', 'C');
    layout->controls.push_back(
        make_item("integerValue", "Integer", kFormatInt, 0, 32));
    layout->controls.push_back(
        make_item("deltaValue", "Delta", kFormatVector2, 4, 64));
    layout->controls.push_back(
        make_item("doubleValue", "Double", kFormatDouble, 16, 64));
    registry.add(std::move(layout));

    DeviceBuilder builder(registry);
    auto device = builder.build("NumericControls", "", 1, description());
    REQUIRE(device != nullptr);

    auto* integer = device->find_control("integerValue");
    auto* delta = device->find_control("deltaValue");
    auto* doubleValue = device->find_control("doubleValue");
    REQUIRE(dynamic_cast<IntegerControl*>(integer) != nullptr);
    REQUIRE(dynamic_cast<DeltaControl*>(delta) != nullptr);
    REQUIRE(dynamic_cast<DoubleControl*>(doubleValue) != nullptr);
    REQUIRE(device->state_size_bytes() == 24);

    uint8_t state[24]{};
    const std::int32_t integerValue = 123456;
    const Vector2 motion{3.0f, -4.0f};
    const double doubleData = 12.25;
    integer->write_value_to_state(state, &integerValue);
    delta->write_value_to_state(state, &motion);
    doubleValue->write_value_to_state(state, &doubleData);

    REQUIRE(integer->read_integer(state) == 123456);
    REQUIRE(delta->read_vector2(state) == motion);
    REQUIRE(doubleValue->read_double(state) == 12.25);
    REQUIRE(doubleValue->read_float(state) == 12.25f);
}

TEST_CASE("DiscreteButtonControl reads a multi-bit value range") {
    LayoutRegistry registry;
    auto layout = std::make_unique<Layout>();
    layout->name = "DiscreteTest";
    layout->stateFormat = FourCC('D', 'I', 'S', 'C');
    layout->stateSizeInBits = 8;

    ControlItem item = make_item("up", "DiscreteButton", kFormatBit, 0, 4);
    item.parameters.push_back({"minValue", PrimitiveValue::from_int(7)});
    item.parameters.push_back({"maxValue", PrimitiveValue::from_int(1)});
    item.parameters.push_back({"wrapAtValue", PrimitiveValue::from_int(7)});
    item.parameters.push_back({"nullValue", PrimitiveValue::from_int(8)});
    layout->controls.push_back(std::move(item));
    registry.add(std::move(layout));

    DeviceBuilder builder(registry);
    auto device = builder.build("DiscreteTest", "", 1, description());
    REQUIRE(device != nullptr);
    auto* up = dynamic_cast<DiscreteButtonControl*>(device->find_control("up"));
    REQUIRE(up != nullptr);
    REQUIRE(up->minValue == 7);
    REQUIRE(up->maxValue == 1);
    REQUIRE(up->wrapAtValue == 7);
    REQUIRE(up->nullValue == 8);

    uint8_t state[1]{};
    for (std::uint8_t value : {7u, 8u, 0u, 1u, 2u}) {
        state[0] = value;
        REQUIRE(up->read_float(state) ==
                (value == 7u || value == 0u || value == 1u ? 1.0f
                                                                         : 0.0f));
    }

    state[0] = 3;
    REQUIRE(up->read_float(state) == 0.0f);
}



