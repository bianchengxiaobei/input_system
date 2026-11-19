#include <catch2/catch_test_macros.hpp>

#include "action_test_helpers.h"
#include "insys/actions/action_asset.h"
#include "insys/actions/action_state.h"
#include "insys/debugger_model.h"

using namespace insys;

TEST_CASE("Debugger model snapshots devices, actions and events") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Keyboard* keyboard = insys_test::add_keyboard(manager);

    ActionAsset asset{"Player"};
    ActionMap& map = asset.add_map("Player");
    ActionDef& jump = map.add_action("Jump", ActionType::Button);
    map.add_binding("Jump", "<Keyboard>/space");
    asset.enable(manager);

    DebuggerModel debugger({32, 64, false});
    debugger.add_asset(asset);
    REQUIRE(debugger.refresh(manager, true));

    REQUIRE(debugger.devices().size() == 1);
    REQUIRE(debugger.devices()[0].deviceId == keyboard->deviceId);
    REQUIRE(debugger.actions().size() == 1);
    REQUIRE(debugger.actions()[0].phase == ActionPhase::Waiting);

    insys_test::set_keys(manager, keyboard, {Key::Space}, 4.0);
    debugger.record_event(low::make_event(low::kStateEvent, low::kHeaderSize,
                                          keyboard->deviceId, 4.0),
                          keyboard);
    REQUIRE(debugger.refresh(manager, true));
    REQUIRE(debugger.actions()[0].phase == ActionPhase::Performed);
    REQUIRE_FALSE(debugger.actions()[0].drivingControl.empty());
    REQUIRE(debugger.recent_events().size() == 1);
    REQUIRE(debugger.recent_events().back().deviceId == keyboard->deviceId);

    debugger.set_paused(true);
    REQUIRE_FALSE(debugger.refresh(manager, false));
    debugger.clear_events();
    REQUIRE(debugger.recent_events().empty());
}

TEST_CASE("Debugger formats axis, button and vector values") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Keyboard* keyboard = insys_test::add_keyboard(manager);

    const uint8_t* state = keyboard->current_state();
    REQUIRE(debugger_format_control_value(*keyboard->key_control(Key::A), state) == "0.00");

    insys_test::set_keys(manager, keyboard, {Key::A}, 1.0);
    state = keyboard->current_state();
    REQUIRE(debugger_format_control_value(*keyboard->key_control(Key::A), state) == "1.00");
}