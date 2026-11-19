#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "insys/actions/control_scheme.h"
#include "insys/device_builder.h"
#include "insys/device_commands.h"
#include "insys/hid/hid_gamepad.h"
#include "insys/layout_registry.h"

using namespace insys;
using namespace insys::hid;

namespace {

bool has_button(const HidGamepadState& state, uint32_t bit) {
    return (state.gamepad.buttons & bit) != 0;
}

} // namespace

TEST_CASE("HID product detection follows vendor and product IDs") {
    DeviceInfo info;
    info.vendorId = 0x054C;
    info.productId = 0x0DF2;
    REQUIRE(detect_device_kind(info) == DeviceKind::DualSense);

    info.productId = 0x0268;
    REQUIRE(detect_device_kind(info) == DeviceKind::DualShock3);

    info.vendorId = 0x0F0D;
    info.productId = 0x00AA;
    REQUIRE(detect_device_kind(info) == DeviceKind::SwitchPro);

    info.vendorId = 0x20D6;
    info.productId = 0xA716;
    REQUIRE(detect_device_kind(info) == DeviceKind::SwitchPro);

    info.vendorId = 0x28DE;
    info.productId = 0x1142;
    REQUIRE(detect_device_kind(info) == DeviceKind::SteamController);
}

TEST_CASE("HID product layouts extend Gamepad with extra controls") {
    LayoutRegistry registry;
    register_default_layouts(registry);
    DeviceBuilder builder(registry);

    auto device = builder.build("DualShock4GamepadHID", "", 3, {});
    REQUIRE(device != nullptr);
    REQUIRE(device->state_size_bytes() == 72);
    auto* gamepad = dynamic_cast<Gamepad*>(device.get());
    REQUIRE(gamepad != nullptr);
    REQUIRE(gamepad->left_stick() != nullptr);
    REQUIRE(gamepad->right_trigger() != nullptr);
    REQUIRE(device->find_control("systemButton") != nullptr);
    REQUIRE(device->find_control("touchpadButton") != nullptr);
    REQUIRE(device->find_control("micButton") != nullptr);
    REQUIRE(device->find_control("leftTouchpad") != nullptr);
}

TEST_CASE("DualShock 3 HID report decodes buttons, sticks and triggers") {
    std::array<uint8_t, 32> report{};
    report[2] = 0x01;
    report[3] = 0x80;
    report[4] = 0x01;
    report[6] = 255;
    report[7] = 128;
    report[8] = 0;
    report[9] = 128;
    report[18] = 255;
    report[19] = 0;

    HidGamepadState state;
    REQUIRE(decode_dualshock3(report.data(), report.size(), state));
    REQUIRE(state.gamepad.leftStick.x == 1.0f);
    REQUIRE(state.gamepad.leftStick.y == 0.0f);
    REQUIRE(state.gamepad.rightStick.x == -1.0f);
    REQUIRE(has_button(state, 1u << 7));
    REQUIRE(has_button(state, 1u << 13));
    REQUIRE((state.extraButtons & kExtraSystemButton) != 0);
    REQUIRE(state.gamepad.leftTrigger == 1.0f);
    REQUIRE(state.gamepad.rightTrigger == 0.0f);

    LayoutRegistry registry;
    register_default_layouts(registry);
    DeviceBuilder builder(registry);
    auto device = builder.build("DualShock3GamepadHID", "", 3, {});
    REQUIRE(device != nullptr);
    REQUIRE(device->state_size_bytes() == 72);

    DeviceDescription description;
    description.interfaceName = "HID";
    description.product = "PLAYSTATION(R)3 Controller";
    REQUIRE(registry.find_matching_layout(description) == "dualshock3gamepadhid");
}

