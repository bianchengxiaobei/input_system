#include "insys/backend/touch_backend.h"

#include <algorithm>
#include <utility>

#include "insys/low/event.h"
#include "insys/low/state_format.h"

namespace insys::backend {

bool TouchBackend::initialize(Runtime& runtime, Manager& manager,
                              const DeviceDescription& description) {
    if (m_runtime != nullptr)
        shutdown();
    m_runtime = &runtime;
    m_manager = &manager;
    m_description = description;
    m_deviceId = 0;
    m_state = {};
    m_tracks = {};
    m_slotByTouch.clear();
    m_dirty = false;
    return true;
}

void TouchBackend::shutdown() {
    if (m_manager != nullptr && m_deviceId != 0) {
        if (Device* device = m_manager->device_by_id(m_deviceId))
            m_manager->remove_device(device);
    }
    m_deviceId = 0;
    m_state = {};
    m_tracks = {};
    m_slotByTouch.clear();
    m_dirty = false;
    m_runtime = nullptr;
    m_manager = nullptr;
    m_description = {};
}

bool TouchBackend::touch_began(int touchId, Vector2 position, float pressure) {
    if (!ensure_device())
        return false;
    if (m_slotByTouch.find(touchId) != m_slotByTouch.end())
        return false;

    const int slot = allocate_track(touchId);
    if (slot < 0)
        return false;

    Track& track = m_tracks[static_cast<std::size_t>(slot)];
    TouchPointState& point = m_state.touches[static_cast<std::size_t>(slot)];
    track.start = position;
    track.last = position;
    point = {};
    point.position = position;
    point.startPosition = position;
    point.phase = static_cast<float>(TouchPhase::Began);
    point.press = std::clamp(pressure, 0.0f, 1.0f);
    point.tapCount = 1.0f;
    m_slotByTouch[touchId] = slot;
    m_dirty = true;
    return true;
}

bool TouchBackend::touch_moved(int touchId, Vector2 position, float pressure) {
    const int slot = find_track(touchId);
    if (slot < 0)
        return false;

    Track& track = m_tracks[static_cast<std::size_t>(slot)];
    TouchPointState& point = m_state.touches[static_cast<std::size_t>(slot)];
    point.position = position;
    point.startPosition = track.start;
    point.delta += position - track.last;
    point.phase = static_cast<float>(TouchPhase::Moved);
    point.press = std::clamp(pressure, 0.0f, 1.0f);
    point.tapCount = 1.0f;
    track.last = position;
    m_dirty = true;
    return true;
}

bool TouchBackend::touch_ended(int touchId) {
    const int slot = find_track(touchId);
    if (slot < 0)
        return false;

    Track& track = m_tracks[static_cast<std::size_t>(slot)];
    TouchPointState& point = m_state.touches[static_cast<std::size_t>(slot)];
    point.position = track.last;
    point.startPosition = track.start;
    point.phase = static_cast<float>(TouchPhase::Ended);
    point.press = 0.0f;
    point.tapCount = 0.0f;
    track.active = false;
    m_slotByTouch.erase(touchId);
    m_dirty = true;
    return true;
}

bool TouchBackend::touch_canceled(int touchId) {
    const int slot = find_track(touchId);
    if (slot < 0)
        return false;

    Track& track = m_tracks[static_cast<std::size_t>(slot)];
    TouchPointState& point = m_state.touches[static_cast<std::size_t>(slot)];
    point.phase = static_cast<float>(TouchPhase::Canceled);
    point.press = 0.0f;
    point.tapCount = 0.0f;
    track.active = false;
    m_slotByTouch.erase(touchId);
    m_dirty = true;
    return true;
}

int TouchBackend::cancel_all() {
    int canceled = 0;
    for (int i = 0; i < kMaxTouches; ++i) {
        Track& track = m_tracks[static_cast<std::size_t>(i)];
        if (!track.active)
            continue;
        TouchPointState& point = m_state.touches[static_cast<std::size_t>(i)];
        point.phase = static_cast<float>(TouchPhase::Canceled);
        point.press = 0.0f;
        point.tapCount = 0.0f;
        track.active = false;
        ++canceled;
    }
    if (canceled > 0) {
        m_slotByTouch.clear();
        m_dirty = true;
    }
    return canceled;
}

int TouchBackend::active_touch_count() const {
    int count = 0;
    for (const Track& track : m_tracks) {
        if (track.active)
            ++count;
    }
    return count;
}

void TouchBackend::poll() {
    if (m_runtime == nullptr || m_manager == nullptr || m_deviceId == 0 || !m_dirty)
        return;

    const auto buffer = low::make_state_event(
        kFormatTouchscreen, &m_state, static_cast<uint32_t>(sizeof(m_state)),
        m_deviceId, m_runtime->now());
    m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
    m_dirty = false;
    for (TouchPointState& point : m_state.touches)
        point.delta = {};
}

int TouchBackend::find_track(int touchId) const {
    const auto it = m_slotByTouch.find(touchId);
    return it == m_slotByTouch.end() ? -1 : it->second;
}

int TouchBackend::allocate_track(int touchId) {
    for (int i = 0; i < kMaxTouches; ++i) {
        Track& track = m_tracks[static_cast<std::size_t>(i)];
        if (track.active)
            continue;
        track = {};
        track.externalId = touchId;
        track.active = true;
        return i;
    }
    return -1;
}

bool TouchBackend::ensure_device() {
    if (m_deviceId != 0)
        return true;
    if (m_runtime == nullptr || m_manager == nullptr)
        return false;

    DeviceDescription effective = m_description;
    if (effective.interfaceName.empty())
        effective.interfaceName = "Touchscreen";
    effective.deviceClass = "Touchscreen";
    if (effective.product.empty())
        effective.product = "Touchscreen";
    if (effective.serial.empty())
        effective.serial = "Touchscreen";

    Device* device = m_manager->create_device("Touchscreen", effective);
    if (device == nullptr)
        return false;
    m_deviceId = device->deviceId;
    return true;
}

} // namespace insys::backend

