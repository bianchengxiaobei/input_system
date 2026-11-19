#pragma once

#include "insys/backend/inotify_monitor.h"

namespace insys::backend::linux_evdev {

class LinuxInotifyWatcher {
public:
    LinuxInotifyWatcher() = default;
    ~LinuxInotifyWatcher();

    LinuxInotifyWatcher(const LinuxInotifyWatcher&) = delete;
    LinuxInotifyWatcher& operator=(const LinuxInotifyWatcher&) = delete;

    bool initialize();
    void shutdown();
    bool poll_scan_request();
    bool valid() const { return m_fd >= 0; }

private:
    int m_fd = -1;
    int m_watch = -1;
    InputHotplugMonitor m_monitor;
};

} // namespace insys::backend::linux_evdev
