#pragma once

#include <array>
#include <string>
#include <unordered_map>

#include "insys/backend/runtime.h"
#include "insys/device_states.h"
#include "insys/manager.h"

namespace insys::backend {

class TouchBackend {
public:
    bool initialize(Runtime& runtime, Manager& manager,
                    const DeviceDescription& description = {});
    void shutdown();

    bool touch_began(int touchId, Vector2 position, float pressure = 1.0f);
    bool touch_moved(int touchId, Vector2 position, float pressure = 1.0f);
    bool touch_ended(int touchId);
    bool touch_canceled(int touchId);
    int cancel_all();
    int active_touch_count() const;
    void poll();

    bool has_touchscreen() const { return m_deviceId != 0; }
    int device_id() const { return m_deviceId; }

private:
    struct Track {
        int externalId = -1;
        bool active = false;
        Vector2 start;
        Vector2 last;
    };

    int find_track(int touchId) const;
    int allocate_track(int touchId);
    bool ensure_device();

    Runtime* m_runtime = nullptr;
    Manager* m_manager = nullptr;
    DeviceDescription m_description;
    int m_deviceId = 0;
    TouchscreenState m_state{};
    std::array<Track, kMaxTouches> m_tracks{};
    std::unordered_map<int, int> m_slotByTouch;
    bool m_dirty = false;
};

} // namespace insys::backend