TEST_CASE("DualShock 4 USB and Bluetooth reports decode") {
    const std::array<uint8_t, 10> usb = {
        0x01, 255, 128, 0, 128, 0x25, 0xF3, 0x03, 255, 128,
    };

    HidGamepadState state;
    REQUIRE(decode_dualshock4(usb.data(), usb.size(), state));
    REQUIRE(state.gamepad.leftStick.x == 1.0f);
    REQUIRE(state.gamepad.leftStick.y == 0.0f);
    REQUIRE(state.gamepad.rightStick.x == -1.0f);
    REQUIRE(has_button(state, 1u << 1));
    REQUIRE(has_button(state, 1u << 2));
    REQUIRE_FALSE(has_button(state, 1u << 0));
    REQUIRE_FALSE(has_button(state, 1u << 3));
    REQUIRE(has_button(state, 1u << 6));
    REQUIRE(has_button(state, 1u << 10));
    REQUIRE(has_button(state, 1u << 11));
    REQUIRE(has_button(state, 1u << 12));
    REQUIRE(has_button(state, 1u << 13));
    REQUIRE(has_button(state, 1u << 8));
    REQUIRE(has_button(state, 1u << 9));
    REQUIRE((state.extraButtons & kExtraSystemButton) != 0);
    REQUIRE((state.extraButtons & kExtraTouchpadButton) != 0);
    REQUIRE(state.gamepad.leftTrigger == 1.0f);
    REQUIRE(state.gamepad.rightTrigger > 0.5f);

    std::array<uint8_t, 12> bluetooth{};
    bluetooth[0] = 0x11;
    bluetooth[1] = 0x80;
    std::copy(usb.begin() + 1, usb.end(), bluetooth.begin() + 3);
    HidGamepadState bluetoothState;
    REQUIRE(decode_dualshock4(bluetooth.data(), bluetooth.size(), bluetoothState));
    REQUIRE(bluetoothState.gamepad.leftStick.x == 1.0f);
    REQUIRE(has_button(bluetoothState, 1u << 6));
}

TEST_CASE("DualSense USB full report decodes buttons, triggers and mic") {
    const std::array<uint8_t, 11> report = {
        0x01, 0, 128, 255, 128, 255, 128, 0, 0x40, 0x20, 0x04,
    };

    HidGamepadState state;
    REQUIRE(decode_dualsense(report.data(), report.size(), state));
    REQUIRE(state.gamepad.leftStick.x == -1.0f);
    REQUIRE(state.gamepad.rightStick.x == 1.0f);
    REQUIRE(state.gamepad.leftTrigger == 1.0f);
    REQUIRE(state.gamepad.rightTrigger > 0.5f);
    REQUIRE(has_button(state, 1u << 5));
    REQUIRE(has_button(state, 1u << 12));
    REQUIRE((state.extraButtons & kExtraMicButton) != 0);
}

TEST_CASE("Switch Pro simple report decodes digital triggers and extras") {
    const std::array<uint8_t, 12> report = {
        0x3F, 0x45, 0x32, 2, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80,
    };

    HidGamepadState state;
    REQUIRE(decode_switch_pro(report.data(), report.size(), state));
    REQUIRE(has_button(state, 1u << 7));
    REQUIRE(has_button(state, 1u << 5));
    REQUIRE(has_button(state, 1u << 3));
    REQUIRE(state.gamepad.leftTrigger == 1.0f);
    REQUIRE(state.gamepad.rightTrigger == 0.0f);
    REQUIRE(has_button(state, 1u << 12));
    REQUIRE((state.extraButtons & kExtraHome) != 0);
    REQUIRE((state.extraButtons & kExtraCapture) != 0);
    REQUIRE(state.gamepad.leftStick.x == Catch::Approx(0.0f).margin(0.01f));
    REQUIRE(state.gamepad.leftStick.y == Catch::Approx(0.0f).margin(0.01f));
}

TEST_CASE("Switch Pro full report decodes sticks, dpad bits and IMU") {
    std::array<uint8_t, 49> report{};
    report[0] = 0x30;
    report[3] = 0x04;
    report[4] = 0x02;
    report[5] = 0x02;
    report[6] = 0x00;
    report[7] = 0x08;
    report[8] = 0x80;
    report[9] = 0x00;
    report[10] = 0x08;
    report[11] = 0x80;
    report[37] = 0x00;
    report[38] = 0x10;
    report[43] = 0x00;
    report[44] = 0x04;

    HidGamepadState state;
    REQUIRE(decode_switch_pro(report.data(), report.size(), state));
    REQUIRE(has_button(state, 1u << 6));
    REQUIRE(has_button(state, 1u << 12));
    REQUIRE(has_button(state, 1u << 0));
    REQUIRE(state.gamepad.leftStick.x == Catch::Approx(0.0f).margin(0.01f));
    REQUIRE(state.gamepad.leftStick.y == Catch::Approx(0.0f).margin(0.01f));
    REQUIRE(state.acceleration.x == Catch::Approx(9.80665f).margin(0.01f));
    REQUIRE(state.angularVelocity.x == Catch::Approx(1.25101f).margin(0.001f));
}

