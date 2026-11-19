#pragma once

#include <RmlUi/Core.h>

#include "insys/ui/ui_event_sink.h"

namespace insys::ui {

/// UiEventSink implementation that forwards events into an RmlUi context.
///
/// RmlUi's Process* methods return "not consumed" for most events and
/// "not interacting" for pointer events, so this class normalizes both to the
/// UiEventSink convention: true means the UI consumed the event.
class RmlUiEventSink final : public UiEventSink {
public:
    explicit RmlUiEventSink(Rml::Context& context);

    void set_context(Rml::Context* context) { m_context = context; }
    Rml::Context* context() const { return m_context; }

    bool process_mouse_move(float x, float y, int modifiers) override;
    bool process_mouse_button_down(int button, int modifiers) override;
    bool process_mouse_button_up(int button, int modifiers) override;
    bool process_mouse_wheel(float x, float y, int modifiers) override;
    bool process_mouse_leave() override;

    bool process_key_down(Key key, int modifiers) override;
    bool process_key_up(Key key, int modifiers) override;
    bool process_text_input(char32_t character) override;

    bool pointer_over_ui() const override;
    bool text_input_focused() const override;

private:
    Rml::Context* m_context = nullptr;
};

} // namespace insys::ui
