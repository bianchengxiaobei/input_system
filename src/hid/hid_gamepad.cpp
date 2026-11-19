#include "insys/hid/hid_gamepad.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace insys::hid {
namespace {

constexpr uint32_t kDpadUp = 1u << 0;
constexpr uint32_t kDpadDown = 1u << 1;
constexpr uint32_t kDpadLeft = 1u << 2;
constexpr uint32_t kDpadRight = 1u << 3;
constexpr uint32_t kNorth = 1u << 4;
constexpr uint32_t kEast = 1u << 5;
constexpr uint32_t kSouth = 1u << 6;
constexpr uint32_t kWest = 1u << 7;
constexpr uint32_t kLeftStickButton = 1u << 8;
constexpr uint32_t kRightStickButton = 1u << 9;
constexpr uint32_t kLeftShoulder = 1u << 10;
constexpr uint32_t kRightShoulder = 1u << 11;
constexpr uint32_t kStart = 1u << 12;
constexpr uint32_t kSelect = 1u << 13;

void set_button(GamepadState& state, uint32_t bit, bool pressed) {
    if (pressed)
        state.buttons |= bit;
    else
        state.buttons &= ~bit;
}

void set_extra(HidGamepadState& state, uint32_t bit, bool pressed) {
    if (pressed)
        state.extraButtons |= bit;
    else
        state.extraButtons &= ~bit;
}

float normalize_byte_axis(uint8_t value, bool invertY) {
    float axis = (static_cast<float>(value) - 128.0f) / 127.0f;
    axis = std::clamp(axis, -1.0f, 1.0f);
    return invertY ? -axis : axis;
}

float normalize_switch_axis(float value, bool invertY) {
    float normalized = std::clamp(value, 0.0f, 255.0f) / 255.0f;
    normalized = (normalized - 0.5f) / 0.35f;
    normalized = std::clamp(normalized, -1.0f, 1.0f);
    return invertY ? -normalized : normalized;
}

float normalize_trigger(uint8_t value) {
    return static_cast<float>(value) / 255.0f;
}

uint16_t read_le16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0] |
                                 (static_cast<uint16_t>(data[1]) << 8));
}

int16_t read_le16_signed(const uint8_t* data) {
    return static_cast<int16_t>(read_le16(data));
}

uint32_t read_le32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

float sensor_gyro(int16_t raw) {
    constexpr float kDegreesToRadians = 0.01745329251994329577f;
    return (static_cast<float>(raw) / 1024.0f) * kDegreesToRadians;
}

float sensor_accel(int16_t raw) {
    constexpr float kStandardGravity = 9.80665f;
    return (static_cast<float>(raw) / 8192.0f) * kStandardGravity;
}

float sensor_switch_gyro(int16_t raw) {
    constexpr float kDegreesToRadians = 0.01745329251994329577f;
    return (static_cast<float>(raw) * 0.07f) * kDegreesToRadians;
}

float sensor_switch_accel(int16_t raw) {
    constexpr float kStandardGravity = 9.80665f;
    return (static_cast<float>(raw) / 4096.0f) * kStandardGravity;
}

void set_touch_point(HidGamepadState& state, int index, uint8_t contact,
                     const uint8_t* point, float width, float height) {
    const bool active = (contact & 0x80u) == 0;
    const uint32_t rawX =
        static_cast<uint32_t>(point[1]) |
        (static_cast<uint32_t>(point[2] & 0x0Fu) << 8);
    const uint32_t rawY =
        static_cast<uint32_t>((point[2] & 0xF0u) >> 4) |
        (static_cast<uint32_t>(point[3]) << 4);
    Vector2& destination = index == 0 ? state.leftTouchpad : state.rightTouchpad;
    if (!active) {
        destination = {};
        set_extra(state, index == 0 ? kExtraTouchpadContact1
                                    : kExtraTouchpadContact2,
                  false);
        return;
    }
    destination = {
        std::clamp(static_cast<float>(rawX) / width, 0.0f, 1.0f),
        std::clamp(static_cast<float>(rawY) / height, 0.0f, 1.0f),
    };
    set_extra(state, index == 0 ? kExtraTouchpadContact1
                                : kExtraTouchpadContact2,
              true);
}

void apply_dual_shock_motion(HidGamepadState& state, const uint8_t* common,
                             bool hasMotion) {
    if (!hasMotion)
        return;
    state.angularVelocity = {
        sensor_gyro(read_le16_signed(common + 12)),
        sensor_gyro(read_le16_signed(common + 14)),
        sensor_gyro(read_le16_signed(common + 16)),
    };
    state.acceleration = {
        sensor_accel(read_le16_signed(common + 18)),
        sensor_accel(read_le16_signed(common + 20)),
        sensor_accel(read_le16_signed(common + 22)),
    };
}

void apply_dual_sense_motion(HidGamepadState& state, const uint8_t* common) {
    state.angularVelocity = {
        sensor_gyro(read_le16_signed(common + 15)),
        sensor_gyro(read_le16_signed(common + 17)),
        sensor_gyro(read_le16_signed(common + 19)),
    };
    state.acceleration = {
        sensor_accel(read_le16_signed(common + 21)),
        sensor_accel(read_le16_signed(common + 23)),
        sensor_accel(read_le16_signed(common + 25)),
    };
}

