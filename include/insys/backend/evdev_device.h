#pragma once

#include <array>
#include <cstdint>

#include "insys/backend/evdev_mapping.h"

namespace insys::backend::evdev {

enum class EvdevDeviceType {
    Unknown,
    Keyboard,
    Mouse,
    Gamepad,
};

class EvdevDeviceState {
public:
    explicit EvdevDeviceState(EvdevDeviceType type = EvdevDeviceType::Unknown);

    EvdevDeviceType type() const { return m_type; }

    bool process(uint16_t eventType, uint16_t code, int32_t value);
    bool end_frame();
    void reset();

    const std::array<std::uint8_t, (kKeyCount + 7) / 8>& keyboard_bits() const {
        return m_keyBits;
    }
    const MouseState& mouse() const { return m_mouse.state; }
    const GamepadState& gamepad() const { return m_gamepad.state; }

    MouseMapper& mouse_mapper() { return m_mouse; }
    GamepadMapper& gamepad_mapper() { return m_gamepad; }
    void clear_mouse_delta() { m_mouse.state.delta = {}; }

private:
    EvdevDeviceType m_type = EvdevDeviceType::Unknown;
    std::array<std::uint8_t, (kKeyCount + 7) / 8> m_keyBits{};
    MouseMapper m_mouse;
    GamepadMapper m_gamepad;
    bool m_dirty = false;
};

} // namespace insys::backend::evdev
