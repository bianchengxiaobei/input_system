#pragma once

#include <string>
#include <vector>

#include "insys/backend/evdev_device.h"

namespace insys::backend::evdev {

struct EvdevDeviceInfo {
    std::string path;
    EvdevDeviceType type = EvdevDeviceType::Unknown;
};

enum class EvdevChangeKind {
    Added,
    Removed,
};

struct EvdevDeviceChange {
    EvdevChangeKind kind = EvdevChangeKind::Added;
    std::string path;
    EvdevDeviceType type = EvdevDeviceType::Unknown;
};

class EvdevDeviceTracker {
public:
    std::vector<EvdevDeviceChange> update(std::vector<EvdevDeviceInfo> discovered);

    const std::vector<EvdevDeviceInfo>& devices() const { return m_devices; }
    void clear() { m_devices.clear(); }

private:
    std::vector<EvdevDeviceInfo> m_devices;
};

} // namespace insys::backend::evdev
