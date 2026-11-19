#include "insys/ui/rmlui_key_map.h"

namespace insys::ui {

Rml::Input::KeyIdentifier map_key_to_rmlui(Key key) {
    using KI = Rml::Input::KeyIdentifier;

    switch (key) {
    case Key::A: return KI::KI_A;
    case Key::B: return KI::KI_B;
    case Key::C: return KI::KI_C;
    case Key::D: return KI::KI_D;
    case Key::E: return KI::KI_E;
    case Key::F: return KI::KI_F;
    case Key::G: return KI::KI_G;
    case Key::H: return KI::KI_H;
    case Key::I: return KI::KI_I;
    case Key::J: return KI::KI_J;
    case Key::K: return KI::KI_K;
    case Key::L: return KI::KI_L;
    case Key::M: return KI::KI_M;
    case Key::N: return KI::KI_N;
    case Key::O: return KI::KI_O;
    case Key::P: return KI::KI_P;
    case Key::Q: return KI::KI_Q;
    case Key::R: return KI::KI_R;
    case Key::S: return KI::KI_S;
    case Key::T: return KI::KI_T;
    case Key::U: return KI::KI_U;
    case Key::V: return KI::KI_V;
    case Key::W: return KI::KI_W;
    case Key::X: return KI::KI_X;
    case Key::Y: return KI::KI_Y;
    case Key::Z: return KI::KI_Z;

    case Key::Digit0: return KI::KI_0;
    case Key::Digit1: return KI::KI_1;
    case Key::Digit2: return KI::KI_2;
    case Key::Digit3: return KI::KI_3;
    case Key::Digit4: return KI::KI_4;
    case Key::Digit5: return KI::KI_5;
    case Key::Digit6: return KI::KI_6;
    case Key::Digit7: return KI::KI_7;
    case Key::Digit8: return KI::KI_8;
    case Key::Digit9: return KI::KI_9;

    case Key::F1: return KI::KI_F1;
    case Key::F2: return KI::KI_F2;
    case Key::F3: return KI::KI_F3;
    case Key::F4: return KI::KI_F4;
    case Key::F5: return KI::KI_F5;
    case Key::F6: return KI::KI_F6;
    case Key::F7: return KI::KI_F7;
    case Key::F8: return KI::KI_F8;
    case Key::F9: return KI::KI_F9;
    case Key::F10: return KI::KI_F10;
    case Key::F11: return KI::KI_F11;
    case Key::F12: return KI::KI_F12;

    case Key::Space: return KI::KI_SPACE;
    case Key::Enter: return KI::KI_RETURN;
    case Key::Escape: return KI::KI_ESCAPE;
    case Key::Tab: return KI::KI_TAB;
    case Key::Backspace: return KI::KI_BACK;
    case Key::Delete: return KI::KI_DELETE;
    case Key::Insert: return KI::KI_INSERT;
    case Key::Home: return KI::KI_HOME;
    case Key::End: return KI::KI_END;
    case Key::PageUp: return KI::KI_PRIOR;
    case Key::PageDown: return KI::KI_NEXT;

    case Key::ArrowUp: return KI::KI_UP;
    case Key::ArrowDown: return KI::KI_DOWN;
    case Key::ArrowLeft: return KI::KI_LEFT;
    case Key::ArrowRight: return KI::KI_RIGHT;

    case Key::LeftShift: return KI::KI_LSHIFT;
    case Key::RightShift: return KI::KI_RSHIFT;
    case Key::LeftCtrl: return KI::KI_LCONTROL;
    case Key::RightCtrl: return KI::KI_RCONTROL;
    case Key::LeftAlt: return KI::KI_LMENU;
    case Key::RightAlt: return KI::KI_RMENU;
    case Key::LeftSuper: return KI::KI_LMETA;
    case Key::RightSuper: return KI::KI_RMETA;

    case Key::Minus: return KI::KI_OEM_MINUS;
    case Key::Equal: return KI::KI_OEM_PLUS;
    case Key::LeftBracket: return KI::KI_OEM_4;
    case Key::RightBracket: return KI::KI_OEM_6;
    case Key::Backslash: return KI::KI_OEM_5;
    case Key::Semicolon: return KI::KI_OEM_1;
    case Key::Apostrophe: return KI::KI_OEM_7;
    case Key::Comma: return KI::KI_OEM_COMMA;
    case Key::Period: return KI::KI_OEM_PERIOD;
    case Key::Slash: return KI::KI_OEM_2;
    case Key::Grave: return KI::KI_OEM_3;
    case Key::CapsLock: return KI::KI_CAPITAL;
    case Key::NumLock: return KI::KI_NUMLOCK;
    case Key::ScrollLock: return KI::KI_SCROLL;

    case Key::Numpad0: return KI::KI_NUMPAD0;
    case Key::Numpad1: return KI::KI_NUMPAD1;
    case Key::Numpad2: return KI::KI_NUMPAD2;
    case Key::Numpad3: return KI::KI_NUMPAD3;
    case Key::Numpad4: return KI::KI_NUMPAD4;
    case Key::Numpad5: return KI::KI_NUMPAD5;
    case Key::Numpad6: return KI::KI_NUMPAD6;
    case Key::Numpad7: return KI::KI_NUMPAD7;
    case Key::Numpad8: return KI::KI_NUMPAD8;
    case Key::Numpad9: return KI::KI_NUMPAD9;
    case Key::NumpadAdd: return KI::KI_ADD;
    case Key::NumpadSubtract: return KI::KI_SUBTRACT;
    case Key::NumpadMultiply: return KI::KI_MULTIPLY;
    case Key::NumpadDivide: return KI::KI_DIVIDE;
    case Key::NumpadDecimal: return KI::KI_DECIMAL;
    case Key::NumpadEnter: return KI::KI_NUMPADENTER;

    case Key::Count:
    default:
        return KI::KI_UNKNOWN;
    }
}

} // namespace insys::ui
