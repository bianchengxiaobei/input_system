#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "insys/backend/hid_backend.h"
#include "insys/backend/runtime.h"
#include "insys/device.h"
#include "insys/device_commands.h"
#include "insys/device_states.h"
#include "insys/hid/hid_gamepad.h"
#include "insys/manager.h"

using namespace insys;
using namespace insys::backend;

namespace {

std::array<uint8_t, 40> generic_descriptor() {
    return {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01,
        0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF,
        0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02, 0x05,
        0x09, 0x19, 0x01, 0x29, 0x02, 0x15, 0x00, 0x25,
        0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0xC0,
    };
}

GamepadState current_gamepad(Device* device) {
    GamepadState state;
    std::memcpy(&state, device->current_state(), sizeof(state));
    return state;
}

} // namespace

TEST_CASE("HID backend creates a DualShock 4 device and decodes reports") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    HidBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    hid::DeviceInfo info;
    info.id = "pad1";
    info.vendorId = 0x054C;
    info.productId = 0x05C4;
    info.description.interfaceName = "HID";
    info.description.manufacturer = "Sony";
    info.description.product = "DualShock 4";
    info.outputReportSize = 32;
    REQUIRE(backend.add_device(info));
    REQUIRE(backend.connected("pad1"));
    REQUIRE(backend.kind("pad1") == hid::DeviceKind::DualShock4);
    REQUIRE(manager.devices().size() == 1);
    REQUIRE(manager.devices()[0]->layout.str() == "DualShock4GamepadHID");
    REQUIRE(backend.device_id("pad1") == manager.devices()[0]->deviceId);

    const std::array<uint8_t, 10> report = {
        0x01, 255, 128, 0, 128, 0x20, 0x00, 0x00, 255, 0,
    };
    REQUIRE(backend.feed_report("pad1", report.data(), report.size(), 1.0));
    REQUIRE_FALSE(backend.feed_report("pad1", report.data(), report.size(), 1.0));
    REQUIRE(backend.transport("pad1") == hid::HidTransport::Usb);
    backend.poll();
    manager.update(UpdateType::Dynamic);

    const GamepadState state = current_gamepad(manager.devices()[0]);
    REQUIRE(state.leftStick.x == 1.0f);
    REQUIRE(state.rightStick.x == -1.0f);
    REQUIRE((state.buttons & (1u << 6)) != 0);
    REQUIRE(state.leftTrigger == 1.0f);

    std::vector<uint8_t> output;
    std::string outputId;
    backend.set_output_report_handler(
        [&](std::string_view id, const uint8_t* data, std::size_t size) {
            outputId = std::string(id);
            output.assign(data, data + size);
            return true;
        });

    SendHapticImpulseCommand impulse;
    impulse.motorChannel =
        static_cast<int>(SendHapticImpulseCommand::Motor::LowFrequency);
    impulse.amplitude = 1.0f;
    DeviceCommand command{kSendHapticImpulseCommand, &impulse};
    REQUIRE(manager.send_device_command(manager.devices()[0], &command) == 1);
    REQUIRE(outputId == "pad1");
    REQUIRE(output.size() == 32);
    REQUIRE(output[0] == 0x05);
    REQUIRE(output[5] == 255);

    HapticCapabilities capabilities;
    GetHapticCapabilitiesCommand capabilitiesRequest{&capabilities};
    DeviceCommand capabilitiesCommand{kGetHapticCapabilitiesCommand,
                                     &capabilitiesRequest};
    REQUIRE(manager.send_device_command(manager.devices()[0],
                                        &capabilitiesCommand) == 1);
    REQUIRE(capabilities.supportsLowFrequency);
    REQUIRE(capabilities.supportsHighFrequency);
    REQUIRE(capabilities.supportsLightBar);

    SetLightBarColorCommand color{1.0f, 0.5f, 0.0f};
    DeviceCommand colorCommand{kSetLightBarColorCommand, &color};
    REQUIRE(manager.send_device_command(manager.devices()[0], &colorCommand) == 1);
    REQUIRE(output.size() == 32);
    REQUIRE(output[1] == 0x03);
    REQUIRE(output[6] == 255);
    REQUIRE(output[7] == 127);
    REQUIRE(output[8] == 0);

    const uint8_t raw[] = {0xAA, 0xBB};
    SendBufferedHapticsCommand buffered{raw, 2};
    DeviceCommand bufferedCommand{kSendBufferedHapticsCommand, &buffered};
    REQUIRE(manager.send_device_command(manager.devices()[0], &bufferedCommand) == 1);
    REQUIRE(output.size() == 2);
    REQUIRE(output[0] == 0xAA);
    REQUIRE(output[1] == 0xBB);

    REQUIRE(backend.remove_device("pad1"));
    REQUIRE_FALSE(backend.connected("pad1"));
    REQUIRE(manager.devices().empty());
    backend.shutdown();
}

