#pragma once

#include <memory>
#include <vector>

#include "insys/manager.h"
#include "insys/settings.h"

namespace insys {

class System {
public:
    explicit System(IRuntime& runtime, const Settings& settings = Settings{});
    ~System();

    Manager& manager() { return *m_manager; }
    const Manager& manager() const { return *m_manager; }

    void update(UpdateType type = UpdateType::Dynamic);
    long send_device_command(Device* device, DeviceCommand* command) {
        return m_manager->send_device_command(device, command);
    }

    int add_event_listener(EventListener listener) {
        return m_manager->add_event_listener(std::move(listener));
    }
    void remove_event_listener(int listenerId) {
        m_manager->remove_event_listener(listenerId);
    }
    int add_device_change_listener(DeviceChangeListener listener) {
        return m_manager->add_device_change_listener(std::move(listener));
    }
    void remove_device_change_listener(int listenerId) {
        m_manager->remove_device_change_listener(listenerId);
    }
    int add_action_change_listener(ActionChangeListener listener) {
        return m_manager->add_action_change_listener(std::move(listener));
    }
    void remove_action_change_listener(int listenerId) {
        m_manager->remove_action_change_listener(listenerId);
    }

    const std::vector<char32_t>& pending_text() const { return m_manager->pending_text(); }
    void clear_pending_text() { m_manager->clear_pending_text(); }

private:
    std::unique_ptr<Manager> m_manager;
};

} // namespace insys
