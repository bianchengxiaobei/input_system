#include "insys/enhanced_touch.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "insys/low/event.h"
#include "insys/low/state_format.h"
#include "insys/manager.h"

namespace insys::enhanced_touch {

TouchSimulation::TouchSimulation(Manager& manager) : m_manager(&manager) {}

TouchSimulation::~TouchSimulation() {
    disable();
}

void TouchSimulation::enable() {
    if (m_enabled)
        return;
    if (m_manager == nullptr)
        return;
    if (!ensure_touchscreen())
        return;

    m_enabled = true;
    m_state = {};
    for (PointerSource& pointer : m_pointers)
        pointer.slot = -1;
}

void TouchSimulation::disable() {
    if (!m_enabled)
        return;

    if (m_touchscreen != nullptr && m_ownsTouchscreen && m_manager != nullptr) {
        if (m_manager->device_by_id(m_touchscreen->deviceId) == m_touchscreen)
            m_manager->remove_device(m_touchscreen);
    }

    m_touchscreen = nullptr;
    m_ownsTouchscreen = false;
    m_enabled = false;
    m_state = {};
    m_pointers.clear();
}

void TouchSimulation::add_pointer(Device* device, Vector2Control* position,
                                  Control* button) {
    if (device == nullptr || position == nullptr || button == nullptr)
        return;
    if (find_pointer(device, button) >= 0)
        return;

    PointerSource source;
    source.device = device;
    source.position = position;
    source.button = button;
    m_pointers.push_back(source);
}

void TouchSimulation::add_default_pointers() {
    if (m_manager == nullptr)
        return;

    if (auto* mouse = dynamic_cast<Mouse*>(m_manager->device_by_layout("Mouse"))) {
        if (mouse->position() != nullptr && mouse->left_button() != nullptr)
            add_pointer(mouse, mouse->position(), mouse->left_button());
    }

    if (auto* pen = dynamic_cast<Pen*>(m_manager->device_by_layout("Pen"))) {
        if (pen->position() != nullptr && pen->tip() != nullptr)
            add_pointer(pen, pen->position(), pen->tip());
    }
}

void TouchSimulation::update() {
    if (!m_enabled || m_manager == nullptr)
        return;
    if (m_pointers.empty())
        add_default_pointers();
    if (!ensure_touchscreen())
        return;

    bool changed = false;
    for (PointerSource& pointer : m_pointers) {
        if (pointer.device == nullptr || pointer.position == nullptr ||
            pointer.button == nullptr)
            continue;

        const uint8_t* sourceState = pointer.device->current_state();
        if (sourceState == nullptr)
            continue;

        const bool pressed = pointer.button->read_float(sourceState) >= 0.5f;
        const Vector2 position = pointer.position->read_vector2(sourceState);

        if (pressed && pointer.slot < 0) {
            const int slot = allocate_slot();
            if (slot < 0)
                continue;

            pointer.slot = slot;
            pointer.startPosition = position;
            pointer.lastPosition = position;

            TouchPointState& point = m_state.touches[static_cast<std::size_t>(slot)];
            point = {};
            point.position = position;
            point.startPosition = position;
            point.phase = static_cast<float>(TouchPhase::Began);
            point.press = 1.0f;
            point.tapCount = 1.0f;
            changed = true;
        } else if (pressed) {
            if (position == pointer.lastPosition)
                continue;

            TouchPointState& point = m_state.touches[static_cast<std::size_t>(pointer.slot)];
            point.position = position;
            point.startPosition = pointer.startPosition;
            point.delta = position - pointer.lastPosition;
            point.phase = static_cast<float>(TouchPhase::Moved);
            point.press = 1.0f;
            point.tapCount = 1.0f;
            pointer.lastPosition = position;
            changed = true;
        } else if (pointer.slot >= 0) {
            TouchPointState& point = m_state.touches[static_cast<std::size_t>(pointer.slot)];
            point.position = pointer.lastPosition;
            point.startPosition = pointer.startPosition;
            point.delta = {};
            point.phase = static_cast<float>(TouchPhase::Ended);
            point.press = 0.0f;
            point.tapCount = 0.0f;
            pointer.slot = -1;
            changed = true;
        }
    }

    if (!changed)
        return;

    const auto event = low::make_state_event(
        kFormatTouchscreen, &m_state, static_cast<uint32_t>(sizeof(m_state)),
        m_touchscreen->deviceId, m_manager->runtime().now());
    m_manager->queue_event(reinterpret_cast<const low::Event*>(event.data()));
}

bool TouchSimulation::ensure_touchscreen() {
    if (m_touchscreen != nullptr && m_manager != nullptr &&
        m_manager->device_by_id(m_touchscreen->deviceId) == m_touchscreen)
        return true;

    if (m_manager == nullptr)
        return false;

    DeviceDescription description;
    description.interfaceName = "Touchscreen";
    description.deviceClass = "Touchscreen";
    description.product = "Simulated Touchscreen";
    description.serial = "Simulated Touchscreen";

    Device* device = m_manager->create_device("Touchscreen", description);
    if (device == nullptr)
        return false;

    m_touchscreen = static_cast<Touchscreen*>(device);
    m_ownsTouchscreen = true;
    return true;
}

int TouchSimulation::find_pointer(Device* device, Control* button) const {
    for (std::size_t i = 0; i < m_pointers.size(); ++i) {
        if (m_pointers[i].device == device && m_pointers[i].button == button)
            return static_cast<int>(i);
    }
    return -1;
}

int TouchSimulation::allocate_slot() const {
    for (int slot = 0; slot < kMaxTouches; ++slot) {
        bool used = false;
        for (const PointerSource& pointer : m_pointers) {
            if (pointer.slot == slot) {
                used = true;
                break;
            }
        }
        if (!used)
            return slot;
    }
    return -1;
}

} // namespace insys::enhanced_touch








