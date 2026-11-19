#include "insys/backend/mobile_input_queue.h"

#include <utility>

namespace insys::backend {

void MobileInputQueue::push_touch(MobileInputEventType type, int touchId,
                                  Vector2 position, float pressure) {
    if (type == MobileInputEventType::Motion ||
        type == MobileInputEventType::Suspend)
        return;
    Event event;
    event.type = type;
    event.touchId = touchId;
    event.position = position;
    event.pressure = pressure;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.push_back(event);
}

void MobileInputQueue::push_motion(const MobileMotionSample& sample) {
    Event event;
    event.type = MobileInputEventType::Motion;
    event.motion = sample;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.push_back(event);
}

void MobileInputQueue::push_suspend() {
    Event event;
    event.type = MobileInputEventType::Suspend;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.push_back(event);
}

std::size_t MobileInputQueue::drain(MobileInputBridge& bridge) {
    std::vector<Event> events;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        events.swap(m_events);
    }

    for (const Event& event : events) {
        switch (event.type) {
        case MobileInputEventType::TouchBegan:
            bridge.touch_began(event.touchId, event.position, event.pressure);
            break;
        case MobileInputEventType::TouchMoved:
            bridge.touch_moved(event.touchId, event.position, event.pressure);
            break;
        case MobileInputEventType::TouchEnded:
            bridge.touch_ended(event.touchId);
            break;
        case MobileInputEventType::TouchCanceled:
            bridge.touch_canceled(event.touchId);
            break;
        case MobileInputEventType::Motion:
            bridge.update_motion(event.motion);
            break;
        case MobileInputEventType::Suspend:
            bridge.suspend();
            break;
        }
    }
    return events.size();
}

std::size_t MobileInputQueue::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_events.size();
}

void MobileInputQueue::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.clear();
}

} // namespace insys::backend