void set_dpad_from_hat(GamepadState& state, int hat) {
    bool up = false;
    bool right = false;
    bool down = false;
    bool left = false;
    switch (hat) {
    case 0: up = true; break;
    case 1: up = true; right = true; break;
    case 2: right = true; break;
    case 3: down = true; right = true; break;
    case 4: down = true; break;
    case 5: down = true; left = true; break;
    case 6: left = true; break;
    case 7: up = true; left = true; break;
    default: break;
    }
    set_button(state, kDpadUp, up);
    set_button(state, kDpadDown, down);
    set_button(state, kDpadLeft, left);
    set_button(state, kDpadRight, right);
}

bool is_switch_pro_product(uint32_t vendorId, uint32_t productId) {
    if (vendorId == 0x057E)
        return productId == 0x2009;
    if (vendorId == 0x0F0D) {
        return productId == 0x0092 || productId == 0x00AA ||
               productId == 0x00C1 || productId == 0x00DC ||
               productId == 0x00F6;
    }
    if (vendorId == 0x0E6F) {
        return productId == 0x0180 || productId == 0x0181 ||
               productId == 0x0184 || productId == 0x0185 ||
               productId == 0x0186 || productId == 0x0187 ||
               productId == 0x0188;
    }
    if (vendorId == 0x20D6) {
        return productId == 0xA712 || productId == 0xA714 ||
               productId == 0xA715 || productId == 0xA716;
    }
    return false;
}

