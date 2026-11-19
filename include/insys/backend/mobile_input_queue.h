#pragma once

#include <cstddef>
#include <mutex>
#include <vector>

#include "insys/backend/mobile_input.h"

namespace insys::backend {

enum class MobileInputEventType {
    TouchBegan,
    TouchMoved,
    TouchEnded,
    TouchCanceled,
    Motion,
    Suspend,
};

class MobileInputQueue {
public:
    void push_touch(MobileInputEventType type, int touchId, Vector2 position,
                    float pressure = 1.0f);
    void push_motion(const MobileMotionSample& sample);
    void push_suspend();

    std::size_t drain(MobileInputBridge& bridge);
    std::size_t size() const;
    void clear();

private:
    struct Event {
        MobileInputEventType type = MobileInputEventType::Motion;
        int touchId = 0;
        Vector2 position{};
        float pressure = 1.0f;
        MobileMotionSample motion;
    };

    mutable std::mutex m_mutex;
    std::vector<Event> m_events;
};

} // namespace insys::backend