TEST_CASE("Steam Controller report decodes sticks and haptics") {
    std::array<uint8_t, 16> report{};
    report[0] = 0x01;
    report[2] = 0x01;
    report[4] = 255;
    report[6] = 255;
    report[7] = 128;
    report[9] = 128;

    HidGamepadState state;
    REQUIRE(decode_steam_controller(report.data(), report.size(), state));
    REQUIRE(has_button(state, 1u << 6));
    REQUIRE(state.gamepad.leftTrigger == 1.0f);
    REQUIRE(state.gamepad.leftStick.x == 1.0f);
    REQUIRE(state.gamepad.rightStick.x == -1.0f);
    REQUIRE(state.leftTouchpad.x == 1.0f);
}

TEST_CASE("HID haptic reports match product output formats") {
    std::vector<uint8_t> ds4;
    REQUIRE(encode_dualshock4_haptic(1.0f, 0.5f, ds4));
    REQUIRE(ds4.size() == 32);
    REQUIRE(ds4[0] == 0x05);
    REQUIRE(ds4[1] == 0x01);
    REQUIRE(ds4[4] == 127);
    REQUIRE(ds4[5] == 255);

    const float color[] = {1.0f, 0.5f, 0.0f};
    std::vector<uint8_t> ds4Color;
    REQUIRE(encode_dualshock4_haptic(0.0f, 0.0f, ds4Color, 32, color));
    REQUIRE(ds4Color[1] == 0x03);
    REQUIRE(ds4Color[6] == 255);
    REQUIRE(ds4Color[7] == 127);
    REQUIRE(ds4Color[8] == 0);

    std::vector<uint8_t> ds5;
    REQUIRE(encode_dualsense_haptic(0.5f, 1.0f, ds5));
    REQUIRE(ds5.size() == 48);
    REQUIRE(ds5[0] == 0x02);
    REQUIRE(ds5[1] == 0x03);
    REQUIRE(ds5[2] == 0x04);
    REQUIRE(ds5[3] == 255);
    REQUIRE(ds5[4] == 127);

    std::vector<uint8_t> ds5Color;
    REQUIRE(encode_dualsense_haptic(0.0f, 0.0f, ds5Color, 48, color));
    REQUIRE(ds5Color[45] == 255);
    REQUIRE(ds5Color[46] == 127);
    REQUIRE(ds5Color[47] == 0);

    std::vector<uint8_t> switchReport;
    REQUIRE(encode_switch_haptic(1.0f, 0.0f, switchReport));
    REQUIRE(switchReport.size() == 9);
    REQUIRE(switchReport[0] == 0x10);
    REQUIRE(switchReport[1] == 0x00);
    REQUIRE(switchReport[2] != 0x01);
    REQUIRE(switchReport[5] == 0x00);
    REQUIRE(switchReport[6] == 0x01);

    std::vector<uint8_t> steamReport;
    REQUIRE(encode_steam_haptic(0.25f, 0.5f, steamReport));
    REQUIRE(steamReport.size() == 64);
    REQUIRE(steamReport[0] == 0x8F);
    REQUIRE(steamReport[4] == 63);
    REQUIRE(steamReport[5] == 127);
}



TEST_CASE("HID product layouts satisfy Gamepad control schemes") {
    LayoutRegistry registry;
    register_default_layouts(registry);
    DeviceBuilder builder(registry);
    auto device = builder.build("DualSenseGamepadHID", "", 4, {});
    REQUIRE(device != nullptr);

    ControlScheme scheme;
    scheme.devices = {"Gamepad"};
    REQUIRE(scheme_matches(scheme, {device.get()}));
}









TEST_CASE("DualShock 4 full report decodes touch pad and motion") {
    std::array<uint8_t, 64> report{};
    report[0] = 0x01;
    report[13] = 0x00;
    report[14] = 0x04;
    report[19] = 0x00;
    report[20] = 0x20;
    report[35] = 0x00;
    report[36] = 0x80;
    report[39] = 0x80;

    HidGamepadState state;
    REQUIRE(decode_dualshock4(report.data(), report.size(), state));
    REQUIRE(state.angularVelocity.x ==
            Catch::Approx(0.0174533f).margin(0.0001f));
    REQUIRE(state.acceleration.x == Catch::Approx(9.80665f).margin(0.001f));
    REQUIRE((state.extraButtons & kExtraTouchpadContact1) != 0);
    REQUIRE((state.extraButtons & kExtraTouchpadContact2) == 0);
    REQUIRE(state.leftTouchpad.x == Catch::Approx(128.0f / 1920.0f));
    REQUIRE(state.leftTouchpad.y == Catch::Approx(0.0f));
}

