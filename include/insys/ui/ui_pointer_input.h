#pragma once

#include <array>
#include <unordered_map>
#include <vector>

#include "insys/device.h"
#include "insys/device_states.h"
#include "insys/manager.h"
#include "insys/types.h"

namespace insys::ui {

inline constexpr int kMousePointerId = -1;

struct UiPointerEvent {
    int pointerId = kMousePointerId;
    Vector2 position{};
    bool touch = false;
    int displayIndex = 0;
    double time = 0.0;
};

struct UiRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    bool contains(Vector2 point) const {
        return point.x >= x && point.y >= y && point.x < x + width &&
               point.y < y + height;
    }

    Vector2 center() const { return {x + width * 0.5f, y + height * 0.5f}; }
};

class UiPointerTarget {
public:
    virtual ~UiPointerTarget() = default;

    virtual bool contains(Vector2 position) const = 0;
    virtual bool accepts_pointer(Vector2 position) const { return contains(position); }
    virtual void on_pointer_down(const UiPointerEvent& event) { (void)event; }
    virtual void on_pointer_move(const UiPointerEvent& event) { (void)event; }
    virtual void on_pointer_up(const UiPointerEvent& event) { (void)event; }
};

class UiPointerInputModule {
public:
    explicit UiPointerInputModule(Manager& manager);
    virtual ~UiPointerInputModule() = default;

    UiPointerInputModule(const UiPointerInputModule&) = delete;
    UiPointerInputModule& operator=(const UiPointerInputModule&) = delete;

    void set_touchscreen(Touchscreen* touchscreen);
    Touchscreen* touchscreen() const { return m_touchscreen; }
    void set_mouse(Mouse* mouse);
    Mouse* mouse() const { return m_mouse; }

    void set_touch_enabled(bool enabled);
    bool touch_enabled() const { return m_touchEnabled; }
    void set_mouse_enabled(bool enabled);
    bool mouse_enabled() const { return m_mouseEnabled; }

    void add_target(UiPointerTarget* target);
    void remove_target(UiPointerTarget* target);
    void clear_targets();

    virtual void update();
    void reset();

    UiPointerTarget* captured_target(int pointerId) const;
    bool captures_pointer() const { return !m_captured.empty(); }

protected:
    Manager* m_manager = nullptr;

private:
    UiPointerTarget* find_target(Vector2 position) const;
    void sync_devices();
    void dispatch_down(int pointerId, Vector2 position, bool touch, int displayIndex);
    void dispatch_move(int pointerId, Vector2 position, bool touch, int displayIndex);
    void dispatch_up(int pointerId, Vector2 position, bool touch, int displayIndex);

    Touchscreen* m_touchscreen = nullptr;
    Mouse* m_mouse = nullptr;
    bool m_touchEnabled = true;
    bool m_mouseEnabled = true;

    std::vector<UiPointerTarget*> m_targets;
    std::unordered_map<int, UiPointerTarget*> m_captured;
    std::unordered_map<int, Vector2> m_lastPositions;
    std::array<bool, kMaxTouches> m_touchDown{};
    std::array<Vector2, kMaxTouches> m_touchLastPositions{};

    bool m_mouseDown = false;
    Vector2 m_mouseLastPosition{};
};

} // namespace insys::ui