TEST_CASE("HID backend builds a generic descriptor-driven device") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    HidBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    hid::DeviceInfo info;
    info.id = "raw1";
    info.vendorId = 0x1234;
    info.productId = 0x5678;
    info.description.product = "Raw HID";
    const auto descriptorBytes = generic_descriptor();
    info.reportDescriptor.assign(descriptorBytes.begin(), descriptorBytes.end());
    info.outputReportSize = 8;
    REQUIRE(backend.add_device(info));
    REQUIRE(manager.devices().size() == 1);
    Device* device = manager.devices()[0];
    REQUIRE(device->state_size_bytes() == 4);
    REQUIRE(device->find_control("x") != nullptr);
    REQUIRE(device->find_control("trigger") != nullptr);

    const std::array<uint8_t, 4> report = {0x01, 255, 0, 1};
    REQUIRE(backend.feed_report("raw1", report.data(), report.size(), 1.0));
    REQUIRE_FALSE(backend.feed_report("raw1", report.data(), report.size(), 1.0));
    const std::array<uint8_t, 4> wrongId = {0x02, 255, 0, 1};
    REQUIRE_FALSE(backend.feed_report("raw1", wrongId.data(), wrongId.size(), 2.0));
    backend.poll();
    manager.update(UpdateType::Dynamic);

    REQUIRE(device->find_control("x")->read_float(device->current_state()) == 1.0f);
    REQUIRE(device->find_control("trigger")->read_float(device->current_state()) ==
            1.0f);

    std::vector<uint8_t> output;
    backend.set_output_report_handler(
        [&](std::string_view, const uint8_t* data, std::size_t size) {
            output.assign(data, data + size);
            return true;
        });
    const uint8_t rawOutput[] = {0x03, 0x04};
    SendBufferedHapticsCommand buffered{rawOutput, 2};
    DeviceCommand command{kSendBufferedHapticsCommand, &buffered};
    REQUIRE(manager.send_device_command(device, &command) == 1);
    REQUIRE(output.size() == 2);
    REQUIRE(output[0] == 0x03);

    SendHapticImpulseCommand impulse;
    DeviceCommand impulseCommand{kSendHapticImpulseCommand, &impulse};
    REQUIRE(manager.send_device_command(device, &impulseCommand) == -1);
    backend.shutdown();
}

