#include "insys/backend/inotify_monitor.h"

namespace insys::backend::linux_evdev {

bool InputHotplugMonitor::feed(const std::uint8_t* data, std::size_t size) {
    if (m_decoder.feed(data, size) == 0)
        return false;

    bool scan = false;
    for (const InotifyEvent& event : m_decoder.events())
        scan = is_input_device_change(event) || scan;
    m_decoder.clear_events();
    return scan;
}

bool InputHotplugMonitor::feed(const InotifyEvent& event) {
    return is_input_device_change(event);
}

bool is_input_device_change(const InotifyEvent& event) {
    if (!is_input_scan_event(event))
        return false;
    return event.name.empty() || event.name.rfind("event", 0) == 0;
}

} // namespace insys::backend::linux_evdev
