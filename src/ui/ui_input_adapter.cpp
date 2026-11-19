#include "insys/ui/ui_input_adapter.h"

#include <algorithm>
#include <cmath>

#include "insys/actions/action.h"
#include "insys/actions/action_state.h"
#include "insys/low/key.h"

namespace insys::ui {
namespace {

constexpr int kMouseButtonLeft = 0;

Vector2 scaled(Vector2 value, Vector2 scale) {
    return {value.x * scale.x, value.y * scale.y};
}

bool consumes(bool sinkResult) {
    return sinkResult;
}

} // namespace

UiInputAdapter::UiInputAdapter(UiEventSink* sink, Manager& manager)
    : m_sink(sink), m_manager(&manager) {
    set_devices(static_cast<Keyboard*>(manager.device_by_layout("Keyboard")),
                static_cast<Mouse*>(manager.device_by_layout("Mouse")));
}

UiInputAdapter::UiInputAdapter(UiEventSink& sink, Manager& manager)
    : UiInputAdapter(&sink, manager) {}

void UiInputAdapter::set_sink(UiEventSink* sink) {
    m_sink = sink;
    reset_pointer_state();
    release_navigation();
}

void UiInputAdapter::set_devices(Keyboard* keyboard, Mouse* mouse) {
    m_keyboard = keyboard;
    m_mouse = mouse;
    reset_pointer_state();
    m_lastKeys.fill(false);
}

void UiInputAdapter::set_navigation_source(NavigationSource source) {
    if (m_navigationSource == source)
        return;
    release_navigation();
    m_navigationSource = source;
}

void UiInputAdapter::set_capture_policy(CapturePolicy policy) {
    m_capturePolicy = policy;
}

void UiInputAdapter::set_ui_active(bool active) {
    m_uiActive = active;
}

void UiInputAdapter::set_navigation_dead_zone(float deadZone) {
    m_navDeadZone = std::clamp(deadZone, 0.0f, 1.0f);
}

void UiInputAdapter::set_coordinate_scale(Vector2 scale) {
    m_coordinateScale = scale;
    reset_pointer_state();
}

void UiInputAdapter::set_ui_actions(ActionDef* navigate, ActionDef* submit,
                                    ActionDef* cancel, ActionDef* point,
                                    ActionDef* click, ActionDef* scroll) {
    m_uiNavigate = navigate;
    m_uiSubmit = submit;
    m_uiCancel = cancel;
    m_uiPoint = point;
    m_uiClick = click;
    m_uiScroll = scroll;
    m_lastActionClick = false;
    release_navigation();
}

void UiInputAdapter::flush() {
    m_pointerOverUi = false;
    m_pointerConsumed = false;
    m_keyboardConsumed = false;
    m_textConsumed = false;

    if (m_sink == nullptr)
        return;

    if (m_manager != nullptr) {
        if (m_keyboard == nullptr)
            m_keyboard = static_cast<Keyboard*>(m_manager->device_by_layout("Keyboard"));
        if (m_mouse == nullptr)
            m_mouse = static_cast<Mouse*>(m_manager->device_by_layout("Mouse"));
    }

    if (m_navigationSource == NavigationSource::Actions)
        flush_navigation_actions();
    else
        flush_navigation_raw();

    flush_pointer();
    flush_keyboard();
    flush_text();

    m_pointerOverUi = m_sink->pointer_over_ui();
}

void UiInputAdapter::reset_pointer_state() {
    m_lastMousePos = {-100000.0f, -100000.0f};
    m_lastScroll = {};
    m_lastButtons[0] = false;
    m_lastButtons[1] = false;
    m_lastButtons[2] = false;
    m_mouseActive = false;
    m_lastActionClick = false;
}

void UiInputAdapter::flush_pointer() {
    if (m_uiPoint != nullptr && m_uiPoint->state != nullptr)
        flush_action_pointer();
    else
        flush_mouse_pointer();
}

void UiInputAdapter::flush_mouse_pointer() {
    if (m_mouse == nullptr || m_manager == nullptr) {
        if (m_mouseActive) {
            if (m_sink != nullptr)
                m_pointerConsumed |= m_sink->process_mouse_leave();
            reset_pointer_state();
        }
        return;
    }

    if (m_manager->runtime().focus_state() != FocusState::Normal) {
        if (m_mouseActive) {
            m_pointerConsumed |= m_sink->process_mouse_leave();
            reset_pointer_state();
        }
        return;
    }

    const uint8_t* state = m_mouse->current_state();
    if (state == nullptr)
        return;

    const int modifiers = current_modifiers();
    const Vector2 position = scaled(m_mouse->position()->read_vector2(state),
                                    m_coordinateScale);
    if (position != m_lastMousePos) {
        m_pointerConsumed |= m_sink->process_mouse_move(position.x, position.y, modifiers);
        m_lastMousePos = position;
        m_mouseActive = true;
    }

    auto process_button = [&](int index, Control* control) {
        if (control == nullptr)
            return;
        const bool pressed = control->read_float(state) >= 0.5f;
        if (pressed == m_lastButtons[index])
            return;

        m_pointerConsumed |= m_sink->process_mouse_move(position.x, position.y, modifiers);
        m_pointerConsumed |= pressed
                                 ? m_sink->process_mouse_button_down(index, modifiers)
                                 : m_sink->process_mouse_button_up(index, modifiers);
        m_lastButtons[index] = pressed;
        m_mouseActive = true;
    };

    process_button(0, m_mouse->left_button());
    process_button(1, m_mouse->right_button());
    process_button(2, m_mouse->middle_button());

    const Vector2 scroll = m_mouse->scroll()->read_vector2(state);
    if (scroll != m_lastScroll) {
        if (scroll.x != 0.0f || scroll.y != 0.0f) {
            // GLFW reports positive Y for wheel-up; RmlUi uses positive Y for
            // wheel-down. X is already in the same direction.
            m_pointerConsumed |= m_sink->process_mouse_wheel(scroll.x, -scroll.y, modifiers);
        }
        m_lastScroll = scroll;
        m_mouseActive = true;
    }
}

void UiInputAdapter::flush_action_pointer() {
    ActionState* actionState = m_uiPoint->state;
    if (actionState == nullptr)
        return;

    const int modifiers = current_modifiers();
    const Vector2 position = actionState->read_value<Vector2>(*m_uiPoint);
    if (position != m_lastMousePos) {
        m_pointerConsumed |= m_sink->process_mouse_move(position.x, position.y, modifiers);
        m_lastMousePos = position;
        m_mouseActive = true;
    }

    if (m_uiClick != nullptr && m_uiClick->state == actionState) {
        const bool down = actionState->was_pressed_this_frame(*m_uiClick);
        const bool up = actionState->was_released_this_frame(*m_uiClick);
        if (down && !m_lastActionClick) {
            m_pointerConsumed |= m_sink->process_mouse_move(position.x, position.y, modifiers);
            m_pointerConsumed |= m_sink->process_mouse_button_down(kMouseButtonLeft, modifiers);
            m_lastActionClick = true;
        }
        if (up && m_lastActionClick) {
            m_pointerConsumed |= m_sink->process_mouse_move(position.x, position.y, modifiers);
            m_pointerConsumed |= m_sink->process_mouse_button_up(kMouseButtonLeft, modifiers);
            m_lastActionClick = false;
        }
    }

    if (m_uiScroll != nullptr && m_uiScroll->state == actionState &&
        actionState->was_performed_this_frame(*m_uiScroll)) {
        const Vector2 scroll = actionState->read_value<Vector2>(*m_uiScroll);
        if (scroll.x != 0.0f || scroll.y != 0.0f)
            m_pointerConsumed |= m_sink->process_mouse_wheel(scroll.x, -scroll.y, modifiers);
    }
}

void UiInputAdapter::flush_keyboard() {
    if (m_keyboard == nullptr || m_sink == nullptr)
        return;

    const uint8_t* state = m_keyboard->current_state();
    if (state == nullptr)
        return;

    const int modifiers = current_modifiers();
    for (std::size_t i = 0; i < kKeyCount; ++i) {
        const Key key = static_cast<Key>(i);
        Control* control = key_control(key);
        if (control == nullptr)
            continue;

        const bool down = control->read_float(state) >= 0.5f;
        if (down == m_lastKeys[i])
            continue;

        const bool consumed = down ? emit_sink_key_down(key, modifiers)
                                   : emit_sink_key_up(key, modifiers);
        m_keyboardConsumed |= consumed;
        m_lastKeys[i] = down;
    }
}

void UiInputAdapter::flush_text() {
    if (m_manager == nullptr || m_sink == nullptr)
        return;

    for (char32_t character : m_manager->pending_text()) {
        if (character == U'\r' || character == U'\n')
            continue;
        if (character < 0x20 && character != U'\t')
            continue;
        m_textConsumed |= m_sink->process_text_input(character);
    }
}

void UiInputAdapter::flush_navigation_raw() {
    if (m_sink == nullptr)
        return;

    Gamepad* pad = gamepad();
    if (pad == nullptr) {
        emit_navigation_direction(NavDirection::None);
        update_navigation_submit(false);
        update_navigation_cancel(false);
        return;
    }

    const uint8_t* state = pad->current_state();
    if (state == nullptr) {
        emit_navigation_direction(NavDirection::None);
        update_navigation_submit(false);
        update_navigation_cancel(false);
        return;
    }

    Vector2 nav;
    Control* dpad = pad->dpad();
    Control* stick = pad->left_stick();
    if (dpad != nullptr) {
        const Vector2 dpadValue = dpad->read_vector2(state);
        if (dpadValue.length() > 0.1f)
            nav = dpadValue;
    }
    if (nav.is_zero() && stick != nullptr) {
        const Vector2 stickValue = stick->read_vector2(state);
        if (stickValue.length() >= m_navDeadZone)
            nav = stickValue;
    }

    flush_navigation_vector(nav);

    Control* south = pad->button("buttonSouth");
    Control* east = pad->button("buttonEast");
    update_navigation_submit(south != nullptr && south->read_float(state) >= 0.5f);
    update_navigation_cancel(east != nullptr && east->read_float(state) >= 0.5f);
}

void UiInputAdapter::flush_navigation_actions() {
    if (m_sink == nullptr)
        return;

    Vector2 nav;
    if (m_uiNavigate != nullptr && m_uiNavigate->state != nullptr)
        nav = m_uiNavigate->state->read_value<Vector2>(*m_uiNavigate);
    flush_navigation_vector(nav);

    update_navigation_submit(action_button(m_uiSubmit, true));
    update_navigation_cancel(action_button(m_uiCancel, true));
}

void UiInputAdapter::flush_navigation_vector(Vector2 value) {
    NavDirection direction = NavDirection::None;
    if (value.length() >= m_navDeadZone) {
        if (std::abs(value.x) > std::abs(value.y))
            direction = value.x > 0.0f ? NavDirection::Right : NavDirection::Left;
        else
            direction = value.y > 0.0f ? NavDirection::Up : NavDirection::Down;
    }
    emit_navigation_direction(direction);
}

void UiInputAdapter::emit_navigation_direction(NavDirection direction) {
    if (direction == m_navDirection)
        return;

    if (m_navDirection != NavDirection::None) {
        switch (m_navDirection) {
        case NavDirection::Up: emit_navigation_button(Key::ArrowUp, false); break;
        case NavDirection::Down: emit_navigation_button(Key::ArrowDown, false); break;
        case NavDirection::Left: emit_navigation_button(Key::ArrowLeft, false); break;
        case NavDirection::Right: emit_navigation_button(Key::ArrowRight, false); break;
        default: break;
        }
    }

    if (direction != NavDirection::None) {
        switch (direction) {
        case NavDirection::Up: emit_navigation_button(Key::ArrowUp, true); break;
        case NavDirection::Down: emit_navigation_button(Key::ArrowDown, true); break;
        case NavDirection::Left: emit_navigation_button(Key::ArrowLeft, true); break;
        case NavDirection::Right: emit_navigation_button(Key::ArrowRight, true); break;
        default: break;
        }
    }

    m_navDirection = direction;
}

void UiInputAdapter::emit_navigation_button(Key key, bool down) {
    const int modifiers = current_modifiers();
    m_keyboardConsumed |= down ? emit_sink_key_down(key, modifiers)
                               : emit_sink_key_up(key, modifiers);
}

void UiInputAdapter::update_navigation_submit(bool pressed) {
    if (pressed == m_navSubmit)
        return;
    m_keyboardConsumed |= pressed ? emit_sink_key_down(Key::Enter, current_modifiers())
                                  : emit_sink_key_up(Key::Enter, current_modifiers());
    m_navSubmit = pressed;
}

void UiInputAdapter::update_navigation_cancel(bool pressed) {
    if (pressed == m_navCancel)
        return;
    m_keyboardConsumed |= pressed ? emit_sink_key_down(Key::Escape, current_modifiers())
                                  : emit_sink_key_up(Key::Escape, current_modifiers());
    m_navCancel = pressed;
}

void UiInputAdapter::release_navigation() {
    if (m_sink != nullptr) {
        emit_navigation_direction(NavDirection::None);
        update_navigation_submit(false);
        update_navigation_cancel(false);
    }
    m_navDirection = NavDirection::None;
    m_navSubmit = false;
    m_navCancel = false;
}

bool UiInputAdapter::emit_sink_key_down(Key key, int modifiers) {
    return m_sink != nullptr && consumes(m_sink->process_key_down(key, modifiers));
}

bool UiInputAdapter::emit_sink_key_up(Key key, int modifiers) {
    return m_sink != nullptr && consumes(m_sink->process_key_up(key, modifiers));
}

bool UiInputAdapter::action_button(const ActionDef* action, bool pressed) const {
    if (action == nullptr || action->state == nullptr)
        return false;
    return pressed ? action->state->was_pressed_this_frame(*action)
                   : action->state->was_released_this_frame(*action);
}

int UiInputAdapter::current_modifiers() const {
    if (m_keyboard == nullptr)
        return 0;
    const uint8_t* state = m_keyboard->current_state();
    if (state == nullptr)
        return 0;

    int modifiers = 0;
    if (key_is_down(Key::LeftCtrl) || key_is_down(Key::RightCtrl))
        modifiers |= UiModifierCtrl;
    if (key_is_down(Key::LeftShift) || key_is_down(Key::RightShift))
        modifiers |= UiModifierShift;
    if (key_is_down(Key::LeftAlt) || key_is_down(Key::RightAlt))
        modifiers |= UiModifierAlt;
    if (key_is_down(Key::LeftSuper) || key_is_down(Key::RightSuper))
        modifiers |= UiModifierMeta;
    return modifiers;
}

bool UiInputAdapter::key_is_down(Key key) const {
    if (m_keyboard == nullptr)
        return false;
    const uint8_t* state = m_keyboard->current_state();
    Control* control = key_control(key);
    return state != nullptr && control != nullptr && control->read_float(state) >= 0.5f;
}

Control* UiInputAdapter::key_control(Key key) const {
    return m_keyboard != nullptr ? m_keyboard->key_control(key) : nullptr;
}

Gamepad* UiInputAdapter::gamepad() const {
    if (m_manager == nullptr)
        return nullptr;
    return static_cast<Gamepad*>(m_manager->device_by_layout("Gamepad"));
}

bool UiInputAdapter::pointer_over_ui() const {
    return m_sink != nullptr && m_sink->pointer_over_ui();
}

bool UiInputAdapter::is_capturing_pointer() const {
    switch (m_capturePolicy) {
    case CapturePolicy::None:
        return false;
    case CapturePolicy::Full:
        return m_uiActive;
    case CapturePolicy::PointerAndText:
        return m_pointerConsumed || m_pointerOverUi;
    }
    return false;
}

bool UiInputAdapter::is_capturing_keyboard() const {
    switch (m_capturePolicy) {
    case CapturePolicy::None:
        return false;
    case CapturePolicy::Full:
        return m_uiActive;
    case CapturePolicy::PointerAndText:
        return m_keyboardConsumed || (m_sink != nullptr && m_sink->text_input_focused());
    }
    return false;
}

bool UiInputAdapter::is_capturing_text() const {
    switch (m_capturePolicy) {
    case CapturePolicy::None:
        return false;
    case CapturePolicy::Full:
        return m_uiActive;
    case CapturePolicy::PointerAndText:
        return m_textConsumed || (m_sink != nullptr && m_sink->text_input_focused());
    }
    return false;
}

bool UiInputAdapter::blocks_game_input() const {
    return is_capturing_pointer() || is_capturing_keyboard() || is_capturing_text();
}

} // namespace insys::ui