TEST_CASE("HID backend routes raw output, feature write and feature read") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    HidBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    hid::DeviceInfo info;
    info.id = "raw-reports";
    info.vendorId = 0x054C;
    info.productId = 0x05C4;
    info.description.product = "DualShock 4";
    REQUIRE(backend.add_device(info));
    Device* device = manager.devices()[0];

    std::vector<uint8_t> output;
    std::vector<uint8_t> feature;
    backend.set_output_report_handler(
        [&](std::string_view, const uint8_t* data, std::size_t size) {
            output.assign(data, data + size);
            return true;
        });
    backend.set_feature_report_handler(
        [&](std::string_view, const uint8_t* data, std::size_t size) {
            feature.assign(data, data + size);
            return true;
        });

    const uint8_t payload[] = {0xAA, 0xBB};
    SendHidReportCommand outputRequest;
    outputRequest.reportId = 0x05;
    outputRequest.prependReportId = true;
    outputRequest.data = payload;
    outputRequest.size = sizeof(payload);
    DeviceCommand outputCommand{kSendHidOutputReportCommand, &outputRequest};
    REQUIRE(manager.send_device_command(device, &outputCommand) == 1);
    REQUIRE(output.size() == 3);
    REQUIRE(output[0] == 0x05);
    REQUIRE(output[1] == 0xAA);
    REQUIRE(output[2] == 0xBB);

    outputRequest.prependReportId = false;
    REQUIRE(manager.send_device_command(device, &outputCommand) == 1);
    REQUIRE(output.size() == 2);
    REQUIRE(output[0] == 0xAA);

    SendHidReportCommand featureRequest;
    featureRequest.reportId = 0x04;
    featureRequest.prependReportId = true;
    featureRequest.data = payload;
    featureRequest.size = sizeof(payload);
    DeviceCommand featureCommand{kSendHidFeatureReportCommand, &featureRequest};
    REQUIRE(manager.send_device_command(device, &featureCommand) == 1);
    REQUIRE(feature.size() == 3);
    REQUIRE(feature[0] == 0x04);
    REQUIRE(feature[2] == 0xBB);

    backend.set_feature_report_reader(
        [](std::string_view, uint8_t reportId, uint8_t* data,
           std::size_t size) -> int {
            REQUIRE(size >= 4);
            data[0] = reportId;
            data[1] = 0x11;
            data[2] = 0x22;
            data[3] = 0x33;
            return 4;
        });
    std::array<uint8_t, 16> readBuffer{};
    uint32_t readSize = 0;
    GetHidFeatureReportCommand readRequest;
    readRequest.reportId = 0x04;
    readRequest.data = readBuffer.data();
    readRequest.capacity = readBuffer.size();
    readRequest.reportSize = &readSize;
    DeviceCommand readCommand{kGetHidFeatureReportCommand, &readRequest};
    REQUIRE(manager.send_device_command(device, &readCommand) == 4);
    REQUIRE(readSize == 4);
    REQUIRE(readBuffer[0] == 0x04);
    REQUIRE(readBuffer[3] == 0x33);

    std::vector<HidError> errors;
    backend.set_error_handler(
        [&](const HidError& error) { errors.push_back(error); });
    backend.set_output_report_handler(
        [](std::string_view, const uint8_t*, std::size_t) { return false; });
    REQUIRE_FALSE(backend.send_output_report("raw-reports", payload,
                                             sizeof(payload)));
    REQUIRE(errors.size() == 1);
    REQUIRE(errors[0].code == HidErrorCode::WriteFailed);
    REQUIRE(errors[0].deviceId == "raw-reports");

    backend.set_output_report_handler({});
    backend.set_feature_report_handler({});
    backend.set_feature_report_reader({});
    backend.shutdown();
}

TEST_CASE("HID backend deduplicates serials and exposes transport") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    HidBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    hid::DeviceInfo first;
    first.id = "serial-a";
    first.vendorId = 0x054C;
    first.productId = 0x05C4;
    first.usagePage = 0x01;
    first.usage = 0x05;
    first.transport = hid::HidTransport::Bluetooth;
    first.description.serial = "SERIAL-1";
    REQUIRE(backend.add_device(first));
    REQUIRE(backend.transport("serial-a") == hid::HidTransport::Bluetooth);

    std::vector<HidError> errors;
    backend.set_error_handler(
        [&](const HidError& error) { errors.push_back(error); });

    hid::DeviceInfo duplicate = first;
    duplicate.id = "serial-b";
    REQUIRE_FALSE(backend.add_device(duplicate));
    REQUIRE(manager.devices().size() == 1);
    REQUIRE(errors.size() == 1);
    REQUIRE(errors[0].code == HidErrorCode::DuplicateDevice);

    hid::DeviceInfo otherUsage = first;
    otherUsage.id = "serial-c";
    otherUsage.usage = 0x04;
    REQUIRE(backend.add_device(otherUsage));
    REQUIRE(manager.devices().size() == 2);

    backend.shutdown();
}




