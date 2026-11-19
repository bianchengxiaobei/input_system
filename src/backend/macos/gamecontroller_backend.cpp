#include "insys/backend/gamecontroller_backend.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "insys/low/event.h"
#include "insys/low/state_format.h"

namespace insys::backend {

bool GameControllerBackend::initialize(Runtime& runtime, Manager& manager) {
    if (m_runtime != nullptr)
        shutdown();
    m_runtime = &runtime;
    m_manager = &manager;
    m_slots.clear();
    m_dirtyControllers.clear();
    return true;
}

void GameControllerBackend::shutdown() {
    if (m_manager != nullptr) {
        for (const auto& entry : m_slots) {
            if (Device* device = m_manager->device_by_id(entry.second.deviceId))
                m_manager->remove_device(device);
        }
    }
    m_slots.clear();
    m_dirtyControllers.clear();
    m_runtime = nullptr;
    m_manager = nullptr;
}

bool GameControllerBackend::add_controller(std::string id,
                                           const DeviceDescription& description) {
    if (m_runtime == nullptr || m_manager == nullptr || id.empty() ||
        m_slots.find(id) != m_slots.end()) {
        return false;
    }

    DeviceDescription effective = description;
    if (effective.interfaceName.empty())
        effective.interfaceName = "GameController";
    effective.deviceClass = "Gamepad";
    if (effective.product.empty())
        effective.product = "Apple Game Controller";
    if (effective.serial.empty())
        effective.serial = id;

    Device* device = m_manager->create_device("Gamepad", effective);
    if (device == nullptr)
        return false;

    m_slots.emplace(std::move(id), Slot{device->deviceId});
    return true;
}

bool GameControllerBackend::remove_controller(std::string_view id) {
    const auto it = m_slots.find(std::string(id));
    if (it == m_slots.end())
        return false;
    if (m_manager != nullptr) {
        if (Device* device = m_manager->device_by_id(it->second.deviceId))
            m_manager->remove_device(device);
    }
    m_dirtyControllers.erase(
        std::remove(m_dirtyControllers.begin(), m_dirtyControllers.end(), id),
        m_dirtyControllers.end());
    m_slots.erase(it);
    return true;
}

bool GameControllerBackend::update_controller(std::string_view id,
                                              const GameControllerState& state) {
    const auto it = m_slots.find(std::string(id));
    if (it == m_slots.end())
        return false;

    const GamepadState mapped = map_gamecontroller_state(state);
    if (std::memcmp(&it->second.state, &mapped, sizeof(mapped)) == 0)
        return false;

    it->second.state = mapped;
    if (!it->second.dirty) {
        it->second.dirty = true;
        m_dirtyControllers.emplace_back(id);
    }
    return true;
}

void GameControllerBackend::poll() {
    if (m_runtime == nullptr || m_manager == nullptr)
        return;

    for (const std::string& id : m_dirtyControllers) {
        const auto it = m_slots.find(id);
        if (it == m_slots.end())
            continue;
        const auto buffer = low::make_state_event(
            kFormatGamepad, &it->second.state,
            static_cast<uint32_t>(sizeof(GamepadState)), it->second.deviceId,
            m_runtime->now());
        m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
        it->second.dirty = false;
    }
    m_dirtyControllers.clear();
}

bool GameControllerBackend::connected(std::string_view id) const {
    return m_slots.find(std::string(id)) != m_slots.end();
}

int GameControllerBackend::device_id(std::string_view id) const {
    const auto it = m_slots.find(std::string(id));
    return it == m_slots.end() ? 0 : it->second.deviceId;
}

} // namespace insys::backend