bool contains_ci(const std::string& text, const char* needle) {
    if (needle == nullptr || *needle == '\0')
        return false;
    const std::size_t needleSize = std::strlen(needle);
    if (needleSize > text.size())
        return false;
    for (std::size_t i = 0; i + needleSize <= text.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needleSize; ++j) {
            const auto lhs = static_cast<unsigned char>(text[i + j]);
            const auto rhs = static_cast<unsigned char>(needle[j]);
            if (std::tolower(lhs) != std::tolower(rhs)) {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

void apply_dualshock_buttons(HidGamepadState& state, uint8_t buttons0,
                             uint8_t buttons1, uint8_t buttons2,
                             uint8_t leftTrigger, uint8_t rightTrigger,
                             bool hasMic) {
    set_dpad_from_hat(state.gamepad, buttons0 & 0x0F);

    set_button(state.gamepad, kWest, (buttons0 & 0x10) != 0);
    set_button(state.gamepad, kSouth, (buttons0 & 0x20) != 0);
    set_button(state.gamepad, kEast, (buttons0 & 0x40) != 0);
    set_button(state.gamepad, kNorth, (buttons0 & 0x80) != 0);

    set_button(state.gamepad, kLeftShoulder, (buttons1 & 0x01) != 0);
    set_button(state.gamepad, kRightShoulder, (buttons1 & 0x02) != 0);
    set_button(state.gamepad, kSelect, (buttons1 & 0x10) != 0);
    set_button(state.gamepad, kStart, (buttons1 & 0x20) != 0);
    set_button(state.gamepad, kLeftStickButton, (buttons1 & 0x40) != 0);
    set_button(state.gamepad, kRightStickButton, (buttons1 & 0x80) != 0);

    set_extra(state, kExtraLeftTriggerButton, (buttons1 & 0x04) != 0);
    set_extra(state, kExtraRightTriggerButton, (buttons1 & 0x08) != 0);
    set_extra(state, kExtraSystemButton, (buttons2 & 0x01) != 0);
    set_extra(state, kExtraTouchpadButton, (buttons2 & 0x02) != 0);
    set_extra(state, kExtraMicButton, hasMic && (buttons2 & 0x04) != 0);

    state.gamepad.leftTrigger = normalize_trigger(leftTrigger);
    state.gamepad.rightTrigger = normalize_trigger(rightTrigger);
}

void apply_switch_common(HidGamepadState& state, uint8_t buttons0,
                         uint8_t buttons1, int hat, float leftX, float leftY,
                         float rightX, float rightY) {
    set_button(state.gamepad, kWest, (buttons0 & 0x01) != 0);
    set_button(state.gamepad, kSouth, (buttons0 & 0x02) != 0);
    set_button(state.gamepad, kEast, (buttons0 & 0x04) != 0);
    set_button(state.gamepad, kNorth, (buttons0 & 0x08) != 0);
    set_button(state.gamepad, kLeftShoulder, (buttons0 & 0x10) != 0);
    set_button(state.gamepad, kRightShoulder, (buttons0 & 0x20) != 0);
    set_extra(state, kExtraLeftTriggerButton, (buttons0 & 0x40) != 0);
    set_extra(state, kExtraRightTriggerButton, (buttons0 & 0x80) != 0);
    state.gamepad.leftTrigger = (buttons0 & 0x40) != 0 ? 1.0f : 0.0f;
    state.gamepad.rightTrigger = (buttons0 & 0x80) != 0 ? 1.0f : 0.0f;

    set_button(state.gamepad, kSelect, (buttons1 & 0x01) != 0);
    set_button(state.gamepad, kStart, (buttons1 & 0x02) != 0);
    set_button(state.gamepad, kLeftStickButton, (buttons1 & 0x04) != 0);
    set_button(state.gamepad, kRightStickButton, (buttons1 & 0x08) != 0);
    set_extra(state, kExtraHome, (buttons1 & 0x10) != 0);
    set_extra(state, kExtraCapture, (buttons1 & 0x20) != 0);
    set_extra(state, kExtraSystemButton, (buttons1 & 0x10) != 0);

    set_dpad_from_hat(state.gamepad, hat);
    state.gamepad.leftStick = {normalize_switch_axis(leftX, false),
                               normalize_switch_axis(leftY, true)};
    state.gamepad.rightStick = {normalize_switch_axis(rightX, false),
                                normalize_switch_axis(rightY, true)};
}

void apply_switch_full_buttons(HidGamepadState& state, uint8_t buttons0,
                               uint8_t buttons1, uint8_t buttons2) {
    set_button(state.gamepad, kWest, (buttons0 & 0x01) != 0);
    set_button(state.gamepad, kNorth, (buttons0 & 0x02) != 0);
    set_button(state.gamepad, kSouth, (buttons0 & 0x04) != 0);
    set_button(state.gamepad, kEast, (buttons0 & 0x08) != 0);
    set_button(state.gamepad, kRightShoulder, (buttons0 & 0x40) != 0);
    set_extra(state, kExtraRightTriggerButton, (buttons0 & 0x80) != 0);
    state.gamepad.rightTrigger = (buttons0 & 0x80) != 0 ? 1.0f : 0.0f;

    set_button(state.gamepad, kSelect, (buttons1 & 0x01) != 0);
    set_button(state.gamepad, kStart, (buttons1 & 0x02) != 0);
    set_button(state.gamepad, kRightStickButton, (buttons1 & 0x04) != 0);
    set_button(state.gamepad, kLeftStickButton, (buttons1 & 0x08) != 0);
    set_extra(state, kExtraHome, (buttons1 & 0x10) != 0);
    set_extra(state, kExtraCapture, (buttons1 & 0x20) != 0);
    set_extra(state, kExtraSystemButton, (buttons1 & 0x10) != 0);

    set_button(state.gamepad, kDpadDown, (buttons2 & 0x01) != 0);
    set_button(state.gamepad, kDpadUp, (buttons2 & 0x02) != 0);
    set_button(state.gamepad, kDpadRight, (buttons2 & 0x04) != 0);
    set_button(state.gamepad, kDpadLeft, (buttons2 & 0x08) != 0);
    set_button(state.gamepad, kLeftShoulder, (buttons2 & 0x40) != 0);
    set_extra(state, kExtraLeftTriggerButton, (buttons2 & 0x80) != 0);
    state.gamepad.leftTrigger = (buttons2 & 0x80) != 0 ? 1.0f : 0.0f;
}

uint8_t switch_12bit_to_byte(uint16_t value) {
    return static_cast<uint8_t>((static_cast<uint32_t>(value) * 255u + 2047u) / 4095u);
}

void encode_switch_motor(uint8_t* destination, float amplitude) {
    const float value = std::clamp(amplitude, 0.0f, 1.0f);
    if (value <= 0.0f) {
        destination[0] = 0x00;
        destination[1] = 0x01;
    } else {
        destination[0] = 0x00;
        destination[1] = static_cast<uint8_t>(0x40 + value * 0x3F);
    }
    destination[2] = 0x40;
    destination[3] = 0x40;
}

void resize_output(std::vector<uint8_t>& out, uint32_t outputReportSize,
                   std::size_t minimum) {
    const std::size_t requested =
        outputReportSize == 0 ? minimum : outputReportSize;
    out.assign(std::max(requested, minimum), 0);
}

} // namespace

DeviceKind detect_device_kind(const DeviceInfo& info) {
    if (info.vendorId == 0x054C) {
        if (info.productId == 0x0CE6 || info.productId == 0x0DF2)
            return DeviceKind::DualSense;
        if (info.productId == 0x0268)
            return DeviceKind::DualShock3;
        if (info.productId == 0x05C4 || info.productId == 0x09CC ||
            info.productId == 0x0BA0)
            return DeviceKind::DualShock4;
    }

    if (is_switch_pro_product(info.vendorId, info.productId))
        return DeviceKind::SwitchPro;

    if (info.vendorId == 0x28DE) {
        if (info.productId == 0x1205)
            return DeviceKind::SteamDeck;
        if (info.productId == 0x1102 || info.productId == 0x1142 ||
            (info.productId >= 0x1302 && info.productId <= 0x1305))
            return DeviceKind::SteamController;
    }

    if (contains_ci(info.description.product, "dualsense"))
        return DeviceKind::DualSense;
    if (contains_ci(info.description.product, "dualshock 3") ||
        contains_ci(info.description.product, "playstation(r)3"))
        return DeviceKind::DualShock3;
    if (contains_ci(info.description.product, "dualshock") ||
        contains_ci(info.description.product, "wireless controller"))
        return DeviceKind::DualShock4;
    if (contains_ci(info.description.product, "switch pro"))
        return DeviceKind::SwitchPro;
    if (contains_ci(info.description.product, "steam deck"))
        return DeviceKind::SteamDeck;
    if (contains_ci(info.description.product, "steam controller"))
        return DeviceKind::SteamController;

    return DeviceKind::Generic;
}

const char* default_layout_name(DeviceKind kind) {
    switch (kind) {
    case DeviceKind::DualShock3: return "DualShock3GamepadHID";
    case DeviceKind::DualShock4: return "DualShock4GamepadHID";
    case DeviceKind::DualSense: return "DualSenseGamepadHID";
    case DeviceKind::SwitchPro: return "SwitchProControllerHID";
    case DeviceKind::SteamController: return "SteamController";
    case DeviceKind::SteamDeck: return "SteamDeckControllerHID";
    case DeviceKind::Generic: return "HID";
    }
    return "HID";
}

bool is_gamepad_kind(DeviceKind kind) {
    return kind != DeviceKind::Generic;
}

bool decode_dualshock3(const uint8_t* data, std::size_t size,
                       HidGamepadState& out) {
    if (data == nullptr || size < 20)
        return false;

    const uint8_t* payload = data;
    if (size >= 33 && data[0] == 0x01)
        payload = data + 1;

    HidGamepadState state{};
    const uint8_t buttons1 = payload[2];
    const uint8_t buttons2 = payload[3];
    const uint8_t buttons3 = payload[4];

    set_button(state.gamepad, kSelect, (buttons1 & 0x01) != 0);
    set_button(state.gamepad, kLeftStickButton, (buttons1 & 0x02) != 0);
    set_button(state.gamepad, kRightStickButton, (buttons1 & 0x04) != 0);
    set_button(state.gamepad, kStart, (buttons1 & 0x08) != 0);
    set_button(state.gamepad, kDpadUp, (buttons1 & 0x10) != 0);
    set_button(state.gamepad, kDpadRight, (buttons1 & 0x20) != 0);
    set_button(state.gamepad, kDpadDown, (buttons1 & 0x40) != 0);
    set_button(state.gamepad, kDpadLeft, (buttons1 & 0x80) != 0);

    set_extra(state, kExtraLeftTriggerButton, (buttons2 & 0x01) != 0);
    set_extra(state, kExtraRightTriggerButton, (buttons2 & 0x02) != 0);
    set_button(state.gamepad, kLeftShoulder, (buttons2 & 0x04) != 0);
    set_button(state.gamepad, kRightShoulder, (buttons2 & 0x08) != 0);
    set_button(state.gamepad, kNorth, (buttons2 & 0x10) != 0);
    set_button(state.gamepad, kEast, (buttons2 & 0x20) != 0);
    set_button(state.gamepad, kSouth, (buttons2 & 0x40) != 0);
    set_button(state.gamepad, kWest, (buttons2 & 0x80) != 0);

    set_extra(state, kExtraSystemButton, (buttons3 & 0x01) != 0);
    set_extra(state, kExtraTouchpadButton, (buttons3 & 0x02) != 0);

    state.gamepad.leftStick = {normalize_byte_axis(payload[6], false),
                               normalize_byte_axis(payload[7], true)};
    state.gamepad.rightStick = {normalize_byte_axis(payload[8], false),
                                normalize_byte_axis(payload[9], true)};
    state.gamepad.leftTrigger = normalize_trigger(payload[18]);
    state.gamepad.rightTrigger = normalize_trigger(payload[19]);
    out = state;
    return true;
}

bool decode_dualshock4(const uint8_t* data, std::size_t size,
                       HidGamepadState& out) {
    if (data == nullptr || size < 9)
        return false;

    const uint8_t* generic = nullptr;
    const uint8_t* common = nullptr;
    bool fullReport = false;
    bool bluetooth = false;
    if (data[0] == 0x01 && size >= 10) {
        generic = data + 1;
        common = data + 1;
        fullReport = size >= 33;
    } else if (data[0] >= 0x11 && data[0] <= 0x19 && size >= 12 &&
               (data[1] & 0x80) != 0) {
        generic = data + 3;
        common = data + 3;
        fullReport = size >= 35;
        bluetooth = true;
    } else if (size == 9) {
        generic = data;
    } else {
        return false;
    }

    if (bluetooth && size >= 78 &&
        !validate_dualshock4_input_report(data, size))
        return false;

    HidGamepadState state{};
    state.gamepad.leftStick = {normalize_byte_axis(generic[0], false),
                               normalize_byte_axis(generic[1], true)};
    state.gamepad.rightStick = {normalize_byte_axis(generic[2], false),
                                normalize_byte_axis(generic[3], true)};
    apply_dualshock_buttons(state, generic[4], generic[5], generic[6],
                            generic[7], generic[8], false);

    if (fullReport) {
        apply_dual_shock_motion(state, common, true);
        const std::size_t touchOffset = bluetooth ? 37u : 35u;
        if (size >= touchOffset + 8) {
            set_touch_point(state, 0, data[touchOffset],
                            data + touchOffset, 1920.0f, 942.0f);
            set_touch_point(state, 1, data[touchOffset + 4],
                            data + touchOffset + 4, 1920.0f, 942.0f);
        }
    }

    out = state;
    return true;
}

bool decode_dualsense(const uint8_t* data, std::size_t size,
                      HidGamepadState& out) {
    if (data == nullptr || size < 9)
        return false;

    HidGamepadState state{};
    const uint8_t* common = nullptr;
    if (data[0] == 0x01 && size >= 10) {
        if (size == 10 || size == 78) {
            const uint8_t* generic = data + 1;
            state.gamepad.leftStick = {normalize_byte_axis(generic[0], false),
                                       normalize_byte_axis(generic[1], true)};
            state.gamepad.rightStick = {normalize_byte_axis(generic[2], false),
                                        normalize_byte_axis(generic[3], true)};
            apply_dualshock_buttons(state, generic[4], generic[5], generic[6],
                                    generic[7], generic[8], true);
            out = state;
            return true;
        }

        if (size >= 11) {
            state.gamepad.leftStick = {normalize_byte_axis(data[1], false),
                                       normalize_byte_axis(data[2], true)};
            state.gamepad.rightStick = {normalize_byte_axis(data[3], false),
                                        normalize_byte_axis(data[4], true)};
            apply_dualshock_buttons(state, data[8], data[9], data[10],
                                    data[5], data[6], true);
            common = data + 1;
        }
    } else if (data[0] == 0x31 && size >= 12) {
        if (size >= 78 && !validate_dualsense_input_report(data, size))
            return false;
        state.gamepad.leftStick = {normalize_byte_axis(data[2], false),
                                   normalize_byte_axis(data[3], true)};
        state.gamepad.rightStick = {normalize_byte_axis(data[4], false),
                                    normalize_byte_axis(data[5], true)};
        apply_dualshock_buttons(state, data[9], data[10], data[11], data[6],
                                data[7], true);
        common = data + 2;
    } else {
        return false;
    }

    if (common != nullptr &&
        size >= static_cast<std::size_t>(common - data) + 40u) {
        apply_dual_sense_motion(state, common);
        set_touch_point(state, 0, common[32], common + 32, 1920.0f, 1080.0f);
        set_touch_point(state, 1, common[36], common + 36, 1920.0f, 1080.0f);
    }

    out = state;
    return true;
}

bool decode_switch_pro(const uint8_t* data, std::size_t size,
                       HidGamepadState& out) {
    if (data == nullptr || size < 7)
        return false;

    HidGamepadState state{};
    if (data[0] == 0x3F && size >= 12) {
        const uint16_t leftXRaw =
            static_cast<uint16_t>(data[4] | (data[5] << 8));
        const uint16_t leftYRaw =
            static_cast<uint16_t>(data[6] | (data[7] << 8));
        const uint16_t rightXRaw =
            static_cast<uint16_t>(data[8] | (data[9] << 8));
        const uint16_t rightYRaw =
            static_cast<uint16_t>(data[10] | (data[11] << 8));
        apply_switch_common(state, data[1], data[2], data[3],
                            static_cast<float>(leftXRaw >> 8),
                            static_cast<float>(leftYRaw >> 8),
                            static_cast<float>(rightXRaw >> 8),
                            static_cast<float>(rightYRaw >> 8));
        out = state;
        return true;
    }

    if (data[0] == 0x30 && size >= 25) {
        const uint32_t leftX = data[6] | ((data[7] & 0x0F) << 8);
        const uint32_t leftY = ((data[7] & 0xF0) >> 4) | (data[8] << 4);
        const uint32_t rightX = data[9] | ((data[10] & 0x0F) << 8);
        const uint32_t rightY = ((data[10] & 0xF0) >> 4) | (data[11] << 4);

        apply_switch_full_buttons(state, data[3], data[4], data[5]);
        state.gamepad.leftStick = {
            normalize_switch_axis(static_cast<float>(switch_12bit_to_byte(
                                      static_cast<uint16_t>(leftX))), false),
            normalize_switch_axis(static_cast<float>(switch_12bit_to_byte(
                                      static_cast<uint16_t>(leftY))), true)};
        state.gamepad.rightStick = {
            normalize_switch_axis(static_cast<float>(switch_12bit_to_byte(
                                      static_cast<uint16_t>(rightX))), false),
            normalize_switch_axis(static_cast<float>(switch_12bit_to_byte(
                                      static_cast<uint16_t>(rightY))), true)};

        if (size >= 49) {
            const uint8_t* frame = data + 37;
            state.acceleration = {
                sensor_switch_accel(read_le16_signed(frame)),
                sensor_switch_accel(read_le16_signed(frame + 2)),
                sensor_switch_accel(read_le16_signed(frame + 4)),
            };
            state.angularVelocity = {
                sensor_switch_gyro(read_le16_signed(frame + 6)),
                sensor_switch_gyro(read_le16_signed(frame + 8)),
                sensor_switch_gyro(read_le16_signed(frame + 10)),
            };
        }

        out = state;
        return true;
    }

    const uint8_t* report = data;
    if (size == 9)
        report = data + 1;

    if (size == 7 || size == 8 || size == 9) {
        apply_switch_common(state, report[0], report[1], report[2],
                            static_cast<float>(report[3]),
                            static_cast<float>(report[4]),
                            static_cast<float>(report[5]),
                            static_cast<float>(report[6]));
        out = state;
        return true;
    }

    return false;
}

bool decode_steam_controller(const uint8_t* data, std::size_t size,
                             HidGamepadState& out) {
    if (data == nullptr || size < 10 || data[0] != 0x01)
        return false;

    HidGamepadState state{};
    const uint16_t buttons =
        static_cast<uint16_t>(data[2] | (static_cast<uint16_t>(data[3]) << 8));

    set_button(state.gamepad, kSouth, (buttons & 0x0001) != 0);
    set_button(state.gamepad, kEast, (buttons & 0x0002) != 0);
    set_button(state.gamepad, kWest, (buttons & 0x0004) != 0);
    set_button(state.gamepad, kNorth, (buttons & 0x0008) != 0);
    set_button(state.gamepad, kLeftShoulder, (buttons & 0x0010) != 0);
    set_button(state.gamepad, kRightShoulder, (buttons & 0x0020) != 0);
    set_button(state.gamepad, kSelect, (buttons & 0x0400) != 0);
    set_button(state.gamepad, kStart, (buttons & 0x0800) != 0);
    set_extra(state, kExtraLeftGrip, (buttons & 0x0100) != 0);
    set_extra(state, kExtraRightGrip, (buttons & 0x0200) != 0);

    state.gamepad.leftTrigger = normalize_trigger(data[4]);
    state.gamepad.rightTrigger = normalize_trigger(data[5]);
    state.gamepad.leftStick = {normalize_byte_axis(data[6], false),
                               normalize_byte_axis(data[7], true)};
    state.gamepad.rightStick = {normalize_byte_axis(data[8], false),
                                normalize_byte_axis(data[9], true)};
    state.leftTouchpad = state.gamepad.leftStick;
    state.rightTouchpad = state.gamepad.rightStick;
    out = state;
    return true;
}

bool decode_steam_deck(const uint8_t* data, std::size_t size,
                       HidGamepadState& out) {
    if (data == nullptr || size < 56 || data[0] != 0x01 || data[1] != 0x00 ||
        data[2] != 0x09)
        return false;

    HidGamepadState state{};
    const uint8_t buttons0 = data[8];
    const uint8_t buttons1 = data[9];
    const uint8_t buttons2 = data[10];
    const uint8_t buttons3 = data[11];
    const uint8_t buttons4 = data[13];
    const uint8_t buttons5 = data[14];

    set_extra(state, kExtraRightTriggerButton, (buttons0 & 0x01) != 0);
    set_extra(state, kExtraLeftTriggerButton, (buttons0 & 0x02) != 0);
    set_button(state.gamepad, kRightShoulder, (buttons0 & 0x04) != 0);
    set_button(state.gamepad, kLeftShoulder, (buttons0 & 0x08) != 0);
    set_button(state.gamepad, kNorth, (buttons0 & 0x10) != 0);
    set_button(state.gamepad, kSouth, (buttons0 & 0x20) != 0);
    set_button(state.gamepad, kWest, (buttons0 & 0x40) != 0);
    set_button(state.gamepad, kEast, (buttons0 & 0x80) != 0);

    set_button(state.gamepad, kSelect, (buttons1 & 0x10) != 0);
    set_extra(state, kExtraHome, (buttons1 & 0x20) != 0);
    set_button(state.gamepad, kStart, (buttons1 & 0x40) != 0);
    set_extra(state, kExtraLeftGrip, (buttons1 & 0x80) != 0);
    set_extra(state, kExtraRightGrip, (buttons2 & 0x01) != 0);
    set_button(state.gamepad, kLeftStickButton, (buttons2 & 0x40) != 0);
    set_button(state.gamepad, kRightStickButton, (buttons3 & 0x04) != 0);
    set_extra(state, kExtraLeftBack, (buttons4 & 0x02) != 0);
    set_extra(state, kExtraRightBack, (buttons4 & 0x04) != 0);
    set_extra(state, kExtraSystemButton, (buttons5 & 0x04) != 0);

    set_button(state.gamepad, kDpadUp, (buttons1 & 0x01) != 0);
    set_button(state.gamepad, kDpadRight, (buttons1 & 0x02) != 0);
    set_button(state.gamepad, kDpadLeft, (buttons1 & 0x04) != 0);
    set_button(state.gamepad, kDpadDown, (buttons1 & 0x08) != 0);

    auto axis = [](const uint8_t* raw, bool invert) {
        const float value =
            static_cast<float>(read_le16_signed(raw)) / 32767.0f;
        const float clamped = std::clamp(value, -1.0f, 1.0f);
        return invert ? -clamped : clamped;
    };
    state.gamepad.leftStick = {axis(data + 48, false), axis(data + 50, true)};
    state.gamepad.rightStick = {axis(data + 52, false), axis(data + 54, true)};
    state.gamepad.leftTrigger = std::clamp(
        static_cast<float>(read_le16_signed(data + 44)) / 32767.0f, 0.0f,
        1.0f);
    state.gamepad.rightTrigger = std::clamp(
        static_cast<float>(read_le16_signed(data + 46)) / 32767.0f, 0.0f,
        1.0f);

    const bool leftTouched = (buttons2 & 0x08) != 0;
    const bool rightTouched = (buttons2 & 0x10) != 0;
    if (leftTouched) {
        state.leftTouchpad = {axis(data + 16, false), axis(data + 18, true)};
        set_extra(state, kExtraTouchpadContact1, true);
    }
    if (rightTouched) {
        state.rightTouchpad = {axis(data + 20, false), axis(data + 22, true)};
        set_extra(state, kExtraTouchpadContact2, true);
    }

    state.angularVelocity = {
        axis(data + 24, false), axis(data + 28, false), axis(data + 32, false)};
    state.acceleration = {
        axis(data + 26, false), axis(data + 30, false), axis(data + 34, false)};
    out = state;
    return true;
}

uint32_t hid_crc32(uint8_t seed, const uint8_t* data, std::size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    auto update = [&crc](uint8_t value) {
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    };
    update(seed);
    for (std::size_t index = 0; index < size; ++index)
        update(data[index]);
    return ~crc;
}

bool validate_bluetooth_crc(const uint8_t* data, std::size_t size,
                            uint8_t seed) {
    if (data == nullptr || size < 5)
        return false;
    const uint32_t expected = read_le32(data + size - 4);
    return hid_crc32(seed, data, size - 4) == expected;
}

bool validate_dualshock4_input_report(const uint8_t* data, std::size_t size) {
    if (data == nullptr || size < 78 || data[0] < 0x11 || data[0] > 0x19)
        return false;
    return validate_bluetooth_crc(data, size, 0xA1);
}

bool validate_dualsense_input_report(const uint8_t* data, std::size_t size) {
    if (data == nullptr || size < 78 || data[0] != 0x31)
        return false;
    return validate_bluetooth_crc(data, size, 0xA1);
}

void write_crc(uint8_t* data, std::size_t size) {
    if (size < 4)
        return;
    const uint32_t crc = hid_crc32(0xA2, data, size - 4);
    data[size - 4] = static_cast<uint8_t>(crc & 0xFFu);
    data[size - 3] = static_cast<uint8_t>((crc >> 8) & 0xFFu);
    data[size - 2] = static_cast<uint8_t>((crc >> 16) & 0xFFu);
    data[size - 1] = static_cast<uint8_t>((crc >> 24) & 0xFFu);
}

bool encode_dualshock3_haptic(float lowFrequency, float highFrequency,
                              std::vector<uint8_t>& out,
                              uint32_t outputReportSize) {
    resize_output(out, outputReportSize, 49);
    out[0] = 0x01;
    out[1] = 0xFF;
    out[2] = highFrequency > 0.0f ? 0x01 : 0x00;
    out[3] = 0xFF;
    out[4] = static_cast<uint8_t>(
        std::clamp(lowFrequency, 0.0f, 1.0f) * 255.0f);
    return true;
}

bool encode_dualshock4_haptic(float lowFrequency, float highFrequency,
                              std::vector<uint8_t>& out,
                              uint32_t outputReportSize,
                              const float* lightBarColor, bool bluetooth,
                              uint8_t sequence) {
    if (bluetooth) {
        resize_output(out, 78, 78);
        out[0] = 0x11;
        out[1] = 0xC0;
        out[2] = 0x00;
        out[3] = 0x01;
        out[6] = static_cast<uint8_t>(
            std::clamp(highFrequency, 0.0f, 1.0f) * 255.0f);
        out[7] = static_cast<uint8_t>(
            std::clamp(lowFrequency, 0.0f, 1.0f) * 255.0f);
        if (lightBarColor != nullptr) {
            out[3] |= 0x02;
            out[8] = static_cast<uint8_t>(
                std::clamp(lightBarColor[0], 0.0f, 1.0f) * 255.0f);
            out[9] = static_cast<uint8_t>(
                std::clamp(lightBarColor[1], 0.0f, 1.0f) * 255.0f);
            out[10] = static_cast<uint8_t>(
                std::clamp(lightBarColor[2], 0.0f, 1.0f) * 255.0f);
        }
        (void)sequence;
        write_crc(out.data(), out.size());
        return true;
    }

    resize_output(out, outputReportSize, 32);
    out[0] = 0x05;
    out[1] = 0x01;
    out[4] = static_cast<uint8_t>(
        std::clamp(highFrequency, 0.0f, 1.0f) * 255.0f);
    out[5] = static_cast<uint8_t>(
        std::clamp(lowFrequency, 0.0f, 1.0f) * 255.0f);
    if (lightBarColor != nullptr) {
        out[1] |= 0x02;
        out[6] = static_cast<uint8_t>(
            std::clamp(lightBarColor[0], 0.0f, 1.0f) * 255.0f);
        out[7] = static_cast<uint8_t>(
            std::clamp(lightBarColor[1], 0.0f, 1.0f) * 255.0f);
        out[8] = static_cast<uint8_t>(
            std::clamp(lightBarColor[2], 0.0f, 1.0f) * 255.0f);
    }
    return true;
}

bool encode_dualsense_haptic(float lowFrequency, float highFrequency,
                             std::vector<uint8_t>& out,
                             uint32_t outputReportSize,
                             const float* lightBarColor, bool bluetooth,
                             uint8_t sequence,
                             const uint8_t* leftTriggerEffect,
                             const uint8_t* rightTriggerEffect) {
    resize_output(out, bluetooth ? 78u : outputReportSize, 48);
    if (bluetooth) {
        out[0] = 0x31;
        out[1] = static_cast<uint8_t>((sequence & 0x0Fu) << 4);
        out[2] = 0x10;
    } else {
        out[0] = 0x02;
    }

    const std::size_t common = bluetooth ? 3u : 1u;
    out[common] = 0x01 | 0x02;
    out[common + 1] = 0x04;
    out[common + 2] = static_cast<uint8_t>(
        std::clamp(highFrequency, 0.0f, 1.0f) * 255.0f);
    out[common + 3] = static_cast<uint8_t>(
        std::clamp(lowFrequency, 0.0f, 1.0f) * 255.0f);

    if (lightBarColor != nullptr) {
        out[common + 44] = static_cast<uint8_t>(
            std::clamp(lightBarColor[0], 0.0f, 1.0f) * 255.0f);
        out[common + 45] = static_cast<uint8_t>(
            std::clamp(lightBarColor[1], 0.0f, 1.0f) * 255.0f);
        out[common + 46] = static_cast<uint8_t>(
            std::clamp(lightBarColor[2], 0.0f, 1.0f) * 255.0f);
    }

    if (rightTriggerEffect != nullptr) {
        out[common] |= 0x04;
        std::memcpy(out.data() + common + 10, rightTriggerEffect, 11);
    }
    if (leftTriggerEffect != nullptr) {
        out[common] |= 0x08;
        std::memcpy(out.data() + common + 21, leftTriggerEffect, 11);
    }

    if (bluetooth)
        write_crc(out.data(), out.size());
    return true;
}

bool encode_switch_magic_output(SwitchMagicCommand command,
                                std::vector<uint8_t>& out,
                                bool bluetooth) {
    out.assign(bluetooth ? 49 : 64, 0);
    out[0] = 0x80;
    out[1] = static_cast<uint8_t>(command);
    return true;
}

bool encode_switch_subcommand(SwitchSubcommand subcommand,
                              const uint8_t* payload, std::size_t payloadSize,
                              std::vector<uint8_t>& out, bool bluetooth,
                              uint8_t packetCounter) {
    out.assign(bluetooth ? 49 : 64, 0);
    out[0] = 0x01;
    out[1] = static_cast<uint8_t>(packetCounter & 0x0Fu);
    encode_switch_motor(out.data() + 2, 0.0f);
    encode_switch_motor(out.data() + 6, 0.0f);
    out[10] = static_cast<uint8_t>(subcommand);
    if (payload != nullptr && payloadSize != 0) {
        const std::size_t capacity = out.size() - 11u;
        std::memcpy(out.data() + 11, payload, std::min(payloadSize, capacity));
    }
    return true;
}

bool make_switch_player_lights_payload(uint8_t lights,
                                       std::vector<uint8_t>& out) {
    out.assign(1, lights);
    return true;
}

bool make_switch_home_light_payload(const uint8_t* effect,
                                    std::size_t effectSize,
                                    std::vector<uint8_t>& out) {
    if (effect == nullptr || effectSize != 25)
        return false;
    out.assign(effect, effect + effectSize);
    return true;
}

bool make_switch_imu_sensitivity_payload(
    const SwitchImuSensitivity& sensitivity, std::vector<uint8_t>& out) {
    out.assign({
        sensitivity.gyroRange,
        sensitivity.accelerometerRange,
        sensitivity.gyroPerformanceRate,
        sensitivity.accelerometerFilterBandwidth,
    });
    return true;
}

bool make_switch_spi_read_payload(uint32_t address, uint8_t size,
                                  std::vector<uint8_t>& out) {
    if (size == 0 || size > 0x1D)
        return false;
    out.assign({
        static_cast<uint8_t>(address & 0xFFu),
        static_cast<uint8_t>((address >> 8) & 0xFFu),
        static_cast<uint8_t>((address >> 16) & 0xFFu),
        static_cast<uint8_t>((address >> 24) & 0xFFu),
        size,
    });
    return true;
}

bool make_switch_spi_write_payload(uint32_t address, const uint8_t* data,
                                   uint8_t size, std::vector<uint8_t>& out) {
    if (data == nullptr || size == 0 || size > 0x1D)
        return false;
    out.assign({
        static_cast<uint8_t>(address & 0xFFu),
        static_cast<uint8_t>((address >> 8) & 0xFFu),
        static_cast<uint8_t>((address >> 16) & 0xFFu),
        static_cast<uint8_t>((address >> 24) & 0xFFu),
        size,
    });
    out.insert(out.end(), data, data + size);
    return true;
}

bool make_switch_spi_erase_payload(uint32_t address,
                                   std::vector<uint8_t>& out) {
    out.assign({
        static_cast<uint8_t>(address & 0xFFu),
        static_cast<uint8_t>((address >> 8) & 0xFFu),
        static_cast<uint8_t>((address >> 16) & 0xFFu),
        static_cast<uint8_t>((address >> 24) & 0xFFu),
    });
    return true;
}

bool make_switch_nfc_ir_config_payload(const uint8_t* config,
                                       std::size_t configSize,
                                       std::vector<uint8_t>& out) {
    if (config == nullptr || (configSize != 37 && configSize != 38))
        return false;
    out.assign(config, config + configSize);
    return true;
}

bool make_switch_nfc_ir_state_payload(SwitchNfcIrState state,
                                      std::vector<uint8_t>& out) {
    out.assign(1, static_cast<uint8_t>(state));
    return true;
}

bool encode_switch_haptic(float lowFrequency, float highFrequency,
                          std::vector<uint8_t>& out) {
    out.assign(9, 0);
    out[0] = 0x10;
    encode_switch_motor(out.data() + 1, lowFrequency);
    encode_switch_motor(out.data() + 5, highFrequency);
    return true;
}

bool encode_steam_haptic(float lowFrequency, float highFrequency,
                         std::vector<uint8_t>& out) {
    out.assign(64, 0);
    out[0] = 0x8F;
    out[4] = static_cast<uint8_t>(
        std::clamp(lowFrequency, 0.0f, 1.0f) * 255.0f);
    out[5] = static_cast<uint8_t>(
        std::clamp(highFrequency, 0.0f, 1.0f) * 255.0f);
    return true;
}

bool encode_steam_deck_haptic(float lowFrequency, float highFrequency,
                              std::vector<uint8_t>& out) {
    out.assign(65, 0);
    out[0] = 0x00;
    out[1] = 0xEB;
    out[2] = 0x09;
    out[3] = 0x00;
    const uint16_t left = static_cast<uint16_t>(
        std::clamp(lowFrequency, 0.0f, 1.0f) * 65535.0f);
    const uint16_t right = static_cast<uint16_t>(
        std::clamp(highFrequency, 0.0f, 1.0f) * 65535.0f);
    out[6] = static_cast<uint8_t>(left & 0xFFu);
    out[7] = static_cast<uint8_t>((left >> 8) & 0xFFu);
    out[8] = static_cast<uint8_t>(right & 0xFFu);
    out[9] = static_cast<uint8_t>((right >> 8) & 0xFFu);
    out[10] = 0x02;
    out[11] = 0x00;
    return true;
}

} // namespace insys::hid