TEST_CASE("HID backend drives the Switch Pro handshake sequence") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    HidBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    hid::DeviceInfo info;
    info.id = "switch";
    info.vendorId = 0x057E;
    info.productId = 0x2009;
    info.description.product = "Switch Pro Controller";
    REQUIRE(backend.add_device(info));

    std::vector<uint8_t> lastReport;
    backend.set_output_report_handler(
        [&](std::string_view, const uint8_t* data, std::size_t size) {
            lastReport.assign(data, data + size);
            return true;
        });

    backend.poll(0.0);
    REQUIRE(lastReport.size() == 49);
    REQUIRE(lastReport[0] == 0x80);
    REQUIRE(lastReport[1] == 0x01);

    backend.poll(0.09);
    REQUIRE(lastReport[1] == 0x01);
    backend.poll(0.11);
    REQUIRE(lastReport[1] == 0x02);
    backend.poll(0.21);
    REQUIRE(lastReport[1] == 0x03);
    backend.poll(0.31);
    REQUIRE(lastReport[1] == 0x02);
    backend.poll(0.41);
    REQUIRE(lastReport[1] == 0x04);

    lastReport.clear();
    backend.poll(0.51);
    REQUIRE(lastReport.size() == 49);
    REQUIRE(lastReport[0] == 0x01);
    REQUIRE(lastReport[10] == 0x03);
    REQUIRE(lastReport[11] == 0x30);

    backend.poll(0.61);
    REQUIRE(lastReport.size() == 49);
    REQUIRE(lastReport[10] == 0x40);
    REQUIRE(lastReport[11] == 0x01);

    backend.poll(0.71);
    REQUIRE(lastReport.size() == 49);
    REQUIRE(lastReport[10] == 0x48);
    REQUIRE(lastReport[11] == 0x01);

    lastReport.clear();
    backend.poll(1.0);
    REQUIRE(lastReport.empty());

    backend.set_output_report_handler({});
    backend.shutdown();
}

TEST_CASE("HID backend falls back to USB Switch handshake reports") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    HidBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    hid::DeviceInfo info;
    info.id = "switch-usb";
    info.vendorId = 0x057E;
    info.productId = 0x2009;
    info.description.product = "Switch Pro Controller";
    REQUIRE(backend.add_device(info));

    std::size_t sizes[2] = {0, 0};
    int calls = 0;
    backend.set_output_report_handler(
        [&](std::string_view, const uint8_t* data, std::size_t size) {
            if (size == 64) {
                sizes[1] = size;
                REQUIRE(data[0] == 0x80);
                return true;
            }
            sizes[0] = size;
            ++calls;
            return false;
        });

    backend.poll(0.0);
    REQUIRE(calls == 1);
    REQUIRE(sizes[0] == 49);
    REQUIRE(sizes[1] == 64);
    backend.set_output_report_handler({});
    backend.shutdown();
}