TEST_CASE("DualSense full report decodes touch pad and motion") {
    std::array<uint8_t, 64> report{};
    report[0] = 0x01;
    report[16] = 0x00;
    report[17] = 0x04;
    report[22] = 0x00;
    report[23] = 0x20;
    report[33] = 0x00;
    report[34] = 0x80;
    report[37] = 0x80;

    HidGamepadState state;
    REQUIRE(decode_dualsense(report.data(), report.size(), state));
    REQUIRE(state.angularVelocity.x ==
            Catch::Approx(0.0174533f).margin(0.0001f));
    REQUIRE(state.acceleration.x == Catch::Approx(9.80665f).margin(0.001f));
    REQUIRE((state.extraButtons & kExtraTouchpadContact1) != 0);
    REQUIRE((state.extraButtons & kExtraTouchpadContact2) == 0);
    REQUIRE(state.leftTouchpad.x == Catch::Approx(128.0f / 1920.0f));
}

TEST_CASE("DualShock 4 Bluetooth output includes CRC") {
    const float color[] = {1.0f, 0.5f, 0.0f};
    std::vector<uint8_t> report;
    REQUIRE(encode_dualshock4_haptic(1.0f, 0.5f, report, 32, color, true));
    REQUIRE(report.size() == 78);
    REQUIRE(report[0] == 0x11);
    REQUIRE(report[1] == 0xC0);
    REQUIRE(report[3] == 0x03);
    REQUIRE(report[6] == 127);
    REQUIRE(report[7] == 255);
    REQUIRE(report[8] == 255);
    REQUIRE(report[9] == 127);
    REQUIRE(report[10] == 0);
    const uint32_t crc = hid_crc32(0xA2, report.data(), report.size() - 4);
    REQUIRE(report[74] == static_cast<uint8_t>(crc & 0xFF));
    REQUIRE(report[75] == static_cast<uint8_t>((crc >> 8) & 0xFF));
}

TEST_CASE("DualSense Bluetooth output includes sequence, triggers and CRC") {
    const float color[] = {1.0f, 0.5f, 0.0f};
    uint8_t left[11] = {};
    uint8_t right[11] = {};
    left[0] = 0x26;
    right[0] = 0x21;
    right[1] = 0xAA;

    std::vector<uint8_t> report;
    REQUIRE(encode_dualsense_haptic(1.0f, 0.5f, report, 48, color, true, 3,
                                    left, right));
    REQUIRE(report.size() == 78);
    REQUIRE(report[0] == 0x31);
    REQUIRE(report[1] == 0x30);
    REQUIRE(report[2] == 0x10);
    REQUIRE(report[3] == 0x0F);
    REQUIRE(report[4] == 0x04);
    REQUIRE(report[5] == 127);
    REQUIRE(report[6] == 255);
    REQUIRE(report[13] == 0x21);
    REQUIRE(report[14] == 0xAA);
    REQUIRE(report[24] == 0x26);
    REQUIRE(report[47] == 255);
    REQUIRE(report[48] == 127);
    REQUIRE(report[49] == 0);
    const uint32_t crc = hid_crc32(0xA2, report.data(), report.size() - 4);
    REQUIRE(report[74] == static_cast<uint8_t>(crc & 0xFF));
}

TEST_CASE("DualShock 4 Bluetooth input CRC is validated") {
    std::array<uint8_t, 78> report{};
    report[0] = 0x11;
    report[1] = 0x80;
    report[7] = 0x20;
    const uint32_t crc = hid_crc32(0xA1, report.data(), report.size() - 4);
    report[74] = static_cast<uint8_t>(crc & 0xFF);
    report[75] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    report[76] = static_cast<uint8_t>((crc >> 16) & 0xFF);
    report[77] = static_cast<uint8_t>((crc >> 24) & 0xFF);

    HidGamepadState state;
    REQUIRE(validate_dualshock4_input_report(report.data(), report.size()));
    REQUIRE(decode_dualshock4(report.data(), report.size(), state));
    REQUIRE(has_button(state, 1u << 6));

    report[20] ^= 0x01;
    REQUIRE_FALSE(validate_dualshock4_input_report(report.data(), report.size()));
    REQUIRE_FALSE(decode_dualshock4(report.data(), report.size(), state));
}

