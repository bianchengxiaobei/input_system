#pragma once

#include <vector>

#include "insys/backend/evdev_backend.h"
#include "insys/backend/evdev_hotplug.h"

namespace insys::backend::linux_evdev {

std::vector<evdev::EvdevDeviceInfo> scan_devices();
EvdevBackend::SourceFactory make_source_factory();

} // namespace insys::backend::linux_evdev
