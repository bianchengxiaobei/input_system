#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "insys/device_builder.h"
#include "insys/hid/hid_descriptor.h"
#include "insys/hid/hid_layout.h"
#include "insys/layout_registry.h"

using namespace insys;
using namespace insys::hid;

namespace {

constexpr std::array<uint8_t, 40> kGamepadDescriptor = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Gamepad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        // Report ID (1)
    0x09, 0x30,        // Usage (X)
    0x09, 0x31,        // Usage (Y)
    0x15, 0x00,        // Logical Minimum (0)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x02,        // Report Count (2)
    0x81, 0x02,        // Input
    0x05, 0x09,        // Usage Page (Button)
    0x19, 0x01,        // Usage Minimum (1)
    0x29, 0x02,        // Usage Maximum (2)
    0x15, 0x00,        // Logical Minimum (0)
    0x25, 0x01,        // Logical Maximum (1)
    0x75, 0x01,        // Report Size (1)
    0x95, 0x02,        // Report Count (2)
    0x81, 0x02,        // Input
    0xC0,              // End Collection
};

std::unique_ptr<Device> build_device_from_descriptor(const uint8_t* data,
                                                     std::size_t size,
                                                     std::string_view name) {
    DeviceDescriptor descriptor;
    if (!parse_report_descriptor(data, size, descriptor))
        return nullptr;
    const ReportInfo* report = descriptor.primary_input_report();
    if (report == nullptr)
        return nullptr;

    LayoutRegistry registry;
    registry.add(build_generic_layout(descriptor, *report, name));
    DeviceBuilder builder(registry);
    return builder.build(name, "", 1, {});
}

} // namespace

TEST_CASE("HID descriptor parser reads usages, offsets and report sizes") {
    DeviceDescriptor descriptor;
    REQUIRE(parse_report_descriptor(kGamepadDescriptor.data(),
                                    kGamepadDescriptor.size(), descriptor));

    REQUIRE(descriptor.usagePage == UsagePage::GenericDesktop);
    REQUIRE(descriptor.usage == 0x05);
    REQUIRE(descriptor.collections.size() == 1);
    REQUIRE(descriptor.collections[0].type == CollectionType::Application);
    REQUIRE(descriptor.collections[0].parent == -1);
    REQUIRE(descriptor.collections[0].childCount == 4);

    REQUIRE(descriptor.elements.size() == 4);
    REQUIRE(descriptor.elements[0].usagePage == UsagePage::GenericDesktop);
    REQUIRE(descriptor.elements[0].usage == 0x30);
    REQUIRE(descriptor.elements[0].reportOffsetInBits == 8);
    REQUIRE(descriptor.elements[1].usage == 0x31);
    REQUIRE(descriptor.elements[1].reportOffsetInBits == 16);
    REQUIRE(descriptor.elements[2].usagePage == UsagePage::Button);
    REQUIRE(descriptor.elements[2].usage == 1);
    REQUIRE(descriptor.elements[2].reportOffsetInBits == 24);
    REQUIRE(descriptor.elements[3].usage == 2);
    REQUIRE(descriptor.elements[3].reportOffsetInBits == 25);

    REQUIRE(descriptor.reports.size() == 1);
    REQUIRE(descriptor.reports[0].id == 1);
    REQUIRE(descriptor.reports[0].hasId);
    REQUIRE(descriptor.reports[0].sizeInBits == 26);
    REQUIRE(descriptor.inputReportSize == 4);
    REQUIRE(descriptor.primary_input_report() == &descriptor.reports[0]);
}

TEST_CASE("HID descriptor parser rejects malformed collections") {
    const uint8_t malformed[] = {0xC0};
    DeviceDescriptor descriptor;
    REQUIRE_FALSE(parse_report_descriptor(malformed, sizeof(malformed), descriptor));
    REQUIRE_FALSE(parse_report_descriptor(nullptr, 0, descriptor));
}

TEST_CASE("Generic HID layout exposes normalized axes and buttons") {
    DeviceDescriptor descriptor;
    REQUIRE(parse_report_descriptor(kGamepadDescriptor.data(),
                                    kGamepadDescriptor.size(), descriptor));
    const ReportInfo* report = descriptor.primary_input_report();
    REQUIRE(report != nullptr);

    LayoutRegistry registry;
    registry.add(build_generic_layout(descriptor, *report, "HID Test"));
    DeviceBuilder builder(registry);
    auto device = builder.build("HID Test", "", 1, {});
    REQUIRE(device != nullptr);
    REQUIRE(device->state_size_bytes() == 4);
    REQUIRE(dynamic_cast<Gamepad*>(device.get()) != nullptr);
    REQUIRE(device->find_control("leftStick") != nullptr);
    REQUIRE(device->find_control("buttonSouth") != nullptr);
    REQUIRE(device->find_control("buttonEast") != nullptr);

    Control* x = device->find_control("x");
    Control* y = device->find_control("y");
    Control* trigger = device->find_control("trigger");
    Control* button2 = device->find_control("button2");
    REQUIRE(x != nullptr);
    REQUIRE(y != nullptr);
    REQUIRE(trigger != nullptr);
    REQUIRE(button2 != nullptr);
    REQUIRE(x->stateBlock.byteOffset == 1);
    REQUIRE(y->stateBlock.byteOffset == 2);
    REQUIRE(trigger->stateBlock.byteOffset == 3);
    REQUIRE(trigger->stateBlock.bitOffset == 0);

    std::array<uint8_t, 4> state = {1, 255, 0, 3};
    REQUIRE(x->read_float(state.data()) == 1.0f);
    REQUIRE(y->read_float(state.data()) == 1.0f);
    REQUIRE(trigger->read_float(state.data()) == 1.0f);
    REQUIRE(button2->read_float(state.data()) == 1.0f);
}


