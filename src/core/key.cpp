#include "insys/low/key.h"

#include <iterator>

namespace insys {
namespace {

const char* const kKeyNames[] = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
    "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",

    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",

    "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12",

    "space", "enter", "escape", "tab", "backspace",
    "delete", "insert", "home", "end", "pageUp", "pageDown",

    "upArrow", "downArrow", "leftArrow", "rightArrow",

    "leftShift", "rightShift", "leftCtrl", "rightCtrl",
    "leftAlt", "rightAlt", "leftMeta", "rightMeta",

    "minus", "equals", "leftBracket", "rightBracket", "backslash",
    "semicolon", "quote", "comma", "period", "slash", "backquote",
    "capsLock", "numLock", "scrollLock",

    "numpad0", "numpad1", "numpad2", "numpad3", "numpad4",
    "numpad5", "numpad6", "numpad7", "numpad8", "numpad9",
    "numpadPlus", "numpadMinus", "numpadMultiply", "numpadDivide",
    "numpadPeriod", "numpadEnter",
};

static_assert(std::size(kKeyNames) == kKeyCount, "key name table out of sync");

} // namespace

const char* key_name(Key key) {
    const auto index = static_cast<std::size_t>(key);
    if (index >= kKeyCount)
        return "unknown";
    return kKeyNames[index];
}

const char* key_layout_name(Key key) {
    return key_name(key);
}

} // namespace insys