TEST_CASE("HID backend decodes DualShock 3 and sends output haptics") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    HidBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    hid::DeviceInfo info;
    info.id = "ps3";
    info.vendorId = 0x054C;
    info.productId = 0x0268;
    info.description.product = "PLAYSTATION(R)3 Controller";
    REQUIRE(backend.add_device(info));
    REQUIRE(backend.kind("ps3") == hid::DeviceKind::DualShock3);
    REQUIRE(manager.devices().size() == 1);
    REQUIRE(manager.devices()[0]->layout.str() == "DualShock3GamepadHID");

    std::array<uint8_t, 32> report{};
    report[2] = 0x08;
    report[3] = 0x40;
    report[4] = 0x01;
    report[6] = 255;
    report[7] = 128;
    report[8] = 0;
    report[9] = 128;
    report[18] = 255;
    REQUIRE(backend.feed_report("ps3", report.data(), report.size()));
    backend.poll();
    manager.update(UpdateType::Dynamic);

    const GamepadState state = current_gamepad(manager.devices()[0]);
    REQUIRE(state.leftStick.x == 1.0f);
    REQUIRE((state.buttons & (1u << 12)) != 0);
    REQUIRE((state.buttons & (1u << 6)) != 0);
    REQUIRE(state.leftTrigger == 1.0f);

    HapticCapabilities capabilities;
    GetHapticCapabilitiesCommand request{&capabilities};
    DeviceCommand command{kGetHapticCapabilitiesCommand, &request};
    REQUIRE(manager.send_device_command(manager.devices()[0], &command) == 1);
    REQUIRE(capabilities.supportsLowFrequency);
    REQUIRE(capabilities.supportsHighFrequency);
    REQUIRE_FALSE(capabilities.supportsLightBar);

    std::vector<uint8_t> output;
    backend.set_output_report_handler(
        [&](std::string_view, const uint8_t* data, std::size_t size) {
            output.assign(data, data + size);
            return true;
        });

    SendHapticImpulseCommand impulse;
    impulse.amplitude = 1.0f;
    DeviceCommand impulseCommand{kSendHapticImpulseCommand, &impulse};
    REQUIRE(manager.send_device_command(manager.devices()[0], &impulseCommand) == 1);
    REQUIRE(output.size() == 49);
    REQUIRE(output[0] == 0x01);
    REQUIRE(output[4] == 255);

    backend.set_output_report_handler({});
    backend.shutdown();
}

TEST_CASE("Runtime combines added and legacy device command handlers") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    Gamepad* gamepad = static_cast<Gamepad*>(manager.create_device("Gamepad", {}));
    REQUIRE(gamepad != nullptr);

    int addedCalls = 0;
    int legacyCalls = 0;
    const int handlerId = runtime.add_device_command_handler(
        [&](int, DeviceCommand*) {
            ++addedCalls;
            return 5;
        });
    runtime.set_device_command_handler([&](int, DeviceCommand*) {
        ++legacyCalls;
        return 6;
    });

    DeviceCommand command{kGetHapticCapabilitiesCommand, nullptr};
    REQUIRE(manager.send_device_command(gamepad, &command) == 5);
    REQUIRE(addedCalls == 1);
    REQUIRE(legacyCalls == 0);

    runtime.remove_device_command_handler(handlerId);
    REQUIRE(manager.send_device_command(gamepad, &command) == 6);
    REQUIRE(legacyCalls == 1);
    runtime.clear_device_command_handler();
}




