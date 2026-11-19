#pragma once

#include <functional>
#include <string>

#include <RmlUi/Core/SystemInterface.h>

#include "insys/backend/itime.h"

namespace insys::ui {

class RmlUiSystemInterface : public Rml::SystemInterface {
public:
    using MouseCursorCallback = std::function<void(const std::string&)>;

    explicit RmlUiSystemInterface(ITime& time);

    // Optional host hook. The demo maps the names to GLFW standard cursors.
    void set_mouse_cursor_callback(MouseCursorCallback callback);
    const std::string& last_mouse_cursor() const { return m_lastMouseCursor; }

    double GetElapsedTime() override;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
    void SetMouseCursor(const Rml::String& cursorName) override;
    void GetClipboardText(Rml::String& text) override;
    void SetClipboardText(const Rml::String& text) override;
    void ActivateKeyboard(Rml::Vector2f caretPosition, float lineHeight) override;
    void DeactivateKeyboard() override;

private:
    ITime& m_time;
    MouseCursorCallback m_mouseCursorCallback;
    std::string m_lastMouseCursor;
};

} // namespace insys::ui
