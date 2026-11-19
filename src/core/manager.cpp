#include "insys/manager.h"

#include <algorithm>
#include <cstring>

#include "insys/actions/input_user.h"
#include "insys/device_builder.h"
#include "insys/enhanced_touch.h"
#include "insys/string_utils.h"

namespace insys {

Manager::Manager(IRuntime& runtime, const Settings& settings)
    : m_runtime(&runtime), m_settings(settings), m_updateMask(settings.updateMask) {
    register_default_layouts(m_layouts);
}

Manager::~Manager() {
    InputUser::leave_all(this);
    enhanced_touch::EnhancedTouchSupport::disable_for(this);
}

Device* Manager::add_device(std::unique_ptr<Device> device) {
    if (!device)
        return nullptr;

    if (device->deviceId == 0 || m_byId.find(device->deviceId) != m_byId.end())
        device->deviceId = m_runtime->allocate_device_id();

    Device* raw = device.get();
    raw->deviceIndex = static_cast<int>(m_devices.size());

    m_owned.push_back(std::move(device));
    m_devices.push_back(raw);
    m_byId[raw->deviceId] = raw;

    rebuild_state_buffers();
    notify_device_change(raw, DeviceChange::Added);
    return raw;
}

void Manager::remove_device(Device* device) {
    if (device == nullptr)
        return;

    const auto it = std::find(m_devices.begin(), m_devices.end(), device);
    if (it == m_devices.end())
        return;

    // Monitors point at controls owned by the device. Detach them before
    // destroying the device, otherwise the monitor list contains dangling
    // control pointers during removal.
    m_stateMonitors.remove_for_device(device);

    m_devices.erase(it);
    m_byId.erase(device->deviceId);

    notify_device_change(device, DeviceChange::Removed);

    m_owned.erase(std::remove_if(m_owned.begin(), m_owned.end(),
                                 [&](const std::unique_ptr<Device>& owned) {
                                     return owned.get() == device;
                                 }),
                  m_owned.end());

    for (std::size_t i = 0; i < m_devices.size(); ++i)
        m_devices[i]->deviceIndex = static_cast<int>(i);

    rebuild_state_buffers();
}

Device* Manager::device_by_id(int id) {
    const auto it = m_byId.find(id);
    return it == m_byId.end() ? nullptr : it->second;
}

Device* Manager::device_by_layout(std::string_view layout) {
    for (Device* device : m_devices) {
        if (equals_ignore_case(device->layout.str(), layout))
            return device;
        for (const std::string& candidate : device->layoutLineage) {
            if (equals_ignore_case(candidate, layout))
                return device;
        }
    }
    return nullptr;
}

Device* Manager::create_device(std::string_view layoutName,
                               const DeviceDescription& description) {
    DeviceBuilder builder(m_layouts);
    auto device = builder.build(layoutName, "", m_runtime->allocate_device_id(), description);
    if (!device)
        return nullptr;
    return add_device(std::move(device));
}

long Manager::send_device_command(Device* device, DeviceCommand* command) {
    if (device == nullptr || command == nullptr)
        return -1;
    return m_runtime->device_command(device->deviceId, command);
}

void Manager::queue_event(const low::Event* event) {
    m_queue.push(event);
}

void Manager::update() {
    update(UpdateType::Dynamic);
}

void Manager::update(UpdateType type) {
    if (!has_update(m_updateMask, type))
        return;

    m_currentUpdate = type;
    ++m_updateStepCount;
    m_pendingText.clear();

    m_stateBuffers.switch_to(type);

    for (std::size_t i = 0; i < m_beforeUpdateListeners.size(); ++i) {
        IBeforeUpdateListener* listener = m_beforeUpdateListeners[i];
        if (listener != nullptr &&
            std::find(m_beforeUpdateListeners.begin(), m_beforeUpdateListeners.end(), listener) !=
                m_beforeUpdateListeners.end())
            listener->on_before_update();
    }

    m_queue.drain_into(m_nativeBytes);
    std::size_t offset = 0;
    while (offset + low::kHeaderSize <= m_nativeBytes.size()) {
        auto* event = reinterpret_cast<low::Event*>(m_nativeBytes.data() + offset);
        if (event->size < low::kHeaderSize)
            break;
        const std::size_t aligned = low::align_up(event->size, low::kEventAlignment);
        if (aligned > m_nativeBytes.size() - offset)
            break;
        process_event(event);
        offset += aligned;
    }
    m_queue.reclaim(std::move(m_nativeBytes));

    m_stateMonitors.process_timeouts(m_runtime->now());

    m_currentUpdate = UpdateType::None;
}

void Manager::process_event(low::Event* event) {
    if (event == nullptr || event->size < low::kHeaderSize)
        return;

    if (event->eventId == 0)
        event->eventId = m_nextEventId++;

    Device* device = nullptr;
    if (event->type == low::kStateEvent.value || event->type == low::kDeltaStateEvent.value ||
        event->type == low::kTextEvent.value ||
        event->type == low::kDeviceConfigurationEvent.value ||
        event->type == low::kDeviceRemoveEvent.value ||
        event->type == low::kDeviceResetEvent.value)
        device = device_by_id(event->deviceId);

    // Unity invokes onEvent before processing the event and lets a listener
    // suppress the state/action update by marking the event handled.
    notify_event(*event, device);
    if (low::event_handled(event))
        return;

    switch (event->type) {
    case low::kStateEvent.value:
    case low::kDeltaStateEvent.value:
        update_state(device, event, m_currentUpdate);
        break;
    case low::kTextEvent.value: {
        if (event->size < sizeof(low::TextEvent))
            break;
        auto* textEvent = reinterpret_cast<low::TextEvent*>(event);
        m_pendingText.push_back(static_cast<char32_t>(textEvent->character));
        break;
    }
    case low::kDeviceConfigurationEvent.value:
        notify_device_change(device, DeviceChange::ConfigurationChanged);
        break;
    case low::kDeviceRemoveEvent.value:
        notify_device_change(device, DeviceChange::Disconnected);
        break;
    case low::kDeviceResetEvent.value: {
        if (event->size < sizeof(low::DeviceResetEvent))
            break;
        auto* resetEvent = reinterpret_cast<low::DeviceResetEvent*>(event);
        notify_device_change(device, resetEvent->hardReset ? DeviceChange::HardReset
                                                           : DeviceChange::SoftReset);
        break;
    }
    default:
        break;
    }
}

bool Manager::update_state(Device* device, low::Event* stateEvent, UpdateType type) {
    (void)type;
    if (device == nullptr || stateEvent == nullptr)
        return false;

    if (!flip_buffers_for_device_if_necessary(device))
        return false;

    uint8_t* destination = device->currentStatePtr;
    if (destination == nullptr)
        return false;

    if (stateEvent->type == low::kStateEvent.value) {
        const auto* event = reinterpret_cast<const low::StateEvent*>(stateEvent);
        const uint32_t payloadOffset = static_cast<uint32_t>(offsetof(low::StateEvent, stateData));
        const uint32_t payloadSize = stateEvent->size > payloadOffset
                                         ? stateEvent->size - payloadOffset
                                         : 0;
        const uint32_t copySize = std::min(payloadSize, device->state_size_bytes());
        if (copySize > 0)
            std::memcpy(destination, event->stateData, copySize);
    } else if (stateEvent->type == low::kDeltaStateEvent.value) {
        if (stateEvent->size < offsetof(low::DeltaStateEvent, deltaState))
            return false;
        const auto* event = reinterpret_cast<const low::DeltaStateEvent*>(stateEvent);
        const uint32_t payloadOffset =
            static_cast<uint32_t>(offsetof(low::DeltaStateEvent, deltaState));
        const uint32_t stateSize = device->state_size_bytes();
        if (event->stateOffset > stateSize)
            return false;
        const uint32_t payloadSize = stateEvent->size > payloadOffset
                                         ? stateEvent->size - payloadOffset
                                         : 0;
        const uint32_t copySize = std::min(payloadSize, stateSize - event->stateOffset);
        if (copySize > 0)
            std::memcpy(destination + event->stateOffset, event->deltaState, copySize);
    }

    device->lastUpdateTime = stateEvent->time;

    for (std::size_t i = 0; i < m_inputEventListeners.size(); ++i) {
        IInputEventListener* listener = m_inputEventListeners[i];
        if (listener == nullptr ||
            std::find(m_inputEventListeners.begin(), m_inputEventListeners.end(), listener) ==
                m_inputEventListeners.end())
            continue;
        listener->on_input_event(stateEvent);
        if (low::event_handled(stateEvent))
            break;
    }

    if (!low::event_handled(stateEvent))
        m_stateMonitors.process(device, stateEvent->time, stateEvent);
    return true;
}

bool Manager::update_state(Device* device, UpdateType type, const void* state,
                           uint32_t offset, uint32_t size, double time,
                           const low::Event* event) {
    (void)type;
    (void)event;
    if (device == nullptr || state == nullptr)
        return false;

    if (!flip_buffers_for_device_if_necessary(device))
        return false;

    if (device->currentStatePtr == nullptr)
        return false;

    const uint32_t deviceSize = device->state_size_bytes();
    if (offset > deviceSize)
        return false;
    const uint32_t copySize = std::min(size, deviceSize - offset);
    if (copySize > 0)
        std::memcpy(device->currentStatePtr + offset, state, copySize);

    device->lastUpdateTime = time;
    m_stateMonitors.process(device, time, nullptr);
    return true;
}

void Manager::add_state_change_monitor(Control* control, IStateChangeMonitor* monitor,
                                       int64_t monitorIndex, int timerIndex) {
    m_stateMonitors.add(control, monitor, m_runtime->now(), monitorIndex, timerIndex);
}

void Manager::add_state_change_monitor_timeout(Control* control, IStateChangeMonitor* monitor,
                                               double time, int64_t monitorIndex,
                                               int timerIndex) {
    m_stateMonitors.add_timeout(control, monitor, time, monitorIndex, timerIndex);
}

void Manager::remove_state_change_monitor(Control* control, int64_t monitorIndex,
                                          int timerIndex) {
    m_stateMonitors.remove(control, monitorIndex, timerIndex);
}

void Manager::remove_state_change_monitor_timeout(IStateChangeMonitor* monitor,
                                                  int64_t monitorIndex, int timerIndex) {
    m_stateMonitors.remove_timeout(monitor, monitorIndex, timerIndex);
}

void Manager::signal_state_change_monitor(Control* control, IStateChangeMonitor* monitor,
                                          int64_t monitorIndex, double time) {
    m_stateMonitors.signal(control, monitor, monitorIndex, time, nullptr);
}

void Manager::add_before_update_listener(IBeforeUpdateListener* listener) {
    if (listener == nullptr)
        return;
    if (std::find(m_beforeUpdateListeners.begin(), m_beforeUpdateListeners.end(), listener) ==
        m_beforeUpdateListeners.end())
        m_beforeUpdateListeners.push_back(listener);
}

void Manager::remove_before_update_listener(IBeforeUpdateListener* listener) {
    m_beforeUpdateListeners.erase(
        std::remove(m_beforeUpdateListeners.begin(), m_beforeUpdateListeners.end(), listener),
        m_beforeUpdateListeners.end());
}

void Manager::add_input_event_listener(IInputEventListener* listener) {
    if (listener == nullptr)
        return;
    if (std::find(m_inputEventListeners.begin(), m_inputEventListeners.end(), listener) ==
        m_inputEventListeners.end())
        m_inputEventListeners.push_back(listener);
}

void Manager::remove_input_event_listener(IInputEventListener* listener) {
    m_inputEventListeners.erase(
        std::remove(m_inputEventListeners.begin(), m_inputEventListeners.end(), listener),
        m_inputEventListeners.end());
}

int Manager::add_event_listener(EventListener listener) {
    if (!listener)
        return 0;
    const int id = m_nextEventListenerId++;
    m_eventListeners.push_back({id, std::move(listener), true});
    return id;
}

void Manager::remove_event_listener(int listenerId) {
    for (EventListenerEntry& entry : m_eventListeners) {
        if (entry.id == listenerId) {
            entry.active = false;
            entry.callback = {};
            break;
        }
    }
    if (m_listenerNotifyDepth == 0)
        prune_inactive_listeners();
}

int Manager::add_device_change_listener(DeviceChangeListener listener) {
    if (!listener)
        return 0;
    const int id = m_nextDeviceListenerId++;
    m_deviceListeners.push_back({id, std::move(listener), true});
    return id;
}

void Manager::remove_device_change_listener(int listenerId) {
    for (DeviceListenerEntry& entry : m_deviceListeners) {
        if (entry.id == listenerId) {
            entry.active = false;
            entry.callback = {};
            break;
        }
    }
    if (m_listenerNotifyDepth == 0)
        prune_inactive_listeners();
}

int Manager::add_action_change_listener(ActionChangeListener listener) {
    if (!listener)
        return 0;
    const int id = m_nextActionListenerId++;
    m_actionListeners.push_back({id, std::move(listener), true});
    return id;
}

void Manager::remove_action_change_listener(int listenerId) {
    for (ActionListenerEntry& entry : m_actionListeners) {
        if (entry.id == listenerId) {
            entry.active = false;
            entry.callback = {};
            break;
        }
    }
    if (m_listenerNotifyDepth == 0)
        prune_inactive_listeners();
}

void Manager::notify_event(low::Event& event, Device* device) {
    if (m_eventListeners.empty())
        return;

    ++m_listenerNotifyDepth;
    for (std::size_t i = 0; i < m_eventListeners.size(); ++i) {
        if (i >= m_eventListeners.size() || !m_eventListeners[i].active)
            continue;
        EventListener& callback = m_eventListeners[i].callback;
        if (callback)
            callback(event, device);
    }
    --m_listenerNotifyDepth;
    if (m_listenerNotifyDepth == 0)
        prune_inactive_listeners();
}

void Manager::notify_device_change(Device* device, DeviceChange change) {
    if (m_deviceListeners.empty())
        return;

    ++m_listenerNotifyDepth;
    for (std::size_t i = 0; i < m_deviceListeners.size(); ++i) {
        if (i >= m_deviceListeners.size() || !m_deviceListeners[i].active)
            continue;
        DeviceChangeListener& callback = m_deviceListeners[i].callback;
        if (callback)
            callback(device, change);
    }
    --m_listenerNotifyDepth;
    if (m_listenerNotifyDepth == 0)
        prune_inactive_listeners();
}

void Manager::notify_device_change_by_id(int deviceId, DeviceChange change) {
    notify_device_change(device_by_id(deviceId), change);
}

void Manager::notify_action_change(ActionDef* action, ActionChange change) {
    if (m_actionListeners.empty())
        return;

    ++m_listenerNotifyDepth;
    for (std::size_t i = 0; i < m_actionListeners.size(); ++i) {
        if (i >= m_actionListeners.size() || !m_actionListeners[i].active)
            continue;
        ActionChangeListener& callback = m_actionListeners[i].callback;
        if (callback)
            callback(action, change);
    }
    --m_listenerNotifyDepth;
    if (m_listenerNotifyDepth == 0)
        prune_inactive_listeners();
}

void Manager::prune_inactive_listeners() {
    m_eventListeners.erase(
        std::remove_if(m_eventListeners.begin(), m_eventListeners.end(),
                       [](const EventListenerEntry& entry) { return !entry.active; }),
        m_eventListeners.end());
    m_deviceListeners.erase(
        std::remove_if(m_deviceListeners.begin(), m_deviceListeners.end(),
                       [](const DeviceListenerEntry& entry) { return !entry.active; }),
        m_deviceListeners.end());
    m_actionListeners.erase(
        std::remove_if(m_actionListeners.begin(), m_actionListeners.end(),
                       [](const ActionListenerEntry& entry) { return !entry.active; }),
        m_actionListeners.end());
}
bool Manager::flip_buffers_for_device_if_necessary(Device* device) {
    if (device->deviceIndex < 0)
        return false;
    if (device->lastUpdateStepCount == m_updateStepCount)
        return false;

    m_stateBuffers.swap(device->deviceIndex);
    device->lastUpdateStepCount = m_updateStepCount;
    device->currentStatePtr = m_stateBuffers.front_for(device->deviceIndex);
    device->previousStatePtr = m_stateBuffers.back_for(device->deviceIndex);
    return true;
}

void Manager::rebuild_state_buffers() {
    std::unordered_map<int, std::vector<uint8_t>> previousStates;
    for (Device* device : m_devices) {
        if (device->currentStatePtr != nullptr && device->state_size_bytes() > 0) {
            std::vector<uint8_t>& state = previousStates[device->deviceId];
            state.assign(device->currentStatePtr,
                         device->currentStatePtr + device->state_size_bytes());
        }
    }

    m_stateBuffers.allocate_all(m_devices);
    for (Device* device : m_devices) {
        device->currentStatePtr = m_stateBuffers.front_for(device->deviceIndex);
        device->previousStatePtr = m_stateBuffers.back_for(device->deviceIndex);
        device->lastUpdateStepCount = 0;

        const auto it = previousStates.find(device->deviceId);
        if (it != previousStates.end() && device->currentStatePtr != nullptr) {
            const std::size_t copySize =
                std::min(it->second.size(), static_cast<std::size_t>(device->state_size_bytes()));
            if (copySize > 0)
                std::memcpy(device->currentStatePtr, it->second.data(), copySize);
        }
    }
}

} // namespace insys