TEST_CASE("DualSense Bluetooth input CRC is validated") {
    std::array<uint8_t, 78> report{};
    report[0] = 0x31;
    report[2] = 128;
    report[3] = 128;
    report[4] = 128;
    report[5] = 128;
    report[9] = 0x20;
    const uint32_t crc = hid_crc32(0xA1, report.data(), report.size() - 4);
    report[74] = static_cast<uint8_t>(crc & 0xFF);
    report[75] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    report[76] = static_cast<uint8_t>((crc >> 16) & 0xFF);
    report[77] = static_cast<uint8_t>((crc >> 24) & 0xFF);

    HidGamepadState state;
    REQUIRE(validate_dualsense_input_report(report.data(), report.size()));
    REQUIRE(decode_dualsense(report.data(), report.size(), state));
    REQUIRE(has_button(state, 1u << 6));

    report[9] = 0x00;
    REQUIRE_FALSE(validate_dualsense_input_report(report.data(), report.size()));
    REQUIRE_FALSE(decode_dualsense(report.data(), report.size(), state));
}

TEST_CASE("Switch subcommand carries input mode, IMU and vibration") {
    REQUIRE(static_cast<uint8_t>(SwitchSubcommand::SetHomeLight) == 0x38);
    REQUIRE(static_cast<uint8_t>(SwitchSubcommand::EnableImu) == 0x40);
    REQUIRE(static_cast<uint8_t>(SwitchSubcommand::EnableVibration) == 0x48);
    REQUIRE(static_cast<uint8_t>(SwitchSubcommand::GetVoltage) == 0x50);

    const uint8_t mode = static_cast<uint8_t>(SwitchInputReportMode::Full);
    std::vector<uint8_t> report;
    REQUIRE(encode_switch_subcommand(SwitchSubcommand::SetInputReportMode,
                                     &mode, 1, report, true, 5));
    REQUIRE(report.size() == 49);
    REQUIRE(report[0] == 0x01);
    REQUIRE(report[1] == 0x05);
    REQUIRE(report[10] == 0x03);
    REQUIRE(report[11] == 0x30);

    const uint8_t enabled = 1;
    REQUIRE(encode_switch_subcommand(SwitchSubcommand::EnableImu, &enabled, 1,
                                     report, false, 6));
    REQUIRE(report.size() == 64);
    REQUIRE(report[1] == 0x06);
    REQUIRE(report[10] == 0x40);
    REQUIRE(report[11] == 0x01);

    REQUIRE(encode_switch_subcommand(SwitchSubcommand::EnableVibration,
                                     &enabled, 1, report, false, 7));
    REQUIRE(report[10] == 0x48);
    REQUIRE(report[11] == 0x01);
}

TEST_CASE("Switch subcommand payload helpers cover lights, IMU, SPI and NFC/IR") {
    std::vector<uint8_t> payload;
    REQUIRE(make_switch_player_lights_payload(0x1F, payload));
    REQUIRE(payload.size() == 1);
    REQUIRE(payload[0] == 0x1F);

    std::array<uint8_t, 25> home{};
    home[0] = 0x21;
    home[24] = 0x44;
    REQUIRE(make_switch_home_light_payload(home.data(), home.size(), payload));
    REQUIRE(payload.size() == 25);
    REQUIRE(payload[24] == 0x44);

    SwitchImuSensitivity sensitivity;
    sensitivity.gyroRange = 0;
    sensitivity.accelerometerRange = 2;
    sensitivity.gyroPerformanceRate = 0;
    sensitivity.accelerometerFilterBandwidth = 0;
    REQUIRE(make_switch_imu_sensitivity_payload(sensitivity, payload));
    REQUIRE(payload == std::vector<uint8_t>({0x00, 0x02, 0x00, 0x00}));

    REQUIRE(make_switch_spi_read_payload(0xF8006080u, 0x18, payload));
    REQUIRE(payload.size() == 5);
    REQUIRE(payload[0] == 0x80);
    REQUIRE(payload[4] == 0x18);

    const uint8_t flashData[] = {0xAA, 0xBB};
    REQUIRE(make_switch_spi_write_payload(0xF8006080u, flashData, 2, payload));
    REQUIRE(payload.size() == 7);
    REQUIRE(payload[5] == 0xAA);
    REQUIRE(payload[6] == 0xBB);
    REQUIRE_FALSE(make_switch_spi_write_payload(0, flashData, 0, payload));

    REQUIRE(make_switch_spi_erase_payload(0xF8006000u, payload));
    REQUIRE(payload.size() == 4);
    REQUIRE(payload[3] == 0xF8);

    std::array<uint8_t, 37> nfc{};
    nfc[0] = 0x01;
    REQUIRE(make_switch_nfc_ir_config_payload(nfc.data(), nfc.size(), payload));
    REQUIRE(payload.size() == 37);
    REQUIRE(make_switch_nfc_ir_state_payload(SwitchNfcIrState::Resume, payload));
    REQUIRE(payload.size() == 1);
    REQUIRE(payload[0] == 0x01);
}

