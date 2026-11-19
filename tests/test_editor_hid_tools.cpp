#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "insys/backend/hid_backend.h"
#include "insys/editor/editor_hid_tools.h"
#include "insys/editor/editor_preview.h"
#include "insys/hid/hid_gamepad.h"

using namespace insys;

namespace {

const std::uint8_t kDescriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x00,
    0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x01, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x01, 0x81, 0x02, 0x75, 0x07, 0x95, 0x01,
    0x81, 0x03,
    0xC0,
};

hid::DeviceInfo fake_device(std::string id) {
    hid::DeviceInfo info;
    info.id = std::move(id);
    info.vendorId = 0x1234;
    info.productId = 0x0001;
    info.description.product = "Fake HID";
    info.description.manufacturer = "Tests";
    info.reportDescriptor.assign(kDescriptor, kDescriptor + sizeof(kDescriptor));
    info.inputReportSize = 2;
    info.outputReportSize = 8;
    info.featureReportSize = 8;
    return info;
}

hid::DeviceInfo switch_device() {
    hid::DeviceInfo info;
    info.id = "switch";
    info.vendorId = 0x057E;
    info.productId = 0x2009;
    info.description.product = "Switch Pro Controller";
    info.outputReportSize = 49;
    return info;
}

hid::DeviceInfo dualsense_device() {
    hid::DeviceInfo info;
    info.id = "ds5";
    info.vendorId = 0x054C;
    info.productId = 0x0CE6;
    info.description.product = "DualSense";
    info.outputReportSize = 48;
    return info;
}

} // namespace

TEST_CASE("HID tool service exposes device views and descriptors") {
    editor::PreviewRuntimeOptions options;
    options.enableHid = true;
    options.enableHidPlatform = false;
    editor::PreviewRuntime preview(std::move(options));
    editor::HidToolService tools(preview);

    preview.ensure_hid();
    REQUIRE(preview.hid_backend() != nullptr);
    REQUIRE(preview.hid_backend()->add_device(fake_device("fake")));

    const std::vector<backend::DeviceView> devices = tools.devices();
    REQUIRE(devices.size() == 1);
    REQUIRE(devices[0].id == "fake");
    REQUIRE(devices[0].kind == hid::DeviceKind::Generic);
    REQUIRE(devices[0].info.inputReportSize == 2);
    REQUIRE(devices[0].layoutName == "HID fake");

    std::string error;
    const std::optional<editor::HidDescriptorView> descriptor =
        tools.descriptor("fake", error);
    REQUIRE(descriptor.has_value());
    REQUIRE(error.empty());
    REQUIRE(descriptor->parsed);
    REQUIRE(!descriptor->descriptor.elements.empty());
    REQUIRE(!descriptor->descriptor.reports.empty());
    REQUIRE(!descriptor->controls.empty());
}

TEST_CASE("HID tool service monitors raw input and output reports") {
    editor::PreviewRuntimeOptions options;
    options.enableHid = true;
    options.enableHidPlatform = false;
    editor::PreviewRuntime preview(std::move(options));
    editor::HidToolService tools(preview);
    preview.ensure_hid();
    REQUIRE(preview.hid_backend()->add_device(fake_device("fake")));

    std::string error;
    const std::string monitor = tools.start_monitor(
        "fake", {backend::HidReportKind::Input}, error);
    REQUIRE(error.empty());
    REQUIRE(!monitor.empty());

    const std::uint8_t input[] = {0x01, 0x80};
    preview.hid_backend()->feed_report("fake", input, sizeof(input), 1.0);
    std::size_t dropped = 0;
    bool paused = false;
    const std::vector<editor::HidMonitorReport> reports =
        tools.poll_monitor(monitor, 10, dropped, paused);
    REQUIRE(reports.size() == 1);
    REQUIRE(reports[0].kind == backend::HidReportKind::Input);
    REQUIRE(reports[0].data == std::vector<std::uint8_t>(input, input + 2));

    REQUIRE(tools.pause_monitor(monitor, true));
    const std::uint8_t pausedInput[] = {0x01, 0x40};
    preview.hid_backend()->feed_report("fake", pausedInput,
                                       sizeof(pausedInput), 2.0);
    REQUIRE(tools.poll_monitor(monitor, 10, dropped, paused).empty());
    REQUIRE(paused);
    REQUIRE(tools.pause_monitor(monitor, false));
    const std::uint8_t resumedInput[] = {0x01, 0x20};
    preview.hid_backend()->feed_report("fake", resumedInput,
                                       sizeof(resumedInput), 3.0);
    REQUIRE(tools.poll_monitor(monitor, 10, dropped, paused).size() == 1);
    REQUIRE(tools.clear_monitor(monitor));

    tools.stop_monitor(monitor);
    const std::string outputMonitor = tools.start_monitor(
        "fake", {backend::HidReportKind::Output}, error);
    REQUIRE(!outputMonitor.empty());

    std::vector<std::uint8_t> output;
    preview.hid_backend()->set_output_report_handler(
        [&](std::string_view, const std::uint8_t* data, std::size_t size) {
            output.assign(data, data + size);
            return true;
        });
    const std::vector<std::uint8_t> bytes = {0x02, 0xAA, 0x55};
    REQUIRE(tools.write_output("fake", bytes, error));
    REQUIRE(output == bytes);
    const std::vector<editor::HidMonitorReport> outputReports =
        tools.poll_monitor(outputMonitor, 10, dropped, paused);
    REQUIRE(outputReports.size() == 1);
    REQUIRE(outputReports[0].kind == backend::HidReportKind::Output);
}

