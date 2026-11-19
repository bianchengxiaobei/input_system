#include <catch2/catch_test_macros.hpp>

#include <string>

#include "insys/backend/itime.h"
#include "insys/ui/rmlui_system_interface.h"

using namespace insys;
using namespace insys::ui;

namespace {

class FakeTime final : public ITime {
public:
    double now() const override { return m_now; }
    double m_now = 5.0;
};

} // namespace

TEST_CASE("RmlUi system interface uses the input time axis") {
    FakeTime time;
    RmlUiSystemInterface system(time);
    REQUIRE(system.GetElapsedTime() == 5.0);
    time.m_now = 7.5;
    REQUIRE(system.GetElapsedTime() == 7.5);
}

TEST_CASE("RmlUi system interface forwards cursor requests") {
    FakeTime time;
    RmlUiSystemInterface system(time);
    std::string requested;
    system.set_mouse_cursor_callback([&](const std::string& cursor) {
        requested = cursor;
    });

    system.SetMouseCursor("text");
    REQUIRE(requested == "text");
    REQUIRE(system.last_mouse_cursor() == "text");

    system.SetMouseCursor("pointer");
    REQUIRE(requested == "pointer");
}

TEST_CASE("RmlUi system interface accepts logging and keyboard activation hooks") {
    FakeTime time;
    RmlUiSystemInterface system(time);
    REQUIRE(system.LogMessage(Rml::Log::LT_INFO, "message"));
    system.ActivateKeyboard(Rml::Vector2f{10.0f, 20.0f}, 16.0f);
    system.DeactivateKeyboard();
}
