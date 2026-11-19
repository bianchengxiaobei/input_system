#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "insys/backend/iruntime.h"
#include "insys/device.h"
#include "insys/input_listeners.h"
#include "insys/layout_registry.h"
#include "insys/low/event.h"
#include "insys/low/event_queue.h"
#include "insys/settings.h"
#include "insys/state_buffers.h"
#include "insys/state_change_monitors.h"

namespace insys {

class IBeforeUpdateListener {
public:
    virtual ~IBeforeUpdateListener() = default;
    virtual void on_before_update() = 0;
};

class IInputEventListener {
public:
    virtual ~IInputEventListener() = default;
    virtual void on_input_event(low::Event* event) = 0;
};

class Manager {
public:
    Manager(IRuntime& runtime, const Settings& settings = Settings{});
    ~Manager();

    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

    const Settings& settings() const { return m_settings; }
    IRuntime& runtime() const { return *m_runtime; }

    const std::vector<Device*>& devices() const { return m_devices; }
    Device* add_device(std::unique_ptr<Device> device);
    void remove_device(Device* device);
    Device* device_by_id(int id);
    Device* device_by_layout(std::string_view layout);
    Device* create_device(std::string_view layoutName, const DeviceDescription& description);
    long send_device_command(Device* device, DeviceCommand* command);

    LayoutRegistry& layouts() { return m_layouts; }
    const LayoutRegistry& layouts() const { return m_layouts; }

    void queue_event(const low::Event* event);
    void update();
    void update(UpdateType type);

    bool update_state(Device* device, low::Event* stateEvent, UpdateType type);
    bool update_state(Device* device, UpdateType type, const void* state,
                      uint32_t offset, uint32_t size, double time,
                      const low::Event* event = nullptr);

    StateBuffers& state_buffers() { return m_stateBuffers; }
    StateChangeMonitors& state_monitors() { return m_stateMonitors; }

    void add_state_change_monitor(Control* control, IStateChangeMonitor* monitor,
                                  int64_t monitorIndex, int timerIndex = 0);
    void add_state_change_monitor_timeout(Control* control, IStateChangeMonitor* monitor,
                                          double time, int64_t monitorIndex, int timerIndex);
    void remove_state_change_monitor(Control* control, int64_t monitorIndex,
                                     int timerIndex = 0);
    void remove_state_change_monitor_timeout(IStateChangeMonitor* monitor,
                                             int64_t monitorIndex, int timerIndex);
    void signal_state_change_monitor(Control* control, IStateChangeMonitor* monitor,
                                     int64_t monitorIndex, double time);
    void add_before_update_listener(IBeforeUpdateListener* listener);
    void remove_before_update_listener(IBeforeUpdateListener* listener);
    void add_input_event_listener(IInputEventListener* listener);
    void remove_input_event_listener(IInputEventListener* listener);

    int add_event_listener(EventListener listener);
    void remove_event_listener(int listenerId);
    int add_device_change_listener(DeviceChangeListener listener);
    void remove_device_change_listener(int listenerId);
    int add_action_change_listener(ActionChangeListener listener);
    void remove_action_change_listener(int listenerId);

    void notify_action_change(ActionDef* action, ActionChange change);

    std::size_t event_listener_count() const { return m_eventListeners.size(); }
    std::size_t device_change_listener_count() const { return m_deviceListeners.size(); }
    std::size_t action_change_listener_count() const { return m_actionListeners.size(); }

    uint32_t update_step_count() const { return m_updateStepCount; }

    const std::vector<char32_t>& pending_text() const { return m_pendingText; }
    void clear_pending_text() { m_pendingText.clear(); }

private:
    struct EventListenerEntry {
        int id = 0;
        EventListener callback;
        bool active = true;
    };
    struct DeviceListenerEntry {
        int id = 0;
        DeviceChangeListener callback;
        bool active = true;
    };
    struct ActionListenerEntry {
        int id = 0;
        ActionChangeListener callback;
        bool active = true;
    };

    void process_event(low::Event* event);
    void notify_event(low::Event& event, Device* device);
    void notify_device_change(Device* device, DeviceChange change);
    void notify_device_change_by_id(int deviceId, DeviceChange change);
    void prune_inactive_listeners();
    bool flip_buffers_for_device_if_necessary(Device* device);
    void rebuild_state_buffers();

    IRuntime* m_runtime = nullptr;
    Settings m_settings;
    LayoutRegistry m_layouts;

    std::vector<std::unique_ptr<Device>> m_owned;
    std::vector<Device*> m_devices;
    std::unordered_map<int, Device*> m_byId;

    UpdateMask m_updateMask = 0;
    UpdateType m_currentUpdate = UpdateType::None;
    uint32_t m_updateStepCount = 0;

    StateBuffers m_stateBuffers;
    StateChangeMonitors m_stateMonitors;
    low::EventQueue m_queue;
    std::vector<uint8_t> m_nativeBytes;
    uint32_t m_nextEventId = 1;

    std::vector<IBeforeUpdateListener*> m_beforeUpdateListeners;
    std::vector<IInputEventListener*> m_inputEventListeners;

    std::vector<EventListenerEntry> m_eventListeners;
    std::vector<DeviceListenerEntry> m_deviceListeners;
    std::vector<ActionListenerEntry> m_actionListeners;
    int m_nextEventListenerId = 1;
    int m_nextDeviceListenerId = 1;
    int m_nextActionListenerId = 1;
    int m_listenerNotifyDepth = 0;

    std::vector<char32_t> m_pendingText;
};

} // namespace insys