TEST_CASE("HID tool service reads and writes feature reports") {
    editor::PreviewRuntimeOptions options;
    options.enableHid = true;
    options.enableHidPlatform = false;
    editor::PreviewRuntime preview(std::move(options));
    editor::HidToolService tools(preview);
    preview.ensure_hid();
    REQUIRE(preview.hid_backend()->add_device(fake_device("fake")));

    std::vector<std::uint8_t> featureWrite;
    preview.hid_backend()->set_feature_report_handler(
        [&](std::string_view, const std::uint8_t* data, std::size_t size) {
            featureWrite.assign(data, data + size);
            return true;
        });
    std::vector<std::uint8_t> featureRead = {0x03, 0x11, 0x22, 0x33};
    preview.hid_backend()->set_feature_report_reader(
        [&](std::string_view, std::uint8_t, std::uint8_t* data,
            std::size_t size) -> int {
            if (size < featureRead.size())
                return -1;
            std::copy(featureRead.begin(), featureRead.end(), data);
            return static_cast<int>(featureRead.size());
        });

    std::string error;
    const std::string monitor = tools.start_monitor(
        "fake", {backend::HidReportKind::Feature}, error);
    REQUIRE(!monitor.empty());
    std::vector<std::uint8_t> bytes;
    REQUIRE(tools.read_feature("fake", 3, 8, bytes, error));
    REQUIRE(bytes == featureRead);
    REQUIRE(tools.write_feature("fake", {0x04, 0x99}, error));
    REQUIRE(featureWrite == std::vector<std::uint8_t>({0x04, 0x99}));
    std::size_t dropped = 0;
    bool paused = false;
    const std::vector<editor::HidMonitorReport> reports =
        tools.poll_monitor(monitor, 10, dropped, paused);
    REQUIRE(reports.size() == 2);
}

TEST_CASE("HID tool service routes Switch subcommands") {
    editor::PreviewRuntimeOptions options;
    options.enableHid = true;
    options.enableHidPlatform = false;
    editor::PreviewRuntime preview(std::move(options));
    editor::HidToolService tools(preview);
    preview.ensure_hid();
    REQUIRE(preview.hid_backend()->add_device(switch_device()));

    std::vector<std::uint8_t> output;
    preview.hid_backend()->set_output_report_handler(
        [&](std::string_view, const std::uint8_t* data, std::size_t size) {
            output.assign(data, data + size);
            return true;
        });

    std::string error;
    REQUIRE(tools.switch_subcommand("switch", 0x40, {0x01}, error));
    REQUIRE(output.size() == 49);
    REQUIRE(output[10] == 0x40);
}

TEST_CASE("HID tool service routes rumble lightbar and adaptive triggers") {
    editor::PreviewRuntimeOptions options;
    options.enableHid = true;
    options.enableHidPlatform = false;
    editor::PreviewRuntime preview(std::move(options));
    editor::HidToolService tools(preview);
    preview.ensure_hid();
    REQUIRE(preview.hid_backend()->add_device(dualsense_device()));

    std::vector<std::vector<std::uint8_t>> reports;
    preview.hid_backend()->set_output_report_handler(
        [&](std::string_view, const std::uint8_t* data, std::size_t size) {
            reports.emplace_back(data, data + size);
            return true;
        });

    std::string error;
    REQUIRE(tools.haptics("ds5", 0.5f, 0.25f, error));
    REQUIRE(!reports.empty());
    REQUIRE(tools.lightbar("ds5", 1.0f, 0.5f, 0.0f, error));
    REQUIRE(tools.adaptive_trigger("ds5", false, 0x21, {128, 200}, error));
    REQUIRE(reports.size() >= 3);
}