TEST_CASE("HID backend merges generic input reports into one device") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    HidBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    const uint8_t descriptor[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
        0x85, 0x01, 0x09, 0x30, 0x09, 0x31,
        0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02,
        0x81, 0x02,
        0x85, 0x02, 0x09, 0x32,
        0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x01,
        0x81, 0x02,
        0xC0,
    };

    hid::DeviceInfo info;
    info.id = "multi";
    info.vendorId = 0x1234;
    info.productId = 0x0001;
    info.description.product = "Multi Report HID";
    info.reportDescriptor.assign(descriptor, descriptor + sizeof(descriptor));
    REQUIRE(backend.add_device(info));
    REQUIRE(manager.devices().size() == 1);

    Device* device = manager.devices()[0];
    REQUIRE(device->state_size_bytes() == 5);
    REQUIRE(device->find_control("x") != nullptr);
    REQUIRE(device->find_control("z") != nullptr);
    REQUIRE(backend.device_id("multi") == device->deviceId);

    const uint8_t report1[] = {0x01, 255, 0};
    const uint8_t report2[] = {0x02, 128};
    REQUIRE(backend.feed_report("multi", report1, sizeof(report1), 1.0));
    REQUIRE(backend.feed_report("multi", report2, sizeof(report2), 1.0));
    backend.poll();
    manager.update(UpdateType::Dynamic);
    REQUIRE(device->find_control("x")->read_float(device->current_state()) ==
            1.0f);
    REQUIRE(device->current_state()[3] == 0x02);
    REQUIRE(device->current_state()[4] == 128);
    Control* z = device->find_control("z");
    REQUIRE(z->read_float(device->current_state()) ==
            Catch::Approx(0.00392157f).margin(0.001f));

    const uint8_t report1b[] = {0x01, 0, 255};
    REQUIRE(backend.feed_report("multi", report1b, sizeof(report1b), 2.0));
    backend.poll();
    manager.update(UpdateType::Dynamic);
    REQUIRE(device->find_control("x")->read_float(device->current_state()) ==
            -1.0f);
    REQUIRE(device->current_state()[3] == 0x02);
    REQUIRE(device->current_state()[4] == 128);

    REQUIRE(backend.remove_device("multi"));
    REQUIRE(manager.devices().empty());
    backend.shutdown();
}

TEST_CASE("HID backend routes DualSense adaptive trigger effects") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    HidBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    hid::DeviceInfo info;
    info.id = "ds5";
    info.vendorId = 0x054C;
    info.productId = 0x0CE6;
    info.description.product = "DualSense";
    REQUIRE(backend.add_device(info));

    std::vector<uint8_t> output;
    backend.set_output_report_handler(
        [&](std::string_view, const uint8_t* data, std::size_t size) {
            output.assign(data, data + size);
            return true;
        });
    Device* device = manager.devices()[0];

    HapticCapabilities capabilities;
    GetHapticCapabilitiesCommand request{&capabilities};
    DeviceCommand capabilityCommand{kGetHapticCapabilitiesCommand, &request};
    REQUIRE(manager.send_device_command(device, &capabilityCommand) == 1);
    REQUIRE(capabilities.supportsLeftTrigger);
    REQUIRE(capabilities.supportsRightTrigger);

    SetAdaptiveTriggerCommand right;
    right.trigger = SetAdaptiveTriggerCommand::Trigger::Right;
    right.effect.mode = 0x21;
    right.effect.parameters[0] = 0xAA;
    DeviceCommand rightCommand{kSetAdaptiveTriggerCommand, &right};
    REQUIRE(manager.send_device_command(device, &rightCommand) == 1);
    REQUIRE(output.size() == 48);
    REQUIRE(output[1] == 0x07);
    REQUIRE(output[11] == 0x21);
    REQUIRE(output[12] == 0xAA);

    SetAdaptiveTriggerCommand left;
    left.trigger = SetAdaptiveTriggerCommand::Trigger::Left;
    left.effect.mode = 0x26;
    DeviceCommand leftCommand{kSetAdaptiveTriggerCommand, &left};
    REQUIRE(manager.send_device_command(device, &leftCommand) == 1);
    REQUIRE(output[1] == 0x0F);
    REQUIRE(output[22] == 0x26);

    right.effect = AdaptiveTriggerEffect::weapon(0.0f, 1.0f, 0.5f);
    REQUIRE(manager.send_device_command(device, &rightCommand) == 1);
    REQUIRE(output[11] == 0x25);
    REQUIRE(output[12] == 0x00);
    REQUIRE(output[13] == 0xFF);
    REQUIRE(output[14] == 0x80);

    backend.remove_device("ds5");
    backend.shutdown();
}





