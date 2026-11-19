#pragma once

#include <array>
#include <vector>

#include "insys/device_states.h"
#include "insys/types.h"

namespace insys {

enum class GestureType {
    Tap,
    LongPress,
    Swipe,
    Pinch,
};

enum class SwipeDirection {
    None,
    Up,
    Down,
    Left,
    Right,
};

struct GestureEvent {
    GestureType type = GestureType::Tap;
    SwipeDirection direction = SwipeDirection::None;
    int touchIndex = -1;
    int secondTouchIndex = -1;
    Vector2 position;
    Vector2 delta;
    float magnitude = 0.0f;
    double time = 0.0;
    double duration = 0.0;
};

struct GestureSettings {
    float tapMaxDistance = 10.0f;
    float swipeMinDistance = 40.0f;
    double tapMaxDuration = 0.25;
    double longPressDuration = 0.5;
    float pinchMinScaleDelta = 0.05f;
};

class TouchGestureRecognizer {
public:
    explicit TouchGestureRecognizer(GestureSettings settings = {});

    void reset();
    void update(const TouchscreenState& state, double time);

    const std::vector<GestureEvent>& events() const { return m_events; }
    const GestureSettings& settings() const { return m_settings; }

private:
    struct TouchTrack {
        bool active = false;
        bool swipeEmitted = false;
        bool longPressEmitted = false;
        bool multiTouch = false;
        Vector2 start;
        Vector2 last;
        double startTime = 0.0;
    };

    struct PinchTrack {
        bool active = false;
        int first = -1;
        int second = -1;
        float baselineDistance = 0.0f;
        float lastScale = 1.0f;
        double startTime = 0.0;
    };

    void start_touch(int index, const TouchPointState& touch, double time);
    void update_touch(int index, const TouchPointState& touch, double time);
    void end_touch(int index, const TouchPointState& touch, double time);
    void update_pinch(const TouchscreenState& state, double time);
    void add_event(GestureEvent event);

    GestureSettings m_settings;
    std::array<TouchTrack, kMaxTouches> m_tracks{};
    PinchTrack m_pinch;
    std::vector<GestureEvent> m_events;
};

} // namespace insys


