#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace insys {

class Control;
class Device;

namespace low {
struct Event;
}

class IStateChangeMonitor {
public:
    virtual ~IStateChangeMonitor() = default;

    virtual void notify_state_changed(Control* control, double time, low::Event* event,
                                      int64_t monitorIndex) = 0;
    virtual void notify_timer_expired(Control* control, double time, int64_t monitorIndex,
                                      int timerIndex) = 0;
};

class StateChangeMonitors {
public:
    struct Monitor {
        Control* control = nullptr;
        IStateChangeMonitor* monitor = nullptr;
        double time = 0.0;
        int64_t monitorIndex = 0;
        int timerIndex = 0;
        uint64_t valueHash = 0;
    };

    struct Timeout {
        Control* control = nullptr;
        IStateChangeMonitor* monitor = nullptr;
        double time = 0.0;
        int64_t monitorIndex = 0;
        int timerIndex = 0;
    };

    void add(Control* control, IStateChangeMonitor* monitor, double time, int64_t monitorIndex,
             int timerIndex = 0);
    void add_timeout(Control* control, IStateChangeMonitor* monitor, double time,
                     int64_t monitorIndex, int timerIndex);
    void remove(Control* control, int64_t monitorIndex, int timerIndex = 0);
    void remove_timeout(IStateChangeMonitor* monitor, int64_t monitorIndex, int timerIndex);
    void clear();
    void remove_for_device(Device* device);

    std::size_t count() const { return m_monitors.size(); }
    const std::vector<Monitor>& monitors() const { return m_monitors; }
    std::size_t timeout_count() const { return m_timeouts.size(); }

    void process(Device* device, double time, low::Event* event);
    void process_timeouts(double time);
    void signal(Control* control, IStateChangeMonitor* monitor, int64_t monitorIndex,
                double time, low::Event* event);

private:
    struct Notification {
        Control* control = nullptr;
        IStateChangeMonitor* monitor = nullptr;
        int64_t monitorIndex = 0;
    };

    std::vector<Monitor> m_monitors;
    std::vector<Timeout> m_timeouts;
    std::vector<Notification> m_notificationScratch;
    std::vector<Timeout> m_expiredScratch;
    std::vector<Timeout> m_pendingScratch;
};

} // namespace insys
