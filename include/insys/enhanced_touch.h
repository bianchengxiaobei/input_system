#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

#include "insys/device.h"
#include "insys/device_states.h"
#include "insys/types.h"

namespace insys {

class Manager;
class Touchscreen;

namespace enhanced_touch {

class Finger;
class TouchHistory;
class EnhancedTouchSupport;

class Touch {
public:
    Touch() = default;

    bool valid() const { return m_valid; }
    Finger* finger() const { return m_finger; }
    TouchPhase phase() const { return m_phase; }
    bool began() const { return m_phase == TouchPhase::Began; }
    bool in_progress() const;
    bool is_in_progress() const { return in_progress(); }
    bool ended() const;

    int touch_id() const { return m_touchId; }
    float pressure() const { return m_pressure; }
    Vector2 radius() const { return m_radius; }
    double start_time() const { return m_startTime; }
    double time() const { return m_time; }
    Touchscreen* screen() const;

    Vector2 screen_position() const { return m_position; }
    Vector2 start_screen_position() const { return m_startPosition; }
    Vector2 delta() const { return m_delta; }
    int tap_count() const { return m_tapCount; }
    bool is_tap() const { return m_isTap; }
    int display_index() const { return m_displayIndex; }
    uint32_t update_step_count() const { return m_updateStepCount; }
    uint64_t record_id() const { return m_recordId; }

    TouchHistory history() const;

    static const std::vector<Touch>& active_touches();
    static const std::vector<Finger*>& fingers();
    static const std::vector<Finger*>& active_fingers();
    static const std::vector<Touchscreen*>& screens();
    static int max_history_length_per_finger();

    static int add_on_finger_down(std::function<void(const Finger&)> callback);
    static int add_on_finger_move(std::function<void(const Finger&)> callback);
    static int add_on_finger_up(std::function<void(const Finger&)> callback);
    static void remove_callback(int callbackId);

private:
    friend class Finger;
    friend class EnhancedTouchSupport;

    Finger* m_finger = nullptr;
    bool m_valid = false;
    TouchPhase m_phase = TouchPhase::Began;
    int m_touchId = 0;
    float m_pressure = 0.0f;
    Vector2 m_radius{};
    double m_startTime = 0.0;
    double m_time = 0.0;
    Vector2 m_position{};
    Vector2 m_startPosition{};
    Vector2 m_delta{};
    int m_tapCount = 0;
    bool m_isTap = false;
    int m_displayIndex = 0;
    uint32_t m_updateStepCount = 0;
    uint64_t m_recordId = 0;
};

class TouchHistory {
public:
    TouchHistory() = default;

    std::size_t count() const { return m_touches.size(); }
    std::size_t size() const { return m_touches.size(); }
    bool empty() const { return m_touches.empty(); }
    Touch operator[](std::size_t index) const { return m_touches.at(index); }

    std::vector<Touch>::const_iterator begin() const { return m_touches.begin(); }
    std::vector<Touch>::const_iterator end() const { return m_touches.end(); }

private:
    friend class Finger;
    friend class Touch;

    explicit TouchHistory(std::vector<Touch> touches) : m_touches(std::move(touches)) {}

    std::vector<Touch> m_touches;
};

class Finger {
public:
    Touchscreen* screen() const { return m_screen; }
    int index() const { return m_index; }
    bool is_active() const { return current_touch().valid(); }
    Vector2 screen_position() const;

    Touch last_touch() const;
    Touch current_touch() const;
    TouchHistory touch_history() const;

private:
    friend class EnhancedTouchSupport;
    friend class Touch;

    Finger(Touchscreen* screen, int index) : m_screen(screen), m_index(index) {}

    Touchscreen* m_screen = nullptr;
    int m_index = 0;
    std::deque<Touch> m_history;

    bool m_contactActive = false;
    int m_currentTouchId = 0;
    double m_startTime = 0.0;
    Vector2 m_startPosition{};
    int m_tapCount = 0;
    TouchPointState m_lastPoint{};
    bool m_hasLastPoint = false;
};

class EnhancedTouchSupport {
public:
    static void enable(Manager& manager);
    static void disable();
    static void disable_for(Manager* manager);
    static bool enabled();
    static void update();
    static void reset();

    static const std::vector<Touch>& active_touches();
    static const std::vector<Finger*>& fingers();
    static const std::vector<Finger*>& active_fingers();
    static const std::vector<Touchscreen*>& screens();

    static int max_history_length_per_finger();
    static void set_max_history_length_per_finger(int length);

    static int add_on_finger_down(std::function<void(const Finger&)> callback);
    static int add_on_finger_move(std::function<void(const Finger&)> callback);
    static int add_on_finger_up(std::function<void(const Finger&)> callback);
    static void remove_callback(int callbackId);

    static Manager* manager();
    static uint32_t current_update_step();
};

class TouchSimulation {
public:
    explicit TouchSimulation(Manager& manager);
    ~TouchSimulation();

    TouchSimulation(const TouchSimulation&) = delete;
    TouchSimulation& operator=(const TouchSimulation&) = delete;

    void enable();
    void disable();
    bool enabled() const { return m_enabled; }
    Touchscreen* simulated_touchscreen() const { return m_touchscreen; }

    void add_pointer(Device* device, Vector2Control* position, Control* button);
    void update();

private:
    struct PointerSource {
        Device* device = nullptr;
        Vector2Control* position = nullptr;
        Control* button = nullptr;
        int slot = -1;
        Vector2 startPosition{};
        Vector2 lastPosition{};
    };

    bool ensure_touchscreen();
    void add_default_pointers();
    int find_pointer(Device* device, Control* button) const;
    int allocate_slot() const;

    Manager* m_manager = nullptr;
    Touchscreen* m_touchscreen = nullptr;
    bool m_enabled = false;
    bool m_ownsTouchscreen = false;
    TouchscreenState m_state{};
    std::vector<PointerSource> m_pointers;
};

} // namespace enhanced_touch
} // namespace insys