TEST_CASE("DualSense adaptive trigger presets build effect parameters") {
    auto feedback = AdaptiveTriggerEffect::feedback(0.25f, 1.0f);
    REQUIRE(feedback.mode == 0x21);
    REQUIRE(feedback.parameters[0] == 64);
    REQUIRE(feedback.parameters[1] == 255);

    auto weapon = AdaptiveTriggerEffect::weapon(0.0f, 0.5f, 0.75f);
    REQUIRE(weapon.mode == 0x25);
    REQUIRE(weapon.parameters[1] == 128);
    REQUIRE(weapon.parameters[2] == 191);

    auto vibration = AdaptiveTriggerEffect::vibration(0.5f, 0.25f, 0.75f);
    REQUIRE(vibration.mode == 0x26);
    REQUIRE(vibration.parameters[0] == 128);
    REQUIRE(vibration.parameters[1] == 64);
    REQUIRE(vibration.parameters[2] == 191);

    auto bow = AdaptiveTriggerEffect::bow(0.0f, 1.0f, 0.5f, 0.25f);
    REQUIRE(bow.mode == 0x22);
    REQUIRE(bow.parameters[2] == 128);
    REQUIRE(bow.parameters[3] == 64);

    auto off = AdaptiveTriggerEffect::off();
    REQUIRE(off.mode == 0x05);
    REQUIRE(off.parameters[0] == 0);
}

TEST_CASE("DualShock 3 and Steam Deck haptics use native report formats") {
    std::vector<uint8_t> ds3;
    REQUIRE(encode_dualshock3_haptic(1.0f, 0.5f, ds3));
    REQUIRE(ds3.size() == 49);
    REQUIRE(ds3[0] == 0x01);
    REQUIRE(ds3[2] == 0x01);
    REQUIRE(ds3[3] == 0xFF);
    REQUIRE(ds3[4] == 255);

    std::vector<uint8_t> deck;
    REQUIRE(encode_steam_deck_haptic(0.5f, 0.25f, deck));
    REQUIRE(deck.size() == 65);
    REQUIRE(deck[0] == 0x00);
    REQUIRE(deck[1] == 0xEB);
    REQUIRE(deck[2] == 0x09);
    REQUIRE(deck[6] == 0xFF);
    REQUIRE(deck[7] == 0x7F);
    REQUIRE(deck[8] == 0xFF);
    REQUIRE(deck[9] == 0x3F);
    REQUIRE(deck[10] == 0x02);
}

TEST_CASE("Steam Deck report decodes sticks, buttons, touchpads and sensors") {
    std::array<uint8_t, 64> report{};
    report[0] = 0x01;
    report[1] = 0x00;
    report[2] = 0x09;
    report[8] = 0x20;
    report[9] = 0x02;
    report[10] = 0x08;
    report[44] = 0xFF;
    report[45] = 0x7F;
    report[48] = 0xFF;
    report[49] = 0x7F;
    report[16] = 0x00;
    report[17] = 0x40;
    report[26] = 0x00;
    report[27] = 0x20;

    HidGamepadState state;
    REQUIRE(decode_steam_deck(report.data(), report.size(), state));
    REQUIRE(has_button(state, 1u << 6));
    REQUIRE(has_button(state, 1u << 3));
    REQUIRE(state.gamepad.leftStick.x == Catch::Approx(1.0f).margin(0.001f));
    REQUIRE(state.gamepad.leftTrigger == Catch::Approx(1.0f).margin(0.001f));
    REQUIRE((state.extraButtons & kExtraTouchpadContact1) != 0);
    REQUIRE(state.leftTouchpad.x == Catch::Approx(0.5f).margin(0.001f));
    REQUIRE(state.acceleration.x == Catch::Approx(0.25f).margin(0.01f));

    LayoutRegistry registry;
    register_default_layouts(registry);
    DeviceBuilder builder(registry);
    auto device = builder.build("SteamDeckControllerHID", "", 9, {});
    REQUIRE(device != nullptr);
    REQUIRE(device->state_size_bytes() == 72);
}

