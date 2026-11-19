#pragma once

#include <array>

#include "insys/device.h"
#include "insys/low/key.h"
#include "insys/manager.h"
#include "insys/types.h"
#include "insys/ui/ui_event_sink.h"

namespace insys {
struct ActionDef;
}

namespace insys::ui {

enum class NavigationSource {
    RawControls,
    Actions,
};

enum class CapturePolicy {
    Full,
    PointerAndText,
    None,
};

/// Translates insys devices/actions into generic UI events.
///
/// The adapter reads the current device state after Manager::update() and
/// pushes it into a UiEventSink. It keeps edge state so the same key/button is
/// not emitted twice, while also exposing capture queries for game-action
/// gating.
class UiInputAdapter {
public:
    UiInputAdapter(UiEventSink* sink, Manager& manager);
    explicit UiInputAdapter(UiEventSink& sink, Manager& manager);
    virtual ~UiInputAdapter() = default;

    void set_sink(UiEventSink* sink);

    void set_devices(Keyboard* keyboard, Mouse* mouse);
    void set_navigation_source(NavigationSource source);
    void set_capture_policy(CapturePolicy policy);

    /// Full capture policy only blocks while the UI is considered active.
    void set_ui_active(bool active);
    bool ui_active() const { return m_uiActive; }

    /// Dead zone used by gamepad stick navigation.
    void set_navigation_dead_zone(float deadZone);
    float navigation_dead_zone() const { return m_navDeadZone; }

    /// Maps pointer/window coordinates into UI coordinates.
    void set_coordinate_scale(Vector2 scale);
    Vector2 coordinate_scale() const { return m_coordinateScale; }

    /// Optional action-driven UI input.
    ///
    /// When point is set, the action drives the pointer instead of the mouse.
    /// Click and scroll are optional and are only read in action-pointer mode.
    void set_ui_actions(ActionDef* navigate, ActionDef* submit, ActionDef* cancel,
                        ActionDef* point = nullptr, ActionDef* click = nullptr,
                        ActionDef* scroll = nullptr);

    NavigationSource navigation_source() const { return m_navigationSource; }
    CapturePolicy capture_policy() const { return m_capturePolicy; }

    /// Must be called once per frame after System::update()/Manager::update().
    void flush();

    bool pointer_over_ui() const;
    bool is_consuming_pointer() const { return m_pointerConsumed; }
    bool is_consuming_keyboard() const { return m_keyboardConsumed; }
    bool is_consuming_text() const { return m_textConsumed; }

    /// Policy-aware capture queries. Use these to gate game actions.
    bool is_capturing_pointer() const;
    bool is_capturing_keyboard() const;
    bool is_capturing_text() const;
    bool blocks_game_input() const;

private:
    enum class NavDirection {
        None,
        Up,
        Down,
        Left,
        Right,
    };

    void reset_pointer_state();
    void release_navigation();

    void flush_pointer();
    void flush_mouse_pointer();
    void flush_action_pointer();
    void flush_keyboard();
    void flush_text();
    void flush_navigation_raw();
    void flush_navigation_actions();
    void flush_navigation_vector(Vector2 value);

    void emit_navigation_direction(NavDirection direction);
    void emit_navigation_button(Key key, bool down);
    bool emit_sink_key_down(Key key, int modifiers);
    bool emit_sink_key_up(Key key, int modifiers);
    void update_navigation_submit(bool pressed);
    void update_navigation_cancel(bool pressed);

    int current_modifiers() const;
    bool key_is_down(Key key) const;
    Control* key_control(Key key) const;
    Gamepad* gamepad() const;
    bool action_button(const ActionDef* action, bool pressed) const;

    UiEventSink* m_sink = nullptr;
    Manager* m_manager = nullptr;
    Keyboard* m_keyboard = nullptr;
    Mouse* m_mouse = nullptr;

    NavigationSource m_navigationSource = NavigationSource::RawControls;
    CapturePolicy m_capturePolicy = CapturePolicy::PointerAndText;
    bool m_uiActive = true;

    Vector2 m_coordinateScale{1.0f, 1.0f};
    float m_navDeadZone = 0.5f;

    Vector2 m_lastMousePos{-100000.0f, -100000.0f};
    Vector2 m_lastScroll{};
    bool m_lastButtons[3]{};
    std::array<bool, kKeyCount> m_lastKeys{};
    bool m_mouseActive = false;

    NavDirection m_navDirection = NavDirection::None;
    bool m_navSubmit = false;
    bool m_navCancel = false;

    ActionDef* m_uiNavigate = nullptr;
    ActionDef* m_uiSubmit = nullptr;
    ActionDef* m_uiCancel = nullptr;
    ActionDef* m_uiPoint = nullptr;
    ActionDef* m_uiClick = nullptr;
    ActionDef* m_uiScroll = nullptr;
    bool m_lastActionClick = false;

    bool m_pointerOverUi = false;
    bool m_pointerConsumed = false;
    bool m_keyboardConsumed = false;
    bool m_textConsumed = false;
};

} // namespace insys::ui
