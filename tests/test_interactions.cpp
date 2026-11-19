#include <catch2/catch_test_macros.hpp>

#include "action_test_helpers.h"
#include "insys/actions/action_asset.h"
#include "insys/actions/action_state.h"

using namespace insys;

namespace {

struct InteractionFixture {
    insys_test::TestRuntime runtime;
    Manager manager{runtime};
    Keyboard* keyboard = insys_test::add_keyboard(manager);
    ActionAsset asset{"Player"};
    ActionMap* map = nullptr;

    InteractionFixture() { map = &asset.add_map("Player"); }

    void press(Key key, double time) {
        runtime.m_now = time;
        insys_test::set_keys(manager, keyboard, {key}, time);
    }

    void release(Key key, double time) {
        runtime.m_now = time;
        insys_test::set_keys(manager, keyboard, {}, time);
    }

    void tick(double time) {
        runtime.m_now = time;
        manager.update();
    }
};

ActionDef& add_action(ActionMap& map, const char* name, ActionType type = ActionType::Button) {
    ActionDef& action = map.add_action(name, type);
    return action;
}

} // namespace

TEST_CASE("Tap interaction performs on short press and cancels on timeout") {
    InteractionFixture fixture;
    ActionDef& jump = add_action(*fixture.map, "Jump");
    fixture.map->add_binding("Jump", "<Keyboard>/space", "space");
    fixture.map->bindings.back().interactions = "Tap(duration=0.2)";

    int performed = 0;
    int canceled = 0;
    jump.performed = [&](const CallbackContext&) { ++performed; };
    jump.canceled = [&](const CallbackContext&) { ++canceled; };
    fixture.asset.enable(fixture.manager);

    fixture.press(Key::Space, 1.0);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Started);
    REQUIRE(fixture.manager.state_monitors().timeout_count() == 1);
    fixture.release(Key::Space, 1.1);
    REQUIRE(fixture.manager.state_monitors().timeout_count() == 0);
    REQUIRE(performed == 1);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Waiting);

    fixture.press(Key::Space, 2.0);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Started);
    fixture.tick(2.25);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Waiting);
    REQUIRE(canceled >= 1);
    fixture.release(Key::Space, 2.3);
    REQUIRE(performed == 1);
}

TEST_CASE("Hold interaction performs after duration and stays performed") {
    InteractionFixture fixture;
    ActionDef& fire = add_action(*fixture.map, "Fire");
    fixture.map->add_binding("Fire", "<Keyboard>/space", "space");
    fixture.map->bindings.back().interactions = "Hold(duration=0.2)";

    int performed = 0;
    int canceled = 0;
    fire.performed = [&](const CallbackContext&) { ++performed; };
    fire.canceled = [&](const CallbackContext&) { ++canceled; };
    fixture.asset.enable(fixture.manager);

    fixture.press(Key::Space, 1.0);
    REQUIRE(fire.state->phase(fire) == ActionPhase::Started);
    fixture.tick(1.25);
    REQUIRE(fire.state->phase(fire) == ActionPhase::Performed);
    REQUIRE(performed == 1);

    fixture.release(Key::Space, 1.3);
    REQUIRE(fire.state->phase(fire) == ActionPhase::Waiting);
    REQUIRE(canceled == 1);

    fixture.press(Key::Space, 2.0);
    fixture.release(Key::Space, 2.05);
    REQUIRE(performed == 1);
    REQUIRE(canceled == 2);
}

TEST_CASE("Slow tap performs only when released after duration") {
    InteractionFixture fixture;
    ActionDef& jump = add_action(*fixture.map, "Jump");
    fixture.map->add_binding("Jump", "<Keyboard>/space", "space");
    fixture.map->bindings.back().interactions = "SlowTap(duration=0.2)";

    int performed = 0;
    int canceled = 0;
    jump.performed = [&](const CallbackContext&) { ++performed; };
    jump.canceled = [&](const CallbackContext&) { ++canceled; };
    fixture.asset.enable(fixture.manager);

    fixture.press(Key::Space, 1.0);
    fixture.release(Key::Space, 1.1);
    REQUIRE(performed == 0);
    REQUIRE(canceled == 1);

    fixture.press(Key::Space, 2.0);
    fixture.release(Key::Space, 2.3);
    REQUIRE(performed == 1);
}

