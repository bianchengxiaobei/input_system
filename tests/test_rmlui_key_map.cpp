#include <catch2/catch_test_macros.hpp>

#include <RmlUi/Core/Input.h>

#include "insys/low/key.h"
#include "insys/ui/rmlui_key_map.h"

using namespace insys;
using namespace insys::ui;
using KI = Rml::Input::KeyIdentifier;

TEST_CASE("RmlUi key map covers text, navigation and editing keys") {
    REQUIRE(map_key_to_rmlui(Key::A) == KI::KI_A);
    REQUIRE(map_key_to_rmlui(Key::Z) == KI::KI_Z);
    REQUIRE(map_key_to_rmlui(Key::Digit0) == KI::KI_0);
    REQUIRE(map_key_to_rmlui(Key::Digit9) == KI::KI_9);
    REQUIRE(map_key_to_rmlui(Key::F1) == KI::KI_F1);
    REQUIRE(map_key_to_rmlui(Key::F12) == KI::KI_F12);

    REQUIRE(map_key_to_rmlui(Key::ArrowUp) == KI::KI_UP);
    REQUIRE(map_key_to_rmlui(Key::ArrowDown) == KI::KI_DOWN);
    REQUIRE(map_key_to_rmlui(Key::ArrowLeft) == KI::KI_LEFT);
    REQUIRE(map_key_to_rmlui(Key::ArrowRight) == KI::KI_RIGHT);

    REQUIRE(map_key_to_rmlui(Key::Enter) == KI::KI_RETURN);
    REQUIRE(map_key_to_rmlui(Key::Escape) == KI::KI_ESCAPE);
    REQUIRE(map_key_to_rmlui(Key::Tab) == KI::KI_TAB);
    REQUIRE(map_key_to_rmlui(Key::Backspace) == KI::KI_BACK);
    REQUIRE(map_key_to_rmlui(Key::Delete) == KI::KI_DELETE);
    REQUIRE(map_key_to_rmlui(Key::Home) == KI::KI_HOME);
    REQUIRE(map_key_to_rmlui(Key::End) == KI::KI_END);
    REQUIRE(map_key_to_rmlui(Key::PageUp) == KI::KI_PRIOR);
    REQUIRE(map_key_to_rmlui(Key::PageDown) == KI::KI_NEXT);
}

TEST_CASE("RmlUi key map covers modifiers and punctuation") {
    REQUIRE(map_key_to_rmlui(Key::LeftShift) == KI::KI_LSHIFT);
    REQUIRE(map_key_to_rmlui(Key::RightShift) == KI::KI_RSHIFT);
    REQUIRE(map_key_to_rmlui(Key::LeftCtrl) == KI::KI_LCONTROL);
    REQUIRE(map_key_to_rmlui(Key::RightCtrl) == KI::KI_RCONTROL);
    REQUIRE(map_key_to_rmlui(Key::LeftAlt) == KI::KI_LMENU);
    REQUIRE(map_key_to_rmlui(Key::RightAlt) == KI::KI_RMENU);
    REQUIRE(map_key_to_rmlui(Key::LeftSuper) == KI::KI_LMETA);
    REQUIRE(map_key_to_rmlui(Key::RightSuper) == KI::KI_RMETA);

    REQUIRE(map_key_to_rmlui(Key::Space) == KI::KI_SPACE);
    REQUIRE(map_key_to_rmlui(Key::Minus) == KI::KI_OEM_MINUS);
    REQUIRE(map_key_to_rmlui(Key::Equal) == KI::KI_OEM_PLUS);
    REQUIRE(map_key_to_rmlui(Key::Comma) == KI::KI_OEM_COMMA);
    REQUIRE(map_key_to_rmlui(Key::Period) == KI::KI_OEM_PERIOD);
    REQUIRE(map_key_to_rmlui(Key::Slash) == KI::KI_OEM_2);
    REQUIRE(map_key_to_rmlui(Key::NumpadEnter) == KI::KI_NUMPADENTER);
}

TEST_CASE("Unsupported keys map to KI_UNKNOWN") {
    REQUIRE(map_key_to_rmlui(Key::Count) == KI::KI_UNKNOWN);
}