TEST_CASE("HID descriptor parser supports reports without report ids") {
    const uint8_t descriptorBytes[] = {
        0x05, 0x01, 0x09, 0x04, 0xA1, 0x01,
        0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x00,
        0x75, 0x08, 0x95, 0x02, 0x81, 0x02, 0xC0,
    };

    DeviceDescriptor descriptor;
    REQUIRE(parse_report_descriptor(descriptorBytes, sizeof(descriptorBytes),
                                    descriptor));
    REQUIRE(descriptor.reports.size() == 1);
    REQUIRE_FALSE(descriptor.reports[0].hasId);
    REQUIRE(descriptor.reports[0].sizeInBits == 16);
    REQUIRE(descriptor.inputReportSize == 2);
    REQUIRE(descriptor.elements[0].reportOffsetInBits == 0);
    REQUIRE(descriptor.elements[1].reportOffsetInBits == 8);
}


TEST_CASE("Generic HID gamepad maps hat to a semantic dpad") {
    const uint8_t descriptorBytes[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
        0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x00,
        0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
        0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x75, 0x08, 0x95, 0x01,
        0x81, 0x02,
        0x05, 0x09, 0x19, 0x01, 0x29, 0x01, 0x15, 0x00, 0x25, 0x01,
        0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
        0xC0,
    };

    DeviceDescriptor descriptor;
    REQUIRE(parse_report_descriptor(descriptorBytes, sizeof(descriptorBytes),
                                    descriptor));
    const ReportInfo* report = descriptor.primary_input_report();
    REQUIRE(report != nullptr);

    LayoutRegistry registry;
    registry.add(build_generic_layout(descriptor, *report, "HID Dpad"));
    DeviceBuilder builder(registry);
    auto device = builder.build("HID Dpad", "", 1, {});
    REQUIRE(device != nullptr);
    Control* dpad = device->find_control("dpad");
    REQUIRE(dpad != nullptr);
    std::array<uint8_t, 4> state = {0, 128, 2, 0};
    const Vector2 direction = dpad->read_vector2(state.data());
    REQUIRE(direction.x == 1.0f);
    REQUIRE(direction.y == 0.0f);
}

TEST_CASE("Generic HID maps DS4-style axes before buttons as sticks") {
    const uint8_t descriptorBytes[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01,
        0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
        0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x04,
        0x81, 0x02,
        0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00,
        0x46, 0x3B, 0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01,
        0x81, 0x42, 0x65, 0x00,
        0x05, 0x09, 0x19, 0x01, 0x29, 0x0E, 0x15, 0x00,
        0x25, 0x01, 0x75, 0x01, 0x95, 0x0E, 0x81, 0x02,
        0x06, 0x00, 0xFF, 0x09, 0x20, 0x15, 0x00, 0x25, 0x7F,
        0x75, 0x06, 0x95, 0x01, 0x81, 0x02,
        0x05, 0x01, 0x09, 0x33, 0x09, 0x34, 0x15, 0x00,
        0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
        0xC0,
    };

    auto device = build_device_from_descriptor(
        descriptorBytes, sizeof(descriptorBytes), "HID DS4 Style");
    REQUIRE(device != nullptr);
    REQUIRE(device->state_size_bytes() == 10);

    Control* rightX = device->find_control("rightStick/x");
    Control* rightY = device->find_control("rightStick/y");
    Control* leftTrigger = device->find_control("leftTrigger");
    Control* rightTrigger = device->find_control("rightTrigger");
    REQUIRE(rightX != nullptr);
    REQUIRE(rightY != nullptr);
    REQUIRE(leftTrigger != nullptr);
    REQUIRE(rightTrigger != nullptr);
    REQUIRE(rightX->stateBlock.byteOffset == 3);
    REQUIRE(rightY->stateBlock.byteOffset == 4);
    REQUIRE(leftTrigger->stateBlock.byteOffset == 8);
    REQUIRE(rightTrigger->stateBlock.byteOffset == 9);

    std::array<uint8_t, 10> state{};
    state[8] = 0;
    state[9] = 255;
    REQUIRE(leftTrigger->read_float(state.data()) == 0.0f);
    REQUIRE(rightTrigger->read_float(state.data()) == 1.0f);
}

TEST_CASE("Generic HID maps unsigned Z and Rz without Rx or Ry as triggers") {
    const uint8_t descriptorBytes[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
        0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
        0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x04,
        0x81, 0x02,
        0xC0,
    };

    auto device = build_device_from_descriptor(
        descriptorBytes, sizeof(descriptorBytes), "HID Trigger Pair");
    REQUIRE(device != nullptr);
    REQUIRE(device->find_control("rightStick") == nullptr);
    REQUIRE(device->find_control("leftTrigger") != nullptr);
    REQUIRE(device->find_control("rightTrigger") != nullptr);
}
