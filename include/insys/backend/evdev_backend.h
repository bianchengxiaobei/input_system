#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "insys/backend/evdev_capture.h"
#include "insys/backend/evdev_hotplug.h"
#include "insys/backend/runtime.h"
#include "insys/manager.h"

namespace insys::backend {

class EvdevBackend {
public:
    using SourceFactory = std::function<
        evdev::EvdevCaptureSession::ReadFunction(const evdev::EvdevDeviceInfo&)>;

    bool initialize(Runtime& runtime, Manager& manager);
    void shutdown();
    void update_devices(std::vector<evdev::EvdevDeviceInfo> discovered,
                        const SourceFactory& sourceFactory);
    void poll();

private:
    struct Slot {
        evdev::EvdevDeviceType type = evdev::EvdevDeviceType::Unknown;
        int deviceId = 0;
    };

    void remove_slot(std::string_view path);
    void emit_state(std::string_view path, evdev::EvdevDeviceType type,
                    const evdev::EvdevDeviceState& state);

    Runtime* m_runtime = nullptr;
    Manager* m_manager = nullptr;
    evdev::EvdevDeviceTracker m_tracker;
    evdev::EvdevCaptureSession m_capture;
    std::unordered_map<std::string, Slot> m_slots;
};

} // namespace insys::backend
