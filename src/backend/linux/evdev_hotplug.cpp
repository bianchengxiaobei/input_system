#include "insys/backend/evdev_hotplug.h"

#include <algorithm>
#include <utility>

namespace insys::backend::evdev {
namespace {

bool contains(const std::vector<EvdevDeviceInfo>& devices,
              const EvdevDeviceInfo& candidate) {
    return std::any_of(devices.begin(), devices.end(),
                       [&candidate](const EvdevDeviceInfo& device) {
                           return device.path == candidate.path &&
                                  device.type == candidate.type;
                       });
}

bool contains_path(const std::vector<EvdevDeviceInfo>& devices,
                   const std::string& path) {
    return std::any_of(devices.begin(), devices.end(),
                       [&path](const EvdevDeviceInfo& device) {
                           return device.path == path;
                       });
}

} // namespace

std::vector<EvdevDeviceChange>
EvdevDeviceTracker::update(std::vector<EvdevDeviceInfo> discovered) {
    std::vector<EvdevDeviceInfo> next;
    next.reserve(discovered.size());
    for (EvdevDeviceInfo& device : discovered) {
        if (device.path.empty() || contains_path(next, device.path))
            continue;
        next.push_back(std::move(device));
    }

    std::vector<EvdevDeviceChange> changes;
    for (const EvdevDeviceInfo& previous : m_devices) {
        if (!contains(next, previous))
            changes.push_back({EvdevChangeKind::Removed, previous.path, previous.type});
    }
    for (const EvdevDeviceInfo& current : next) {
        if (!contains(m_devices, current))
            changes.push_back({EvdevChangeKind::Added, current.path, current.type});
    }

    m_devices = std::move(next);
    return changes;
}

} // namespace insys::backend::evdev