TEST_CASE("Press interaction supports press, release and press-and-release") {
    InteractionFixture fixture;
    fixture.map->add_action("PressOnly", ActionType::Button);
    fixture.map->add_binding("PressOnly", "<Keyboard>/a");
    fixture.map->bindings.back().interactions = "Press(behavior=0)";

    fixture.map->add_action("ReleaseOnly", ActionType::Button);
    fixture.map->add_binding("ReleaseOnly", "<Keyboard>/b");
    fixture.map->bindings.back().interactions = "Press(behavior=1)";

    fixture.map->add_action("PressAndRelease", ActionType::Button);
    fixture.map->add_binding("PressAndRelease", "<Keyboard>/c");
    fixture.map->bindings.back().interactions = "Press(behavior=2)";

    ActionDef* pressOnly = fixture.map->find_action("PressOnly");
    ActionDef* releaseOnly = fixture.map->find_action("ReleaseOnly");
    ActionDef* pressAndRelease = fixture.map->find_action("PressAndRelease");
    int pressCount = 0;
    int releaseCount = 0;
    int bothCount = 0;
    pressOnly->performed = [&](const CallbackContext&) { ++pressCount; };
    releaseOnly->performed = [&](const CallbackContext&) { ++releaseCount; };
    pressAndRelease->performed = [&](const CallbackContext&) { ++bothCount; };

    fixture.asset.enable(fixture.manager);
    fixture.press(Key::A, 1.0);
    REQUIRE(pressCount == 1);
    REQUIRE(releaseCount == 0);
    REQUIRE(bothCount == 0);
    fixture.release(Key::A, 1.05);

    fixture.press(Key::B, 1.1);
    REQUIRE(releaseCount == 0);
    fixture.release(Key::B, 1.15);
    REQUIRE(releaseCount == 1);

    fixture.press(Key::C, 1.2);
    REQUIRE(bothCount == 1);
    fixture.release(Key::C, 1.25);
    REQUIRE(bothCount == 2);
}

TEST_CASE("Multi tap requires two taps within the delay window") {
    InteractionFixture fixture;
    ActionDef& jump = add_action(*fixture.map, "Jump");
    fixture.map->add_binding("Jump", "<Keyboard>/space", "space");
    fixture.map->bindings.back().interactions =
        "MultiTap(tapCount=2,tapTime=0.2,tapDelay=0.5)";

    int performed = 0;
    int canceled = 0;
    jump.performed = [&](const CallbackContext&) { ++performed; };
    jump.canceled = [&](const CallbackContext&) { ++canceled; };
    fixture.asset.enable(fixture.manager);

    fixture.press(Key::Space, 1.0);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Started);
    fixture.release(Key::Space, 1.05);
    REQUIRE(performed == 0);
    fixture.press(Key::Space, 1.2);
    fixture.release(Key::Space, 1.25);
    REQUIRE(performed == 1);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Waiting);

    fixture.press(Key::Space, 2.0);
    fixture.release(Key::Space, 2.05);
    fixture.tick(2.7);
    REQUIRE(performed == 1);
    REQUIRE(canceled >= 1);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Waiting);
}

TEST_CASE("Starting a new interaction timeout removes the previous one") {
    InteractionFixture fixture;
    ActionDef& jump = add_action(*fixture.map, "Jump");
    fixture.map->add_binding("Jump", "<Keyboard>/space", "space");
    fixture.map->bindings.back().interactions = "Tap(duration=0.2)";

    int canceled = 0;
    jump.canceled = [&](const CallbackContext&) { ++canceled; };
    fixture.asset.enable(fixture.manager);

    fixture.press(Key::Space, 1.0);
    fixture.release(Key::Space, 1.05);
    fixture.press(Key::Space, 1.1);
    fixture.tick(1.3);
    REQUIRE(canceled <= 1);
    fixture.release(Key::Space, 1.35);
}

TEST_CASE("Destroying an asset with an active interaction timer is safe") {
    InteractionFixture fixture;
    ActionDef& jump = add_action(*fixture.map, "Jump");
    fixture.map->add_binding("Jump", "<Keyboard>/space", "space");
    fixture.map->bindings.back().interactions = "Tap(duration=0.2)";
    fixture.asset.enable(fixture.manager);
    fixture.press(Key::Space, 1.0);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Started);
}
