#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "insys/device_description.h"
#include "insys/device_states.h"
#include "insys/hid/hid_descriptor.h"

namespace insys::hid {

enum class DeviceKind {
    Generic,
    DualShock3,
    DualShock4,
    DualSense,
    SwitchPro,
    SteamController,
    SteamDeck,
};

enum class HidTransport : uint8_t {
    Unknown,
    Usb,
    Bluetooth,
    Other,
};

struct DeviceInfo {
    std::string id;
    DeviceDescription description;
    uint32_t vendorId = 0;
    uint32_t productId = 0;
    uint16_t usagePage = 0;
    uint16_t usage = 0;
    std::vector<uint8_t> reportDescriptor;
    uint16_t inputReportSize = 0;
    uint16_t outputReportSize = 0;
    uint16_t featureReportSize = 0;
    bool reportDataIncludesId = true;
    HidTransport transport = HidTransport::Unknown;
    bool bluetooth = false;
    bool bluetoothKnown = false;
};

struct HidGamepadState {
    GamepadState gamepad;
    uint32_t extraButtons = 0;
    Vector2 leftTouchpad;
    Vector2 rightTouchpad;
    Vector3 angularVelocity;
    Vector3 acceleration;
};
static_assert(sizeof(HidGamepadState) == 72,
              "HidGamepadState must match the HID gamepad layout");
static_assert(offsetof(HidGamepadState, extraButtons) == 28,
              "HID extra buttons must follow GamepadState");
static_assert(offsetof(HidGamepadState, angularVelocity) == 48,
              "HID sensors must follow touchpad state");

enum ExtraButton : uint32_t {
    kExtraLeftTriggerButton = 1u << 0,
    kExtraRightTriggerButton = 1u << 1,
    kExtraSystemButton = 1u << 2,
    kExtraTouchpadButton = 1u << 3,
    kExtraMicButton = 1u << 4,
    kExtraCapture = 1u << 5,
    kExtraHome = 1u << 6,
    kExtraLeftGrip = 1u << 7,
    kExtraRightGrip = 1u << 8,
    kExtraLeftBack = 1u << 9,
    kExtraRightBack = 1u << 10,
    kExtraTouchpadContact1 = 1u << 11,
    kExtraTouchpadContact2 = 1u << 12,
};

DeviceKind detect_device_kind(const DeviceInfo& info);
const char* default_layout_name(DeviceKind kind);
bool is_gamepad_kind(DeviceKind kind);

bool decode_dualshock3(const uint8_t* data, std::size_t size,
                       HidGamepadState& out);
bool decode_dualshock4(const uint8_t* data, std::size_t size,
                       HidGamepadState& out);
bool decode_dualsense(const uint8_t* data, std::size_t size,
                      HidGamepadState& out);
bool decode_switch_pro(const uint8_t* data, std::size_t size,
                       HidGamepadState& out);
bool decode_steam_controller(const uint8_t* data, std::size_t size,
                             HidGamepadState& out);
bool decode_steam_deck(const uint8_t* data, std::size_t size,
                       HidGamepadState& out);

uint32_t hid_crc32(uint8_t seed, const uint8_t* data, std::size_t size);
bool validate_bluetooth_crc(const uint8_t* data, std::size_t size,
                            uint8_t seed);
bool validate_dualshock4_input_report(const uint8_t* data, std::size_t size);
bool validate_dualsense_input_report(const uint8_t* data, std::size_t size);

bool encode_dualshock3_haptic(float lowFrequency, float highFrequency,
                              std::vector<uint8_t>& out,
                              uint32_t outputReportSize = 49);
bool encode_dualshock4_haptic(float lowFrequency, float highFrequency,
                              std::vector<uint8_t>& out,
                              uint32_t outputReportSize = 32,
                              const float* lightBarColor = nullptr,
                              bool bluetooth = false,
                              uint8_t sequence = 0);
bool encode_dualsense_haptic(float lowFrequency, float highFrequency,
                             std::vector<uint8_t>& out,
                             uint32_t outputReportSize = 48,
                             const float* lightBarColor = nullptr,
                             bool bluetooth = false,
                             uint8_t sequence = 0,
                             const uint8_t* leftTriggerEffect = nullptr,
                             const uint8_t* rightTriggerEffect = nullptr);
enum class SwitchMagicCommand : uint8_t {
    Status = 0x01,
    Handshake = 0x02,
    Highspeed = 0x03,
    ForceUSB = 0x04,
};

bool encode_switch_magic_output(SwitchMagicCommand command,
                                std::vector<uint8_t>& out,
                                bool bluetooth = true);
enum class SwitchSubcommand : uint8_t {
    BluetoothPairing = 0x01,
    RequestDeviceInfo = 0x02,
    SetInputReportMode = 0x03,
    TriggerButtonElapsed = 0x04,
    GetPageListState = 0x05,
    SetHciState = 0x06,
    ResetPairingInfo = 0x07,
    SetShipmentLowPower = 0x08,
    SpiRead = 0x10,
    SpiWrite = 0x11,
    SpiErase = 0x12,
    ResetNfcIr = 0x20,
    SetNfcIrConfig = 0x21,
    SetNfcIrState = 0x22,
    SetNfcIrData = 0x24,
    ResetNfcIrData = 0x25,
    SetNfcIrMcuData = 0x28,
    GetNfcIrMcuData = 0x29,
    SetGpio = 0x2A,
    GetGpio = 0x2B,
    SetPlayerLights = 0x30,
    GetPlayerLights = 0x31,
    SetHomeLight = 0x38,
    EnableImu = 0x40,
    SetImuSensitivity = 0x41,
    WriteImuRegister = 0x42,
    ReadImuRegister = 0x43,
    EnableVibration = 0x48,
    GetVoltage = 0x50,
    SetGpioPort1 = 0x51,
    GetGpioPort1 = 0x52,
};
enum class SwitchInputReportMode : uint8_t {
    Full = 0x30,
    NfcIr = 0x31,
    Simple = 0x3F,
};
enum class SwitchNfcIrState : uint8_t {
    Suspend = 0x00,
    Resume = 0x01,
    ResumeForUpdate = 0x02,
};
struct SwitchImuSensitivity {
    uint8_t gyroRange = 0x03;
    uint8_t accelerometerRange = 0x00;
    uint8_t gyroPerformanceRate = 0x01;
    uint8_t accelerometerFilterBandwidth = 0x01;
};
bool encode_switch_subcommand(SwitchSubcommand subcommand,
                              const uint8_t* payload, std::size_t payloadSize,
                              std::vector<uint8_t>& out,
                              bool bluetooth = true,
                              uint8_t packetCounter = 0);
bool make_switch_player_lights_payload(uint8_t lights,
                                       std::vector<uint8_t>& out);
bool make_switch_home_light_payload(const uint8_t* effect,
                                    std::size_t effectSize,
                                    std::vector<uint8_t>& out);
bool make_switch_imu_sensitivity_payload(
    const SwitchImuSensitivity& sensitivity, std::vector<uint8_t>& out);
bool make_switch_spi_read_payload(uint32_t address, uint8_t size,
                                  std::vector<uint8_t>& out);
bool make_switch_spi_write_payload(uint32_t address, const uint8_t* data,
                                   uint8_t size, std::vector<uint8_t>& out);
bool make_switch_spi_erase_payload(uint32_t address,
                                   std::vector<uint8_t>& out);
bool make_switch_nfc_ir_config_payload(const uint8_t* config,
                                       std::size_t configSize,
                                       std::vector<uint8_t>& out);
bool make_switch_nfc_ir_state_payload(SwitchNfcIrState state,
                                      std::vector<uint8_t>& out);
bool encode_switch_haptic(float lowFrequency, float highFrequency,
                          std::vector<uint8_t>& out);
bool encode_steam_haptic(float lowFrequency, float highFrequency,
                         std::vector<uint8_t>& out);
bool encode_steam_deck_haptic(float lowFrequency, float highFrequency,
                              std::vector<uint8_t>& out);

} // namespace insys::hid







