#include "insys/backend/evdev_device.h"

#include <cstddef>

namespace insys::backend::evdev {

EvdevDeviceState::EvdevDeviceState(EvdevDeviceType type) : m_type(type) {}

bool EvdevDeviceState::process(uint16_t eventType, uint16_t code, int32_t value) {
    if (eventType == kEvSyn && code == kSynReport)
        return false;

    bool changed = false;
    if (m_type == EvdevDeviceType::Keyboard && eventType == kEvKey) {
        Key key = Key::A;
        if (map_key(code, key)) {
            const auto bit = static_cast<std::uint32_t>(key);
            const std::uint8_t mask = static_cast<std::uint8_t>(1u << (bit % 8));
            std::uint8_t& byte = m_keyBits[bit / 8];
            const bool down = value != 0;
            const bool wasDown = (byte & mask) != 0;
            if (down)
                byte |= mask;
            else
                byte &= static_cast<std::uint8_t>(~mask);
            changed = down != wasDown;
        }
    } else if (m_type == EvdevDeviceType::Mouse) {
        changed = m_mouse.process(eventType, code, value);
    } else if (m_type == EvdevDeviceType::Gamepad) {
        changed = m_gamepad.process(eventType, code, value);
    }

    m_dirty = m_dirty || changed;
    return changed;
}

bool EvdevDeviceState::end_frame() {
    const bool dirty = m_dirty;
    m_dirty = false;
    return dirty;
}

void EvdevDeviceState::reset() {
    m_keyBits.fill(0);
    m_mouse.reset();
    m_gamepad.reset();
    m_dirty = false;
}

} // namespace insys::backend::evdev
