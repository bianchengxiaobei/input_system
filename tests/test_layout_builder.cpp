#include <catch2/catch_test_macros.hpp>

#include "insys/control_layout.h"
#include "insys/device_builder.h"
#include "insys/layout_registry.h"
#include "insys/low/state_format.h"

using namespace insys;

namespace {

ControlItem make_axis_item(std::string name, std::string layout = "Axis") {
    ControlItem item;
    item.name = std::move(name);
    item.layout = std::move(layout);
    item.format = kFormatFloat;
    return item;
}

ControlItem make_bit_item(std::string name, uint32_t bit) {
    ControlItem item;
    item.name = std::move(name);
    item.layout = "Button";
    item.format = kFormatBit;
    item.sizeInBits = 1;
    item.bit = bit;
    return item;
}

std::unique_ptr<Layout> make_bit_and_float_layout() {
    auto layout = std::make_unique<Layout>();
    layout->name = "TestBits";
    layout->stateFormat = FourCC('T', 'B', 'I', 'T');
    layout->controls.push_back(make_bit_item("a", 0));
    layout->controls.push_back(make_bit_item("b", 1));
    layout->controls.push_back(make_axis_item("floatValue"));
    ControlItem vector = make_axis_item("vec", "Vector2");
    vector.format = kFormatVector2;
    layout->controls.push_back(vector);
    return layout;
}

DeviceDescription test_description() {
    DeviceDescription description;
    description.interfaceName = "Test";
    description.product = "Test Device";
    return description;
}

} // namespace

TEST_CASE("Layout merge inherits base controls and overrides fields") {
    Layout base;
    base.name = "Base";
    base.controls.push_back(make_axis_item("axis", "Axis"));
    base.controls.push_back(make_bit_item("button", 0));

    Layout child;
    child.name = "Child";

    ControlItem overrideAxis = make_axis_item("axis", "Button");
    overrideAxis.sizeInBits = 1;
    overrideAxis.bit = 3;
    child.controls.push_back(overrideAxis);
    child.controls.push_back(make_axis_item("extra"));

    child.merge_from(base);

    REQUIRE(child.controls.size() == 3);
    REQUIRE(child.controls[0].name == "axis");
    REQUIRE(child.controls[0].layout == "Button");
    REQUIRE(child.controls[0].sizeInBits == 1);
    REQUIRE(child.controls[0].bit == 3);
    REQUIRE(child.controls[1].name == "button");
    REQUIRE(child.controls[2].name == "extra");
    REQUIRE(child.baseLayout == &base);
}

TEST_CASE("Layout registry resolves inheritance by name") {
    LayoutRegistry registry;

    auto base = std::make_unique<Layout>();
    base->name = "TestBase";
    base->controls.push_back(make_axis_item("baseAxis"));
    registry.add(std::move(base));

    auto child = std::make_unique<Layout>();
    child->name = "TestChild";
    child->extends = "TestBase";
    child->controls.push_back(make_axis_item("childAxis"));
    registry.add(std::move(child));

    Layout* resolved = registry.find("testchild");
    REQUIRE(resolved != nullptr);
    REQUIRE(resolved->controls.size() == 2);
    REQUIRE(resolved->controls[0].name == "baseAxis");
    REQUIRE(resolved->controls[1].name == "childAxis");
}

TEST_CASE("DeviceBuilder computes bitfields then aligns floats") {
    LayoutRegistry registry;
    registry.add(make_bit_and_float_layout());

    DeviceBuilder builder(registry);
    auto device = builder.build("TestBits", "", 7, test_description());
    REQUIRE(device != nullptr);

    Control* a = device->find_control("a");
    Control* b = device->find_control("b");
    Control* floatValue = device->find_control("floatValue");
    Control* vector = device->find_control("vec");

    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(floatValue != nullptr);
    REQUIRE(vector != nullptr);

    REQUIRE(a->stateBlock.byteOffset == 0);
    REQUIRE(a->stateBlock.bitOffset == 0);
    REQUIRE(b->stateBlock.byteOffset == 0);
    REQUIRE(b->stateBlock.bitOffset == 1);
    REQUIRE(floatValue->stateBlock.byteOffset == 4);
    REQUIRE(vector->stateBlock.byteOffset == 8);
    REQUIRE(device->stateBlock.aligned_size_in_bytes() == 16);
}

TEST_CASE("DeviceBuilder applies useStateFrom") {
    LayoutRegistry registry;

    auto layout = std::make_unique<Layout>();
    layout->name = "TestUseStateFrom";
    layout->stateFormat = FourCC('T', 'U', 'S', 'F');
    layout->controls.push_back(make_axis_item("source"));

    ControlItem alias = make_axis_item("alias");
    alias.useStateFrom = "source";
    layout->controls.push_back(alias);

    registry.add(std::move(layout));

    DeviceBuilder builder(registry);
    auto device = builder.build("TestUseStateFrom", "", 1, test_description());
    REQUIRE(device != nullptr);

    Control* source = device->find_control("source");
    Control* aliasControl = device->find_control("alias");
    REQUIRE(source != nullptr);
    REQUIRE(aliasControl != nullptr);
    REQUIRE(aliasControl->stateBlock.byteOffset == source->stateBlock.byteOffset);
    REQUIRE(aliasControl->stateBlock.sizeInBits == source->stateBlock.sizeInBits);
}

TEST_CASE("DeviceBuilder computes keyboard bit layout") {
    LayoutRegistry registry;
    register_default_layouts(registry);

    DeviceBuilder builder(registry);
    auto device = builder.build("Keyboard", "", 3, test_description());
    REQUIRE(device != nullptr);

    auto* keyboard = static_cast<Keyboard*>(device.get());
    REQUIRE(keyboard->key_control(Key::A) != nullptr);
    REQUIRE(keyboard->key_control(Key::A)->stateBlock.bitOffset ==
            static_cast<uint32_t>(Key::A));
    REQUIRE(keyboard->stateBlock.aligned_size_in_bytes() == (kKeyCount + 8) / 8);
}
