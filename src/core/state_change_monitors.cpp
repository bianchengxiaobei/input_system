#include "insys/state_change_monitors.h"

#include <algorithm>
#include <cmath>

#include "insys/control.h"
#include "insys/device.h"

namespace insys {
namespace {

uint64_t hash_control_value(Control* control, const uint8_t* state) {
    if (control == nullptr || state == nullptr)
        return 0;

    alignas(8) uint8_t buffer[64] = {};
    const std::size_t size =
        std::min<std::size_t>(control->value_size_bytes(), sizeof(buffer));
    control->read_value_from_state(state, buffer);

    uint64_t hash = 1469598103934665603ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= buffer[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

} // namespace

void StateChangeMonitors::add(Control* control, IStateChangeMonitor* monitor, double time,
                              int64_t monitorIndex, int timerIndex) {
    uint64_t hash = 0;
    if (control != nullptr && control->device != nullptr && control->device->current_state() != nullptr)
        hash = hash_control_value(control, control->device->current_state());
    m_monitors.push_back(Monitor{control, monitor, time, monitorIndex, timerIndex, hash});
    if (m_notificationScratch.capacity() < m_monitors.size())
        m_notificationScratch.reserve(m_monitors.size());
}

void StateChangeMonitors::add_timeout(Control* control, IStateChangeMonitor* monitor,
                                      double time, int64_t monitorIndex, int timerIndex) {
    m_timeouts.push_back(Timeout{control, monitor, time, monitorIndex, timerIndex});
    if (m_expiredScratch.capacity() < m_timeouts.size())
        m_expiredScratch.reserve(m_timeouts.size());
    if (m_pendingScratch.capacity() < m_timeouts.size())
        m_pendingScratch.reserve(m_timeouts.size());
}

void StateChangeMonitors::remove(Control* control, int64_t monitorIndex, int timerIndex) {
    m_monitors.erase(
        std::remove_if(m_monitors.begin(), m_monitors.end(),
                       [&](const Monitor& monitor) {
                           return monitor.control == control &&
                                  monitor.monitorIndex == monitorIndex &&
                                  monitor.timerIndex == timerIndex;
                       }),
        m_monitors.end());
}

void StateChangeMonitors::remove_timeout(IStateChangeMonitor* monitor, int64_t monitorIndex,
                                         int timerIndex) {
    m_timeouts.erase(
        std::remove_if(m_timeouts.begin(), m_timeouts.end(),
                       [&](const Timeout& timeout) {
                           return timeout.monitor == monitor &&
                                  timeout.monitorIndex == monitorIndex &&
                                  timeout.timerIndex == timerIndex;
                       }),
        m_timeouts.end());
}

void StateChangeMonitors::clear() {
    m_monitors.clear();
    m_timeouts.clear();
}

void StateChangeMonitors::remove_for_device(Device* device) {
    m_monitors.erase(
        std::remove_if(m_monitors.begin(), m_monitors.end(),
                       [&](const Monitor& monitor) {
                           return monitor.control != nullptr && monitor.control->device == device;
                       }),
        m_monitors.end());
    m_timeouts.erase(
        std::remove_if(m_timeouts.begin(), m_timeouts.end(),
                       [&](const Timeout& timeout) {
                           return timeout.control != nullptr && timeout.control->device == device;
                       }),
        m_timeouts.end());
}

void StateChangeMonitors::process_timeouts(double time) {
    if (m_timeouts.empty())
        return;

    m_expiredScratch.clear();
    m_pendingScratch.clear();
    for (const Timeout& timeout : m_timeouts) {
        if (timeout.monitor == nullptr)
            continue;
        if (timeout.time <= time)
            m_expiredScratch.push_back(timeout);
        else
            m_pendingScratch.push_back(timeout);
    }
    m_timeouts.swap(m_pendingScratch);
    m_pendingScratch.clear();

    for (const Timeout& timeout : m_expiredScratch)
        timeout.monitor->notify_timer_expired(timeout.control, time, timeout.monitorIndex,
                                              timeout.timerIndex);
}

void StateChangeMonitors::process(Device* device, double time, low::Event* event) {
    if (device == nullptr || device->current_state() == nullptr)
        return;

    m_notificationScratch.clear();

    for (Monitor& monitor : m_monitors) {
        Control* control = monitor.control;
        if (control == nullptr || control->device != device)
            continue;

        const uint64_t hash = hash_control_value(control, device->current_state());
        if (hash == monitor.valueHash)
            continue;

        monitor.valueHash = hash;
        monitor.time = time;
        if (monitor.monitor != nullptr)
            m_notificationScratch.push_back({control, monitor.monitor, monitor.monitorIndex});
    }

    for (const Notification& notification : m_notificationScratch) {
        notification.monitor->notify_state_changed(
            notification.control, time, event, notification.monitorIndex);
    }
}

void StateChangeMonitors::signal(Control* control, IStateChangeMonitor* monitor,
                                 int64_t monitorIndex, double time, low::Event* event) {
    if (control == nullptr || monitor == nullptr)
        return;

    for (Monitor& entry : m_monitors) {
        if (entry.control != control || entry.monitor != monitor ||
            entry.monitorIndex != monitorIndex)
            continue;
        if (control->device != nullptr && control->device->current_state() != nullptr)
            entry.valueHash = hash_control_value(control, control->device->current_state());
        entry.time = time;
        monitor->notify_state_changed(control, time, event, monitorIndex);
        return;
    }
}

} // namespace insys
