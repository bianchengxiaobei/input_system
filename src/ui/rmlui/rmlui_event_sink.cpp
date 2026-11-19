#include "insys/ui/rmlui_event_sink.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include <RmlUi/Core/Input.h>

#include "insys/ui/rmlui_key_map.h"

namespace insys::ui {
namespace {

int to_rml_modifiers(int modifiers) {
    int result = 0;
    if ((modifiers & UiModifierCtrl) != 0)
        result |= Rml::Input::KM_CTRL;
    if ((modifiers & UiModifierShift) != 0)
        result |= Rml::Input::KM_SHIFT;
    if ((modifiers & UiModifierAlt) != 0)
        result |= Rml::Input::KM_ALT;
    if ((modifiers & UiModifierMeta) != 0)
        result |= Rml::Input::KM_META;
    return result;
}

bool is_text_form_control(const Rml::Element* element) {
    if (element == nullptr)
        return false;
    if (dynamic_cast<const Rml::ElementFormControlTextArea*>(element) != nullptr)
        return true;
    if (auto* input = dynamic_cast<const Rml::ElementFormControlInput*>(element)) {
        const Rml::String type = input->GetAttribute<Rml::String>("type", "text");
        return type == "text" || type == "password" || type == "search" ||
               type == "email" || type == "url" || type == "tel" || type == "number";
    }
    return false;
}

} // namespace

RmlUiEventSink::RmlUiEventSink(Rml::Context& context) : m_context(&context) {}

bool RmlUiEventSink::process_mouse_move(float x, float y, int modifiers) {
    if (m_context == nullptr)
        return false;
    return !m_context->ProcessMouseMove(static_cast<int>(x), static_cast<int>(y),
                                        to_rml_modifiers(modifiers));
}

bool RmlUiEventSink::process_mouse_button_down(int button, int modifiers) {
    if (m_context == nullptr)
        return false;
    return !m_context->ProcessMouseButtonDown(button, to_rml_modifiers(modifiers));
}

bool RmlUiEventSink::process_mouse_button_up(int button, int modifiers) {
    if (m_context == nullptr)
        return false;
    return !m_context->ProcessMouseButtonUp(button, to_rml_modifiers(modifiers));
}

bool RmlUiEventSink::process_mouse_wheel(float x, float y, int modifiers) {
    if (m_context == nullptr)
        return false;
    return !m_context->ProcessMouseWheel(Rml::Vector2f{x, y},
                                         to_rml_modifiers(modifiers));
}

bool RmlUiEventSink::process_mouse_leave() {
    if (m_context == nullptr)
        return false;
    return !m_context->ProcessMouseLeave();
}

bool RmlUiEventSink::process_key_down(Key key, int modifiers) {
    if (m_context == nullptr)
        return false;
    const Rml::Input::KeyIdentifier identifier = map_key_to_rmlui(key);
    if (identifier == Rml::Input::KI_UNKNOWN)
        return false;
    return !m_context->ProcessKeyDown(identifier, to_rml_modifiers(modifiers));
}

bool RmlUiEventSink::process_key_up(Key key, int modifiers) {
    if (m_context == nullptr)
        return false;
    const Rml::Input::KeyIdentifier identifier = map_key_to_rmlui(key);
    if (identifier == Rml::Input::KI_UNKNOWN)
        return false;
    return !m_context->ProcessKeyUp(identifier, to_rml_modifiers(modifiers));
}

bool RmlUiEventSink::process_text_input(char32_t character) {
    if (m_context == nullptr)
        return false;
    return !m_context->ProcessTextInput(Rml::Character(character));
}

bool RmlUiEventSink::pointer_over_ui() const {
    if (m_context == nullptr)
        return false;
    Rml::Element* hover = m_context->GetHoverElement();
    return hover != nullptr && hover != m_context->GetRootElement();
}

bool RmlUiEventSink::text_input_focused() const {
    if (m_context == nullptr)
        return false;
    return is_text_form_control(m_context->GetFocusElement());
}

} // namespace insys::ui