TEST_CASE("HID backend infers DualShock 4 Bluetooth output reports") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    HidBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    hid::DeviceInfo info;
    info.id = "ds4-bt";
    info.vendorId = 0x054C;
    info.productId = 0x05C4;
    info.description.product = "DualShock 4";
    REQUIRE(backend.add_device(info));

    std::vector<uint8_t> output;
    backend.set_output_report_handler(
        [&](std::string_view, const uint8_t* data, std::size_t size) {
            output.assign(data, data + size);
            return true;
        });

    std::array<uint8_t, 12> report{};
    report[0] = 0x11;
    report[1] = 0x80;
    REQUIRE(backend.feed_report("ds4-bt", report.data(), report.size()));
    REQUIRE(backend.transport("ds4-bt") == hid::HidTransport::Bluetooth);

    SendHapticImpulseCommand impulse;
    impulse.amplitude = 0.5f;
    DeviceCommand command{kSendHapticImpulseCommand, &impulse};
    REQUIRE(manager.send_device_command(manager.devices()[0], &command) == 1);
    REQUIRE(output.size() == 78);
    REQUIRE(output[0] == 0x11);
    REQUIRE(output[1] == 0xC0);
    REQUIRE(output[7] == 127);

    backend.remove_device("ds4-bt");
    backend.shutdown();
}

TEST_CASE("HID backend sends explicit Switch subcommands") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    HidBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    hid::DeviceInfo info;
    info.id = "switch-sub";
    info.vendorId = 0x057E;
    info.productId = 0x2009;
    info.description.product = "Switch Pro Controller";
    REQUIRE(backend.add_device(info));

    std::vector<uint8_t> output;
    backend.set_output_report_handler(
        [&](std::string_view, const uint8_t* data, std::size_t size) {
            output.assign(data, data + size);
            return true;
        });

    const uint8_t payload = 0x01;
    SendSwitchSubcommandCommand request;
    request.subcommand = static_cast<uint8_t>(hid::SwitchSubcommand::EnableImu);
    request.data = &payload;
    request.size = 1;
    DeviceCommand command{kSendSwitchSubcommandCommand, &request};
    REQUIRE(manager.send_device_command(manager.devices()[0], &command) == 1);
    REQUIRE(output.size() == 49);
    REQUIRE(output[0] == 0x01);
    REQUIRE(output[10] == 0x40);
    REQUIRE(output[11] == 0x01);

    backend.remove_device("switch-sub");
    backend.shutdown();
}

TEST_CASE("HID backend sends Steam Deck rumble as a feature report") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    HidBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    hid::DeviceInfo info;
    info.id = "deck";
    info.vendorId = 0x28DE;
    info.productId = 0x1205;
    info.description.product = "Steam Deck";
    REQUIRE(backend.add_device(info));
    REQUIRE(backend.kind("deck") == hid::DeviceKind::SteamDeck);

    HapticCapabilities capabilities;
    GetHapticCapabilitiesCommand capabilityRequest{&capabilities};
    DeviceCommand capabilityCommand{kGetHapticCapabilitiesCommand,
                                    &capabilityRequest};
    REQUIRE(manager.send_device_command(manager.devices()[0],
                                        &capabilityCommand) == 1);
    REQUIRE(capabilities.supportsLowFrequency);
    REQUIRE(capabilities.supportsHighFrequency);

    std::vector<uint8_t> feature;
    backend.set_feature_report_handler(
        [&](std::string_view, const uint8_t* data, std::size_t size) {
            feature.assign(data, data + size);
            return true;
        });

    SendHapticImpulseCommand impulse;
    impulse.motorChannel =
        static_cast<int>(SendHapticImpulseCommand::Motor::LowFrequency);
    impulse.amplitude = 0.5f;
    DeviceCommand command{kSendHapticImpulseCommand, &impulse};
    REQUIRE(manager.send_device_command(manager.devices()[0], &command) == 1);
    REQUIRE(feature.size() == 65);
    REQUIRE(feature[1] == 0xEB);
    REQUIRE(feature[2] == 0x09);
    REQUIRE(feature[6] == 0xFF);
    REQUIRE(feature[7] == 0x7F);
    REQUIRE(feature[10] == 0x02);

    backend.remove_device("deck");
    backend.shutdown();
}

