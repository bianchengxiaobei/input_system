#include "insys/backend/evdev_backend.h"

#include <algorithm>
#include <string>
#include <utility>

#include "insys/low/state_format.h"

namespace insys::backend {
namespace {

using evdev::EvdevCaptureSession;
using evdev::EvdevDeviceInfo;
using evdev::EvdevDeviceState;
using evdev::EvdevDeviceType;

const char* layout_name(EvdevDeviceType type) {
    switch (type) {
    case EvdevDeviceType::Keyboard: return "Keyboard";
    case EvdevDeviceType::Mouse: return "Mouse";
    case EvdevDeviceType::Gamepad: return "Gamepad";
    default: return nullptr;
    }
}

bool known_type(EvdevDeviceType type) {
    return layout_name(type) != nullptr;
}

} // namespace

bool EvdevBackend::initialize(Runtime& runtime, Manager& manager) {
    if (m_runtime != nullptr)
        shutdown();
    m_runtime = &runtime;
    m_manager = &manager;
    m_tracker.clear();
    m_capture.clear();
    m_slots.clear();
    return true;
}

void EvdevBackend::shutdown() {
    for (const auto& entry : m_slots) {
        if (m_manager != nullptr) {
            if (Device* device = m_manager->device_by_id(entry.second.deviceId))
                m_manager->remove_device(device);
        }
    }
    m_slots.clear();
    m_capture.clear();
    m_tracker.clear();
    m_runtime = nullptr;
    m_manager = nullptr;
}

void EvdevBackend::update_devices(std::vector<EvdevDeviceInfo> discovered,
                                  const SourceFactory& sourceFactory) {
    if (m_runtime == nullptr || m_manager == nullptr)
        return;

    struct ReadyDevice {
        EvdevDeviceInfo info;
        EvdevCaptureSession::ReadFunction read;
    };

    std::vector<ReadyDevice> ready;
    ready.reserve(discovered.size());
    std::vector<EvdevDeviceInfo> available;
    available.reserve(discovered.size());
    for (EvdevDeviceInfo& info : discovered) {
        if (info.path.empty() || !known_type(info.type))
            continue;
        const auto slotIt = m_slots.find(info.path);
        if (slotIt != m_slots.end() && slotIt->second.type == info.type) {
            available.push_back(info);
            continue;
        }
        auto read = sourceFactory(info);
        if (!read)
            continue;
        available.push_back(info);
        ready.push_back({std::move(info), std::move(read)});
    }

    const auto changes = m_tracker.update(std::move(available));
    for (const auto& change : changes) {
        if (change.kind == evdev::EvdevChangeKind::Removed)
            remove_slot(change.path);
    }

    for (const auto& change : changes) {
        if (change.kind != evdev::EvdevChangeKind::Added)
            continue;

        const auto readyIt = std::find_if(
            ready.begin(), ready.end(), [&change](const ReadyDevice& device) {
                return device.info.path == change.path && device.info.type == change.type;
            });
        if (readyIt == ready.end())
            continue;

        if (!m_capture.add_device(change.path, change.type, readyIt->read))
            continue;

        DeviceDescription description;
        description.interfaceName = "Evdev";
        description.deviceClass = layout_name(change.type);
        description.product = std::string("Evdev ") + layout_name(change.type);
        description.serial = change.path;

        Device* device = m_manager->create_device(layout_name(change.type), description);
        if (device == nullptr) {
            m_capture.remove_device(change.path);
            continue;
        }

        m_slots[change.path] = Slot{change.type, device->deviceId};
    }
}

void EvdevBackend::poll() {
    if (m_runtime == nullptr || m_manager == nullptr)
        return;

    m_capture.poll();
    const auto dirty = m_capture.take_dirty_devices();
    for (const std::string& path : dirty) {
        const auto slotIt = m_slots.find(path);
        if (slotIt == m_slots.end())
            continue;
        EvdevDeviceState* state = m_capture.device_state_mutable(path);
        if (state == nullptr)
            continue;
        emit_state(path, slotIt->second.type, *state);
        if (slotIt->second.type == evdev::EvdevDeviceType::Mouse)
            state->clear_mouse_delta();
    }
}

void EvdevBackend::remove_slot(std::string_view path) {
    const auto it = m_slots.find(std::string(path));
    if (it == m_slots.end())
        return;
    if (m_manager != nullptr) {
        if (Device* device = m_manager->device_by_id(it->second.deviceId))
            m_manager->remove_device(device);
    }
    m_capture.remove_device(path);
    m_slots.erase(it);
}


void EvdevBackend::emit_state(std::string_view path, EvdevDeviceType type,
                              const EvdevDeviceState& state) {
    if (m_runtime == nullptr || m_manager == nullptr)
        return;
    const auto slotIt = m_slots.find(std::string(path));
    if (slotIt == m_slots.end())
        return;

    const int deviceId = slotIt->second.deviceId;
    switch (type) {
    case EvdevDeviceType::Keyboard: {
        const auto& bits = state.keyboard_bits();
        const auto buffer = low::make_state_event(
            kFormatKeyboard, bits.data(), static_cast<uint32_t>(bits.size()),
            deviceId, m_runtime->now());
        m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
        break;
    }
    case EvdevDeviceType::Mouse: {
        const MouseState mouse = state.mouse();
        const auto buffer = low::make_state_event(
            kFormatMouse, &mouse, static_cast<uint32_t>(sizeof(mouse)), deviceId,
            m_runtime->now());
        m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
        break;
    }
    case EvdevDeviceType::Gamepad: {
        const GamepadState& gamepad = state.gamepad();
        const auto buffer = low::make_state_event(
            kFormatGamepad, &gamepad, static_cast<uint32_t>(sizeof(gamepad)),
            deviceId, m_runtime->now());
        m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
        break;
    }
    default:
        break;
    }
}

} // namespace insys::backend



