#include "insys/ui/ui_pointer_input.h"

#include <algorithm>
#include <cstring>

namespace insys::ui {
namespace {

constexpr float kContactThreshold = 0.0f;

TouchPointState read_touch_point(const Touchscreen& screen, int index) {
    TouchPointState point{};
    const uint8_t* state = screen.current_state();
    Control* group = screen.touch(index);
    if (state == nullptr || group == nullptr)
        return point;

    const uint32_t offset = group->stateBlock.effective_byte_offset();
    if (offset + sizeof(point) <= screen.state_size_bytes())
        std::memcpy(&point, state + offset, sizeof(point));
    return point;
}

bool touch_is_pressed(const TouchPointState& point) {
    const auto phase = static_cast<TouchPhase>(static_cast<int>(point.phase));
    if (phase == TouchPhase::Ended || phase == TouchPhase::Canceled)
        return false;
    return point.press > kContactThreshold;
}

} // namespace

UiPointerInputModule::UiPointerInputModule(Manager& manager) : m_manager(&manager) {
    sync_devices();
}

void UiPointerInputModule::set_touchscreen(Touchscreen* touchscreen) {
    if (m_touchscreen == touchscreen)
        return;
    reset();
    m_touchscreen = touchscreen;
}

void UiPointerInputModule::set_mouse(Mouse* mouse) {
    if (m_mouse == mouse)
        return;
    reset();
    m_mouse = mouse;
}

void UiPointerInputModule::set_touch_enabled(bool enabled) {
    if (m_touchEnabled == enabled)
        return;
    if (!enabled)
        reset();
    m_touchEnabled = enabled;
}

void UiPointerInputModule::set_mouse_enabled(bool enabled) {
    if (m_mouseEnabled == enabled)
        return;
    if (!enabled)
        reset();
    m_mouseEnabled = enabled;
}

void UiPointerInputModule::add_target(UiPointerTarget* target) {
    if (target == nullptr)
        return;
    if (std::find(m_targets.begin(), m_targets.end(), target) != m_targets.end())
        return;
    m_targets.push_back(target);
}

void UiPointerInputModule::remove_target(UiPointerTarget* target) {
    if (target == nullptr)
        return;

    for (auto it = m_captured.begin(); it != m_captured.end();) {
        if (it->second == target) {
            if (it->first >= 0 && it->first < kMaxTouches)
                m_touchDown[static_cast<std::size_t>(it->first)] = false;
            else if (it->first == kMousePointerId)
                m_mouseDown = false;
            m_lastPositions.erase(it->first);
            it = m_captured.erase(it);
        } else {
            ++it;
        }
    }

    m_targets.erase(std::remove(m_targets.begin(), m_targets.end(), target),
                    m_targets.end());
}

void UiPointerInputModule::clear_targets() {
    m_captured.clear();
    m_lastPositions.clear();
    m_touchDown.fill(false);
    m_mouseDown = false;
    m_targets.clear();
}

void UiPointerInputModule::update() {
    if (m_manager == nullptr)
        return;

    sync_devices();

    if (m_touchEnabled && m_touchscreen != nullptr) {
        const uint8_t* state = m_touchscreen->current_state();
        if (state != nullptr) {
            for (int index = 0; index < kMaxTouches; ++index) {
                const TouchPointState point = read_touch_point(*m_touchscreen, index);
                const bool pressed = touch_is_pressed(point);
                if (pressed) {
                    if (!m_touchDown[static_cast<std::size_t>(index)]) {
                        m_touchDown[static_cast<std::size_t>(index)] = true;
                        m_touchLastPositions[static_cast<std::size_t>(index)] =
                            point.position;
                        dispatch_down(index, point.position, true, 0);
                    } else if (point.position !=
                               m_touchLastPositions[static_cast<std::size_t>(index)]) {
                        m_touchLastPositions[static_cast<std::size_t>(index)] =
                            point.position;
                        dispatch_move(index, point.position, true, 0);
                    }
                } else if (m_touchDown[static_cast<std::size_t>(index)]) {
                    m_touchDown[static_cast<std::size_t>(index)] = false;
                    dispatch_up(index, point.position, true, 0);
                }
            }
        }
    }

    if (m_mouseEnabled && m_mouse != nullptr) {
        const uint8_t* state = m_mouse->current_state();
        if (state != nullptr && m_mouse->position() != nullptr &&
            m_mouse->left_button() != nullptr) {
            const Vector2 position = m_mouse->position()->read_vector2(state);
            const bool pressed = m_mouse->left_button()->read_float(state) > 0.0f;
            if (pressed) {
                if (!m_mouseDown) {
                    m_mouseDown = true;
                    m_mouseLastPosition = position;
                    dispatch_down(kMousePointerId, position, false, 0);
                } else if (position != m_mouseLastPosition) {
                    m_mouseLastPosition = position;
                    dispatch_move(kMousePointerId, position, false, 0);
                }
            } else if (m_mouseDown) {
                m_mouseDown = false;
                dispatch_up(kMousePointerId, position, false, 0);
            }
        }
    }

}

void UiPointerInputModule::reset() {
    for (const auto& [pointerId, target] : m_captured) {
        if (target == nullptr)
            continue;
        const auto position = m_lastPositions.find(pointerId);
        UiPointerEvent event;
        event.pointerId = pointerId;
        event.position = position != m_lastPositions.end() ? position->second
                                                           : Vector2{};
        event.touch = pointerId >= 0;
        event.time = m_manager != nullptr ? m_manager->runtime().now() : 0.0;
        target->on_pointer_up(event);
    }
    m_captured.clear();
    m_lastPositions.clear();
    m_touchDown.fill(false);
    m_touchLastPositions.fill({});
    m_mouseDown = false;
    m_mouseLastPosition = {};
}

UiPointerTarget* UiPointerInputModule::captured_target(int pointerId) const {
    const auto it = m_captured.find(pointerId);
    return it == m_captured.end() ? nullptr : it->second;
}

UiPointerTarget* UiPointerInputModule::find_target(Vector2 position) const {
    for (auto it = m_targets.rbegin(); it != m_targets.rend(); ++it) {
        UiPointerTarget* target = *it;
        if (target != nullptr && target->accepts_pointer(position))
            return target;
    }
    return nullptr;
}

void UiPointerInputModule::sync_devices() {
    if (m_manager == nullptr)
        return;

    if (m_touchscreen != nullptr &&
        m_manager->device_by_id(m_touchscreen->deviceId) != m_touchscreen) {
        reset();
        m_touchscreen = nullptr;
    }
    if (m_mouse != nullptr &&
        m_manager->device_by_id(m_mouse->deviceId) != m_mouse) {
        reset();
        m_mouse = nullptr;
    }

    if (m_touchscreen == nullptr) {
        m_touchscreen = static_cast<Touchscreen*>(
            m_manager->device_by_layout("Touchscreen"));
    }
    if (m_mouse == nullptr)
        m_mouse = static_cast<Mouse*>(m_manager->device_by_layout("Mouse"));
}

void UiPointerInputModule::dispatch_down(int pointerId, Vector2 position,
                                         bool touch, int displayIndex) {
    UiPointerTarget* target = find_target(position);
    if (target == nullptr)
        return;

    m_captured[pointerId] = target;
    m_lastPositions[pointerId] = position;

    UiPointerEvent event;
    event.pointerId = pointerId;
    event.position = position;
    event.touch = touch;
    event.displayIndex = displayIndex;
    event.time = m_manager != nullptr ? m_manager->runtime().now() : 0.0;
    target->on_pointer_down(event);
}

void UiPointerInputModule::dispatch_move(int pointerId, Vector2 position,
                                         bool touch, int displayIndex) {
    const auto it = m_captured.find(pointerId);
    if (it == m_captured.end() || it->second == nullptr)
        return;

    m_lastPositions[pointerId] = position;

    UiPointerEvent event;
    event.pointerId = pointerId;
    event.position = position;
    event.touch = touch;
    event.displayIndex = displayIndex;
    event.time = m_manager != nullptr ? m_manager->runtime().now() : 0.0;
    it->second->on_pointer_move(event);
}

void UiPointerInputModule::dispatch_up(int pointerId, Vector2 position,
                                       bool touch, int displayIndex) {
    const auto it = m_captured.find(pointerId);
    if (it == m_captured.end() || it->second == nullptr)
        return;

    UiPointerTarget* target = it->second;
    m_captured.erase(it);
    m_lastPositions.erase(pointerId);

    UiPointerEvent event;
    event.pointerId = pointerId;
    event.position = position;
    event.touch = touch;
    event.displayIndex = displayIndex;
    event.time = m_manager != nullptr ? m_manager->runtime().now() : 0.0;
    target->on_pointer_up(event);
}

} // namespace insys::ui




