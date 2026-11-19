#include "glfw_key_map.h"

#include <GLFW/glfw3.h>

namespace insys::backend {

bool map_glfw_key(int glfwKey, Key& outKey) {
    if (glfwKey >= GLFW_KEY_A && glfwKey <= GLFW_KEY_Z) {
        outKey = static_cast<Key>(static_cast<int>(Key::A) + (glfwKey - GLFW_KEY_A));
        return true;
    }
    if (glfwKey >= GLFW_KEY_0 && glfwKey <= GLFW_KEY_9) {
        outKey = static_cast<Key>(static_cast<int>(Key::Digit0) + (glfwKey - GLFW_KEY_0));
        return true;
    }
    if (glfwKey >= GLFW_KEY_F1 && glfwKey <= GLFW_KEY_F12) {
        outKey = static_cast<Key>(static_cast<int>(Key::F1) + (glfwKey - GLFW_KEY_F1));
        return true;
    }
    if (glfwKey >= GLFW_KEY_KP_0 && glfwKey <= GLFW_KEY_KP_9) {
        outKey = static_cast<Key>(static_cast<int>(Key::Numpad0) + (glfwKey - GLFW_KEY_KP_0));
        return true;
    }

    switch (glfwKey) {
    case GLFW_KEY_SPACE: outKey = Key::Space; return true;
    case GLFW_KEY_ENTER: outKey = Key::Enter; return true;
    case GLFW_KEY_ESCAPE: outKey = Key::Escape; return true;
    case GLFW_KEY_TAB: outKey = Key::Tab; return true;
    case GLFW_KEY_BACKSPACE: outKey = Key::Backspace; return true;
    case GLFW_KEY_DELETE: outKey = Key::Delete; return true;
    case GLFW_KEY_INSERT: outKey = Key::Insert; return true;
    case GLFW_KEY_HOME: outKey = Key::Home; return true;
    case GLFW_KEY_END: outKey = Key::End; return true;
    case GLFW_KEY_PAGE_UP: outKey = Key::PageUp; return true;
    case GLFW_KEY_PAGE_DOWN: outKey = Key::PageDown; return true;

    case GLFW_KEY_UP: outKey = Key::ArrowUp; return true;
    case GLFW_KEY_DOWN: outKey = Key::ArrowDown; return true;
    case GLFW_KEY_LEFT: outKey = Key::ArrowLeft; return true;
    case GLFW_KEY_RIGHT: outKey = Key::ArrowRight; return true;

    case GLFW_KEY_LEFT_SHIFT: outKey = Key::LeftShift; return true;
    case GLFW_KEY_RIGHT_SHIFT: outKey = Key::RightShift; return true;
    case GLFW_KEY_LEFT_CONTROL: outKey = Key::LeftCtrl; return true;
    case GLFW_KEY_RIGHT_CONTROL: outKey = Key::RightCtrl; return true;
    case GLFW_KEY_LEFT_ALT: outKey = Key::LeftAlt; return true;
    case GLFW_KEY_RIGHT_ALT: outKey = Key::RightAlt; return true;
    case GLFW_KEY_LEFT_SUPER: outKey = Key::LeftSuper; return true;
    case GLFW_KEY_RIGHT_SUPER: outKey = Key::RightSuper; return true;

    case GLFW_KEY_MINUS: outKey = Key::Minus; return true;
    case GLFW_KEY_EQUAL: outKey = Key::Equal; return true;
    case GLFW_KEY_LEFT_BRACKET: outKey = Key::LeftBracket; return true;
    case GLFW_KEY_RIGHT_BRACKET: outKey = Key::RightBracket; return true;
    case GLFW_KEY_BACKSLASH: outKey = Key::Backslash; return true;
    case GLFW_KEY_SEMICOLON: outKey = Key::Semicolon; return true;
    case GLFW_KEY_APOSTROPHE: outKey = Key::Apostrophe; return true;
    case GLFW_KEY_COMMA: outKey = Key::Comma; return true;
    case GLFW_KEY_PERIOD: outKey = Key::Period; return true;
    case GLFW_KEY_SLASH: outKey = Key::Slash; return true;
    case GLFW_KEY_GRAVE_ACCENT: outKey = Key::Grave; return true;
    case GLFW_KEY_CAPS_LOCK: outKey = Key::CapsLock; return true;
    case GLFW_KEY_NUM_LOCK: outKey = Key::NumLock; return true;
    case GLFW_KEY_SCROLL_LOCK: outKey = Key::ScrollLock; return true;

    case GLFW_KEY_KP_ADD: outKey = Key::NumpadAdd; return true;
    case GLFW_KEY_KP_SUBTRACT: outKey = Key::NumpadSubtract; return true;
    case GLFW_KEY_KP_MULTIPLY: outKey = Key::NumpadMultiply; return true;
    case GLFW_KEY_KP_DIVIDE: outKey = Key::NumpadDivide; return true;
    case GLFW_KEY_KP_DECIMAL: outKey = Key::NumpadDecimal; return true;
    case GLFW_KEY_KP_ENTER: outKey = Key::NumpadEnter; return true;

    default:
        return false;
    }
}

} // namespace insys::backend
