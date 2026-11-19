#include "insys/backend/evdev_mapping.h"

#include <algorithm>

namespace insys::backend::evdev {
namespace {

bool set_button(uint32_t& buttons, uint32_t bit, int32_t value) {
    const uint32_t mask = 1u << bit;
    if (value != 0)
        buttons |= mask;
    else
        buttons &= ~mask;
    return true;
}

float normalize_axis(float value, float minimum, float maximum) {
    if (maximum <= minimum)
        return 0.0f;
    return std::clamp((value - minimum) / (maximum - minimum) * 2.0f - 1.0f,
                      -1.0f, 1.0f);
}

float normalize_trigger(float value, float minimum, float maximum) {
    if (maximum <= minimum)
        return 0.0f;
    return std::clamp((value - minimum) / (maximum - minimum), 0.0f, 1.0f);
}

} // namespace

bool map_key(uint16_t code, Key& key) {
    switch (code) {
    case 1: key = Key::Escape; return true;
    case 2: key = Key::Digit1; return true;
    case 3: key = Key::Digit2; return true;
    case 4: key = Key::Digit3; return true;
    case 5: key = Key::Digit4; return true;
    case 6: key = Key::Digit5; return true;
    case 7: key = Key::Digit6; return true;
    case 8: key = Key::Digit7; return true;
    case 9: key = Key::Digit8; return true;
    case 10: key = Key::Digit9; return true;
    case 11: key = Key::Digit0; return true;
    case 12: key = Key::Minus; return true;
    case 13: key = Key::Equal; return true;
    case 14: key = Key::Backspace; return true;
    case 15: key = Key::Tab; return true;
    case 16: key = Key::Q; return true;
    case 17: key = Key::W; return true;
    case 18: key = Key::E; return true;
    case 19: key = Key::R; return true;
    case 20: key = Key::T; return true;
    case 21: key = Key::Y; return true;
    case 22: key = Key::U; return true;
    case 23: key = Key::I; return true;
    case 24: key = Key::O; return true;
    case 25: key = Key::P; return true;
    case 26: key = Key::LeftBracket; return true;
    case 27: key = Key::RightBracket; return true;
    case 28: key = Key::Enter; return true;
    case 29: key = Key::LeftCtrl; return true;
    case 30: key = Key::A; return true;
    case 31: key = Key::S; return true;
    case 32: key = Key::D; return true;
    case 33: key = Key::F; return true;
    case 34: key = Key::G; return true;
    case 35: key = Key::H; return true;
    case 36: key = Key::J; return true;
    case 37: key = Key::K; return true;
    case 38: key = Key::L; return true;
    case 39: key = Key::Semicolon; return true;
    case 40: key = Key::Apostrophe; return true;
    case 41: key = Key::Grave; return true;
    case 42: key = Key::LeftShift; return true;
    case 43: key = Key::Backslash; return true;
    case 44: key = Key::Z; return true;
    case 45: key = Key::X; return true;
    case 46: key = Key::C; return true;
    case 47: key = Key::V; return true;
    case 48: key = Key::B; return true;
    case 49: key = Key::N; return true;
    case 50: key = Key::M; return true;
    case 51: key = Key::Comma; return true;
    case 52: key = Key::Period; return true;
    case 53: key = Key::Slash; return true;
    case 54: key = Key::RightShift; return true;
    case 55: key = Key::NumpadMultiply; return true;
    case 56: key = Key::LeftAlt; return true;
    case 57: key = Key::Space; return true;
    case 58: key = Key::CapsLock; return true;
    case 59: key = Key::F1; return true;
    case 60: key = Key::F2; return true;
    case 61: key = Key::F3; return true;
    case 62: key = Key::F4; return true;
    case 63: key = Key::F5; return true;
    case 64: key = Key::F6; return true;
    case 65: key = Key::F7; return true;
    case 66: key = Key::F8; return true;
    case 67: key = Key::F9; return true;
    case 68: key = Key::F10; return true;
    case 69: key = Key::NumLock; return true;
    case 70: key = Key::ScrollLock; return true;
    case 71: key = Key::Numpad7; return true;
    case 72: key = Key::Numpad8; return true;
    case 73: key = Key::Numpad9; return true;
    case 74: key = Key::NumpadSubtract; return true;
    case 75: key = Key::Numpad4; return true;
    case 76: key = Key::Numpad5; return true;
    case 77: key = Key::Numpad6; return true;
    case 78: key = Key::NumpadAdd; return true;
    case 79: key = Key::Numpad1; return true;
    case 80: key = Key::Numpad2; return true;
    case 81: key = Key::Numpad3; return true;
    case 82: key = Key::Numpad0; return true;
    case 83: key = Key::NumpadDecimal; return true;
    case 87: key = Key::F11; return true;
    case 88: key = Key::F12; return true;
    case 96: key = Key::NumpadEnter; return true;
    case 97: key = Key::RightCtrl; return true;
    case 98: key = Key::NumpadDivide; return true;
    case 100: key = Key::RightAlt; return true;
    case 102: key = Key::Home; return true;
    case 103: key = Key::ArrowUp; return true;
    case 104: key = Key::PageUp; return true;
    case 105: key = Key::ArrowLeft; return true;
    case 106: key = Key::ArrowRight; return true;
    case 107: key = Key::End; return true;
    case 108: key = Key::ArrowDown; return true;
    case 109: key = Key::PageDown; return true;
    case 110: key = Key::Insert; return true;
    case 111: key = Key::Delete; return true;
    case 125: key = Key::LeftSuper; return true;
    case 126: key = Key::RightSuper; return true;
    default: return false;
    }
}

bool MouseMapper::process(uint16_t type, uint16_t code, int32_t value) {
    if (type == kEvRel) {
        if (code == kRelX) {
            state.delta.x += static_cast<float>(value);
            return true;
        }
        if (code == kRelY) {
            state.delta.y += static_cast<float>(value);
            return true;
        }
        if (code == kRelWheel) {
            state.scroll.y += static_cast<float>(value);
            return true;
        }
        if (code == kRelHWheel) {
            state.scroll.x += static_cast<float>(value);
            return true;
        }
        return false;
    }
    if (type != kEvKey)
        return false;

    uint8_t mask = 0;
    switch (code) {
    case kBtnLeft: mask = 1u << 0; break;
    case kBtnRight: mask = 1u << 1; break;
    case kBtnMiddle: mask = 1u << 2; break;
    case kBtnSide: mask = 1u << 3; break;
    case kBtnExtra: mask = 1u << 4; break;
    default: return false;
    }

    if (value != 0)
        state.buttons[0] |= mask;
    else
        state.buttons[0] &= static_cast<uint8_t>(~mask);
    return true;
}

bool GamepadMapper::process(uint16_t type, uint16_t code, int32_t value) {
    if (type == kEvAbs) {
        switch (code) {
        case kAbsX:
            state.leftStick.x =
                normalize_axis(static_cast<float>(value), axisMin, axisMax);
            return true;
        case kAbsY:
            state.leftStick.y =
                normalize_axis(static_cast<float>(value), axisMin, axisMax);
            return true;
        case kAbsRX:
            state.rightStick.x =
                normalize_axis(static_cast<float>(value), axisMin, axisMax);
            return true;
        case kAbsRY:
            state.rightStick.y =
                normalize_axis(static_cast<float>(value), axisMin, axisMax);
            return true;
        case kAbsZ:
            state.leftTrigger =
                normalize_trigger(static_cast<float>(value), triggerMin, triggerMax);
            return true;
        case kAbsRZ:
            state.rightTrigger =
                normalize_trigger(static_cast<float>(value), triggerMin, triggerMax);
            return true;
        case kAbsHat0X:
            state.buttons &= ~((1u << 2) | (1u << 3));
            if (value < 0)
                state.buttons |= 1u << 2;
            else if (value > 0)
                state.buttons |= 1u << 3;
            return true;
        case kAbsHat0Y:
            state.buttons &= ~((1u << 0) | (1u << 1));
            if (value < 0)
                state.buttons |= 1u << 0;
            else if (value > 0)
                state.buttons |= 1u << 1;
            return true;
        default:
            return false;
        }
    }

    if (type != kEvKey)
        return false;

    switch (code) {
    case kBtnSouth: return set_button(state.buttons, 6, value);
    case kBtnEast: return set_button(state.buttons, 5, value);
    case kBtnNorth: return set_button(state.buttons, 4, value);
    case kBtnWest: return set_button(state.buttons, 7, value);
    case kBtnTL: return set_button(state.buttons, 10, value);
    case kBtnTR: return set_button(state.buttons, 11, value);
    case kBtnSelect: return set_button(state.buttons, 13, value);
    case kBtnStart: return set_button(state.buttons, 12, value);
    case kBtnThumbL: return set_button(state.buttons, 8, value);
    case kBtnThumbR: return set_button(state.buttons, 9, value);
    case kBtnDpadUp: return set_button(state.buttons, 0, value);
    case kBtnDpadDown: return set_button(state.buttons, 1, value);
    case kBtnDpadLeft: return set_button(state.buttons, 2, value);
    case kBtnDpadRight: return set_button(state.buttons, 3, value);
    default: return false;
    }
}
} // namespace insys::backend::evdev

