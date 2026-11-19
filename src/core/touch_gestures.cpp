#include "insys/gestures.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace insys {
namespace {

constexpr float kPressThreshold = 0.5f;
constexpr float kMinDistance = 1e-6f;

bool is_canceled(const TouchPointState& touch) {
    return static_cast<int>(touch.phase) == static_cast<int>(TouchPhase::Canceled);
}

SwipeDirection swipe_direction(Vector2 delta) {
    const float absoluteX = std::fabs(delta.x);
    const float absoluteY = std::fabs(delta.y);
    if (absoluteX < kMinDistance && absoluteY < kMinDistance)
        return SwipeDirection::None;
    if (absoluteX >= absoluteY)
        return delta.x >= 0.0f ? SwipeDirection::Right : SwipeDirection::Left;
    return delta.y >= 0.0f ? SwipeDirection::Down : SwipeDirection::Up;
}

} // namespace

TouchGestureRecognizer::TouchGestureRecognizer(GestureSettings settings)
    : m_settings(std::move(settings)) {
    m_settings.tapMaxDistance = std::max(0.0f, m_settings.tapMaxDistance);
    m_settings.swipeMinDistance = std::max(0.0f, m_settings.swipeMinDistance);
    m_settings.tapMaxDuration = std::max(0.0, m_settings.tapMaxDuration);
    m_settings.longPressDuration = std::max(0.0, m_settings.longPressDuration);
    m_settings.pinchMinScaleDelta = std::max(0.0f, m_settings.pinchMinScaleDelta);
}

void TouchGestureRecognizer::reset() {
    m_tracks = {};
    m_pinch = {};
    m_events.clear();
}

void TouchGestureRecognizer::update(const TouchscreenState& state, double time) {
    m_events.clear();

    int pressedCount = 0;
    for (int i = 0; i < kMaxTouches; ++i) {
        const TouchPointState& touch = state.touches[i];
        const bool completed = is_canceled(touch) ||
                               static_cast<int>(touch.phase) ==
                                   static_cast<int>(TouchPhase::Ended);
        if (touch.press >= kPressThreshold && !completed)
            ++pressedCount;
    }

    for (int i = 0; i < kMaxTouches; ++i) {
        const TouchPointState& touch = state.touches[i];
        TouchTrack& track = m_tracks[static_cast<std::size_t>(i)];
        const bool completed = is_canceled(touch) ||
                               static_cast<int>(touch.phase) ==
                                   static_cast<int>(TouchPhase::Ended);
        const bool down = touch.press >= kPressThreshold && !completed;

        if (down) {
            if (!track.active)
                start_touch(i, touch, time);
            if (pressedCount > 1)
                track.multiTouch = true;
            update_touch(i, touch, time);
        } else if (track.active) {
            end_touch(i, touch, time);
        }
    }

    update_pinch(state, time);
}

void TouchGestureRecognizer::start_touch(int index, const TouchPointState& touch,
                                         double time) {
    TouchTrack& track = m_tracks[static_cast<std::size_t>(index)];
    track = {};
    track.active = true;
    track.start = touch.position;
    track.last = touch.position;
    track.startTime = time;
}

void TouchGestureRecognizer::update_touch(int index, const TouchPointState& touch,
                                          double time) {
    TouchTrack& track = m_tracks[static_cast<std::size_t>(index)];
    track.last = touch.position;
    const Vector2 delta = touch.position - track.start;
    const float distance = delta.length();
    const double duration = time - track.startTime;

    if (!track.multiTouch && !track.longPressEmitted &&
        distance <= m_settings.tapMaxDistance &&
        duration >= m_settings.longPressDuration) {
        GestureEvent event;
        event.type = GestureType::LongPress;
        event.touchIndex = index;
        event.position = touch.position;
        event.delta = delta;
        event.magnitude = distance;
        event.time = time;
        event.duration = duration;
        add_event(std::move(event));
        track.longPressEmitted = true;
    }

    if (!track.multiTouch && !track.swipeEmitted &&
        distance >= m_settings.swipeMinDistance) {
        GestureEvent event;
        event.type = GestureType::Swipe;
        event.direction = swipe_direction(delta);
        event.touchIndex = index;
        event.position = touch.position;
        event.delta = delta;
        event.magnitude = distance;
        event.time = time;
        event.duration = duration;
        add_event(std::move(event));
        track.swipeEmitted = true;
    }
}

void TouchGestureRecognizer::end_touch(int index, const TouchPointState& touch,
                                        double time) {
    TouchTrack& track = m_tracks[static_cast<std::size_t>(index)];
    if (is_canceled(touch)) {
        track.active = false;
        return;
    }

    update_touch(index, touch, time);

    const Vector2 delta = touch.position - track.start;
    const float distance = delta.length();
    const double duration = time - track.startTime;
    if (!track.multiTouch && !track.swipeEmitted && !track.longPressEmitted &&
        distance <= m_settings.tapMaxDistance &&
        duration <= m_settings.tapMaxDuration) {
        GestureEvent event;
        event.type = GestureType::Tap;
        event.touchIndex = index;
        event.position = touch.position;
        event.delta = delta;
        event.magnitude = distance;
        event.time = time;
        event.duration = duration;
        add_event(std::move(event));
    }

    track.active = false;
}

void TouchGestureRecognizer::update_pinch(const TouchscreenState& state, double time) {
    int first = -1;
    int second = -1;
    for (int i = 0; i < kMaxTouches; ++i) {
        if (!m_tracks[static_cast<std::size_t>(i)].active)
            continue;
        if (first < 0)
            first = i;
        else {
            second = i;
            break;
        }
    }

    if (first < 0 || second < 0) {
        m_pinch = {};
        return;
    }

    const Vector2 firstPosition = state.touches[first].position;
    const Vector2 secondPosition = state.touches[second].position;
    const float distance = (secondPosition - firstPosition).length();
    if (distance <= kMinDistance) {
        m_pinch = {};
        return;
    }

    if (!m_pinch.active || m_pinch.first != first || m_pinch.second != second) {
        m_pinch = {};
        m_pinch.active = true;
        m_pinch.first = first;
        m_pinch.second = second;
        m_pinch.baselineDistance = distance;

        m_pinch.lastScale = 1.0f;
        m_pinch.startTime = time;
        return;
    }

    const float scale = distance / m_pinch.baselineDistance;
    if (std::fabs(scale - m_pinch.lastScale) >= m_settings.pinchMinScaleDelta) {
        GestureEvent event;
        event.type = GestureType::Pinch;
        event.touchIndex = first;
        event.secondTouchIndex = second;
        event.position = (firstPosition + secondPosition) * 0.5f;
        event.delta = secondPosition - firstPosition;
        event.magnitude = scale;
        event.time = time;
        event.duration = time - m_pinch.startTime;
        add_event(std::move(event));
        m_pinch.lastScale = scale;
    }

}

void TouchGestureRecognizer::add_event(GestureEvent event) {
    m_events.push_back(std::move(event));
}

} // namespace insys


