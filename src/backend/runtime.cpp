#include "insys/backend/runtime.h"

#include <chrono>
#include <utility>

#include "insys/manager.h"

namespace insys::backend {

Runtime::Runtime() : m_start(std::chrono::steady_clock::now()) {}

void Runtime::set_manager(Manager* manager) {
    m_manager = manager;
}

int Runtime::allocate_device_id() {
    return m_nextDeviceId.fetch_add(1);
}

void Runtime::queue_event(const low::Event* event) {
    if (m_manager != nullptr)
        m_manager->queue_event(event);
}

long Runtime::device_command(int deviceId, DeviceCommand* command) {
    for (const CommandHandlerEntry& entry : m_deviceCommandHandlers) {
        if (!entry.handler)
            continue;
        const long result = entry.handler(deviceId, command);
        if (result >= 0)
            return result;
    }
    return m_deviceCommandHandler ? m_deviceCommandHandler(deviceId, command) : -1;
}

void Runtime::set_device_command_handler(DeviceCommandHandler handler) {
    m_deviceCommandHandler = std::move(handler);
}

void Runtime::clear_device_command_handler() {
    m_deviceCommandHandler = nullptr;
}

int Runtime::add_device_command_handler(DeviceCommandHandler handler) {
    if (!handler)
        return 0;
    const int id = m_nextDeviceCommandHandlerId++;
    m_deviceCommandHandlers.push_back({id, std::move(handler)});
    return id;
}

void Runtime::remove_device_command_handler(int handlerId) {
    for (auto it = m_deviceCommandHandlers.begin();
         it != m_deviceCommandHandlers.end(); ++it) {
        if (it->id == handlerId) {
            m_deviceCommandHandlers.erase(it);
            return;
        }
    }
}

double Runtime::now() const {
    const auto elapsed = std::chrono::steady_clock::now() - m_start;
    return std::chrono::duration<double>(elapsed).count();
}

double Runtime::now_for_fixed() const {
    return now();
}

float Runtime::unscaled_game_time() const {
    return static_cast<float>(now());
}

} // namespace insys::backend


