#pragma once

#include <cstdint>

#include "insys/low/key.h"

namespace insys::ui {

enum UiModifier : int {
    UiModifierCtrl = 1 << 0,
    UiModifierShift = 1 << 1,
    UiModifierAlt = 1 << 2,
    UiModifierMeta = 1 << 3,
};

/// Abstraction over the concrete UI backend.
///
/// The input adapter talks to this interface instead of RmlUi directly so the
/// translation logic can be tested with a fake sink. Implementations return
/// true when the UI consumed the event.
class UiEventSink {
public:
    virtual ~UiEventSink() = default;

    virtual bool process_mouse_move(float x, float y, int modifiers) = 0;
    virtual bool process_mouse_button_down(int button, int modifiers) = 0;
    virtual bool process_mouse_button_up(int button, int modifiers) = 0;
    virtual bool process_mouse_wheel(float x, float y, int modifiers) = 0;
    virtual bool process_mouse_leave() = 0;

    virtual bool process_key_down(Key key, int modifiers) = 0;
    virtual bool process_key_up(Key key, int modifiers) = 0;
    virtual bool process_text_input(char32_t character) = 0;

    virtual bool pointer_over_ui() const = 0;
    virtual bool text_input_focused() const = 0;
};

} // namespace insys::ui
