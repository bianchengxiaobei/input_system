#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "action_test_helpers.h"
#include "insys/actions/action_asset.h"
#include "insys/actions/action_state.h"

using namespace insys;

namespace {

struct Fixture {
    insys_test::TestRuntime runtime;
    Manager manager;
    Keyboard* keyboard = nullptr;
    ActionAsset asset{"Player"};
    ActionMap* map = nullptr;

    explicit Fixture(const Settings& settings = Settings{}) : manager(runtime, settings) {
        keyboard = insys_test::add_keyboard(manager);
        map = &asset.add_map("Player");
    }
};

} // namespace

TEST_CASE("Button action follows started/performed/canceled") {
    Fixture fixture;
    ActionDef& jump = fixture.map->add_action("Jump", ActionType::Button);
    fixture.map->add_binding("Jump", "<Keyboard>/space");

    int started = 0;
    int performed = 0;
    int canceled = 0;
    float startedValue = -1.0f;
    jump.started = [&](const CallbackContext& context) {
        ++started;
        startedValue = context.read_value<float>();
    };
    jump.performed = [&](const CallbackContext&) { ++performed; };
    jump.canceled = [&](const CallbackContext&) { ++canceled; };

    fixture.asset.enable(fixture.manager);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Waiting);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::Space}, 1.0);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Performed);
    REQUIRE(started == 1);
    REQUIRE(performed == 1);
    REQUIRE(startedValue == 1.0f);
    REQUIRE(jump.state->was_performed_this_frame(jump));
    REQUIRE(jump.state->was_pressed_this_frame(jump));

    fixture.manager.update();
    REQUIRE_FALSE(jump.state->was_performed_this_frame(jump));

    insys_test::set_keys(fixture.manager, fixture.keyboard, {}, 1.1);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Waiting);
    REQUIRE(canceled == 1);
    REQUIRE(jump.state->was_released_this_frame(jump));
}

TEST_CASE("Value action with composite reads vector and performs repeatedly") {
    Fixture fixture;
    ActionDef& move = fixture.map->add_action("Move", ActionType::Value);
    fixture.map->add_composite_binding("Move", "2DVector(mode=0)");
    fixture.map->add_composite_part("Move", "up", "<Keyboard>/w");
    fixture.map->add_composite_part("Move", "down", "<Keyboard>/s");
    fixture.map->add_composite_part("Move", "left", "<Keyboard>/a");
    fixture.map->add_composite_part("Move", "right", "<Keyboard>/d");

    int performed = 0;
    move.performed = [&](const CallbackContext&) { ++performed; };

    fixture.asset.enable(fixture.manager);
    REQUIRE(move.state->value_type(move) == ValueType::Vector2);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::W}, 2.0);
    REQUIRE(move.state->phase(move) == ActionPhase::Started);
    REQUIRE(performed == 1);
    REQUIRE(move.state->read_value<Vector2>(move).x == 0.0f);
    REQUIRE(move.state->read_value<Vector2>(move).y == 1.0f);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::W, Key::D}, 2.1);
    REQUIRE(performed == 2);
    const Vector2 diagonal = move.state->read_value<Vector2>(move);
    REQUIRE(diagonal.x == Catch::Approx(0.70710678f));
    REQUIRE(diagonal.y == Catch::Approx(0.70710678f));

    insys_test::set_keys(fixture.manager, fixture.keyboard, {}, 2.2);
    REQUIRE(move.state->phase(move) == ActionPhase::Waiting);
}

TEST_CASE("Pass-through action performs on every value change") {
    Fixture fixture;
    ActionDef& fire = fixture.map->add_action("Fire", ActionType::PassThrough);
    fixture.map->add_binding("Fire", "<Keyboard>/a");

    int performed = 0;
    float lastValue = -1.0f;
    fire.performed = [&](const CallbackContext& context) {
        ++performed;
        lastValue = context.read_value<float>();
    };

    fixture.asset.enable(fixture.manager);
    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::A}, 3.0);
    REQUIRE(performed == 1);
    REQUIRE(lastValue == 1.0f);
    REQUIRE(fire.state->phase(fire) == ActionPhase::Waiting);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {}, 3.1);
    REQUIRE(performed == 2);
    REQUIRE(lastValue == 0.0f);
    REQUIRE(fire.state->phase(fire) == ActionPhase::Waiting);
}

TEST_CASE("Conflicting bindings prefer the strongest actuation") {
    Fixture fixture;
    ActionDef& jump = fixture.map->add_action("Jump", ActionType::Button);
    fixture.map->add_binding("Jump", "<Keyboard>/space");
    fixture.map->add_binding("Jump", "<Keyboard>/enter");

    fixture.asset.enable(fixture.manager);
    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::Space}, 4.0);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Performed);
    REQUIRE(jump.state->active_binding(jump) == 0);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::Space, Key::Enter}, 4.1);
    REQUIRE(jump.state->active_binding(jump) == 0);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::Enter}, 4.2);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Performed);
    REQUIRE(jump.state->active_binding(jump) == 1);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {}, 4.3);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Waiting);
}

TEST_CASE("Value actions check initial state when enabled") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Keyboard* keyboard = insys_test::add_keyboard(manager);

    insys_test::set_keys(manager, keyboard, {Key::W}, 5.0);

    ActionMap map("Player");
    ActionDef& move = map.add_action("Move", ActionType::Value);
    map.add_binding("Move", "<Keyboard>/w");

    int performed = 0;
    move.performed = [&](const CallbackContext&) { ++performed; };

    map.enable(manager);
    manager.update();

    REQUIRE(move.state->phase(move) == ActionPhase::Started);
    REQUIRE(performed == 1);
    REQUIRE(move.state->read_value<float>(move) == 1.0f);
}

TEST_CASE("Button actions skip initial state check by default") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Keyboard* keyboard = insys_test::add_keyboard(manager);

    insys_test::set_keys(manager, keyboard, {Key::Space}, 6.0);

    ActionMap map("Player");
    ActionDef& jump = map.add_action("Jump", ActionType::Button);
    map.add_binding("Jump", "<Keyboard>/space");

    int started = 0;
    jump.started = [&](const CallbackContext&) { ++started; };

    map.enable(manager);
    manager.update();

    REQUIRE(jump.state->phase(jump) == ActionPhase::Waiting);
    REQUIRE(started == 0);
}

TEST_CASE("Gamepad analog values go through binding processors") {
    Fixture fixture;
    Gamepad* gamepad = insys_test::add_gamepad(fixture.manager);
    ActionDef& move = fixture.map->add_action("Move", ActionType::Value);
    fixture.map->add_binding("Move", "<Gamepad>/leftStick").processors =
        "stickDeadzone(min=0.2,max=0.9)";

    fixture.asset.enable(fixture.manager);
    insys_test::set_gamepad_stick(fixture.manager, gamepad, {0.5f, 0.0f}, 4.5);

    REQUIRE(move.state->phase(move) == ActionPhase::Started);
    const Vector2 value = move.state->read_value<Vector2>(move);
    REQUIRE(value.x == Catch::Approx((0.5f - 0.2f) / (0.9f - 0.2f)));
    REQUIRE(value.y == 0.0f);
}

TEST_CASE("State monitors detect direction changes with equal magnitude") {
    Fixture fixture;
    Gamepad* gamepad = insys_test::add_gamepad(fixture.manager);
    ActionDef& move = fixture.map->add_action("Move", ActionType::Value);
    fixture.map->add_binding("Move", "<Gamepad>/leftStick");

    int performed = 0;
    move.performed = [&](const CallbackContext&) { ++performed; };

    fixture.asset.enable(fixture.manager);
    insys_test::set_gamepad_stick(fixture.manager, gamepad, {0.5f, 0.0f}, 4.55);
    insys_test::set_gamepad_stick(fixture.manager, gamepad, {0.0f, 0.5f}, 4.56);

    REQUIRE(performed == 2);
    const Vector2 value = move.state->read_value<Vector2>(move);
    REQUIRE(value.x == 0.0f);
    REQUIRE(value.y == Catch::Approx(0.5f));
}

TEST_CASE("Axis composite combines positive and negative parts") {
    Fixture fixture;
    ActionDef& throttle = fixture.map->add_action("Throttle", ActionType::Value);
    fixture.map->add_composite_binding("Throttle", "Axis");
    fixture.map->add_composite_part("Throttle", "negative", "<Keyboard>/a");
    fixture.map->add_composite_part("Throttle", "positive", "<Keyboard>/d");

    fixture.asset.enable(fixture.manager);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::A}, 4.6);
    REQUIRE(throttle.state->read_value<float>(throttle) == -1.0f);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::D}, 4.7);
    REQUIRE(throttle.state->read_value<float>(throttle) == 1.0f);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::A, Key::D}, 4.8);
    REQUIRE(throttle.state->read_value<float>(throttle) == 0.0f);
}

TEST_CASE("Vector3 composite combines six directions") {
    Fixture fixture;
    ActionDef& move = fixture.map->add_action("Move3D", ActionType::Value);
    fixture.map->add_composite_binding("Move3D", "3DVector");
    fixture.map->add_composite_part("Move3D", "up", "<Keyboard>/w");
    fixture.map->add_composite_part("Move3D", "down", "<Keyboard>/s");
    fixture.map->add_composite_part("Move3D", "left", "<Keyboard>/a");
    fixture.map->add_composite_part("Move3D", "right", "<Keyboard>/d");
    fixture.map->add_composite_part("Move3D", "forward", "<Keyboard>/e");
    fixture.map->add_composite_part("Move3D", "backward", "<Keyboard>/q");

    fixture.asset.enable(fixture.manager);
    REQUIRE(move.state->value_type(move) == ValueType::Vector3);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::W, Key::E}, 4.85);
    const Vector3 value = move.state->read_value<Vector3>(move);
    REQUIRE(value.x == 0.0f);
    REQUIRE(value.y == Catch::Approx(0.70710678f));
    REQUIRE(value.z == Catch::Approx(0.70710678f));
}

TEST_CASE("Removing a device does not leave dangling action controls") {
    Fixture fixture;
    Gamepad* gamepad = insys_test::add_gamepad(fixture.manager);
    ActionDef& move = fixture.map->add_action("Move", ActionType::Value);
    fixture.map->add_binding("Move", "<Gamepad>/leftStick");

    fixture.asset.enable(fixture.manager);
    insys_test::set_gamepad_stick(fixture.manager, gamepad, {0.5f, 0.0f}, 4.9);
    REQUIRE(move.state->active_control(move) != nullptr);

    fixture.manager.remove_device(gamepad);
    fixture.manager.update();

    REQUIRE(move.state->active_control(move) == nullptr);
    REQUIRE(move.state->read_value<Vector2>(move).x == 0.0f);
}

TEST_CASE("Destroying an enabled asset disposes actions cleanly") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    insys_test::add_keyboard(manager);

    {
        ActionAsset asset("Player");
        ActionMap& map = asset.add_map("Player");
        map.add_action("Jump", ActionType::Button);
        map.add_binding("Jump", "<Keyboard>/space");
        asset.enable(manager);
        REQUIRE(manager.state_monitors().count() == 1);
    }

    REQUIRE(manager.state_monitors().count() == 0);
    manager.update();
}

TEST_CASE("Enable and disable manage monitors idempotently") {
    Fixture fixture;
    fixture.map->add_action("Jump", ActionType::Button);
    fixture.map->add_action("Move", ActionType::Value);
    ActionDef& jump = fixture.map->actions[0];
    ActionDef& move = fixture.map->actions[1];
    fixture.map->add_binding("Jump", "<Keyboard>/space");
    fixture.map->add_binding("Move", "<Keyboard>/w");

    fixture.asset.enable(fixture.manager);
    REQUIRE(fixture.manager.state_monitors().count() == 2);

    fixture.asset.enable(fixture.manager);
    REQUIRE(fixture.manager.state_monitors().count() == 2);

    fixture.asset.disable();
    REQUIRE(fixture.manager.state_monitors().count() == 0);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Disabled);
    REQUIRE(move.state->phase(move) == ActionPhase::Disabled);

    fixture.asset.disable();
    REQUIRE(fixture.manager.state_monitors().count() == 0);

    fixture.asset.enable(fixture.manager);
    REQUIRE(fixture.manager.state_monitors().count() == 2);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Waiting);
}

TEST_CASE("Removing a device detaches enabled action monitors before destruction") {
    Fixture fixture;
    ActionDef& jump = fixture.map->add_action("Jump", ActionType::Button);
    fixture.map->add_binding("Jump", "<Keyboard>/space");
    fixture.asset.enable(fixture.manager);
    REQUIRE(fixture.manager.state_monitors().count() == 1);

    fixture.manager.remove_device(fixture.keyboard);
    REQUIRE(fixture.manager.state_monitors().count() == 0);
    REQUIRE(jump.state != nullptr);
}
TEST_CASE("OneModifier gates a button until its modifier is pressed") {
    Fixture fixture;
    ActionDef& shortcut = fixture.map->add_action("Shortcut", ActionType::Button);
    fixture.map->add_composite_binding("Shortcut", "ButtonWithOneModifier");
    fixture.map->add_composite_part("Shortcut", "modifier", "<Keyboard>/leftShift");
    fixture.map->add_composite_part("Shortcut", "button", "<Keyboard>/a");

    fixture.asset.enable(fixture.manager);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::A}, 10.0);
    REQUIRE(shortcut.state->phase(shortcut) == ActionPhase::Waiting);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::A, Key::LeftShift}, 10.1);
    REQUIRE(shortcut.state->phase(shortcut) == ActionPhase::Performed);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {}, 10.2);
    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::LeftShift}, 10.3);
    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::LeftShift, Key::A}, 10.4);
    REQUIRE(shortcut.state->phase(shortcut) == ActionPhase::Performed);
}

TEST_CASE("Modifier order follows shortcutKeysConsumeInput") {
    Settings settings;
    settings.shortcutKeysConsumeInput = true;
    Fixture fixture(settings);
    ActionDef& shortcut = fixture.map->add_action("Shortcut", ActionType::Button);
    fixture.map->add_composite_binding("Shortcut", "ButtonWithOneModifier");
    fixture.map->add_composite_part("Shortcut", "modifier", "<Keyboard>/leftShift");
    fixture.map->add_composite_part("Shortcut", "button", "<Keyboard>/a");

    fixture.asset.enable(fixture.manager);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::A}, 11.0);
    REQUIRE(shortcut.state->read_composite_part_press_time(0, 1) ==
            Catch::Approx(11.0));
    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::A, Key::LeftShift}, 11.1);
    REQUIRE(shortcut.state->read_composite_part_press_time(0, 0) ==
            Catch::Approx(11.1));
    REQUIRE(shortcut.state->phase(shortcut) == ActionPhase::Waiting);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {}, 11.2);
    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::LeftShift}, 11.3);
    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::LeftShift, Key::A}, 11.4);
    REQUIRE(shortcut.state->phase(shortcut) == ActionPhase::Performed);
}

TEST_CASE("Modifier order can be overridden to unordered") {
    Settings settings;
    settings.shortcutKeysConsumeInput = true;
    Fixture fixture(settings);
    ActionDef& shortcut = fixture.map->add_action("Shortcut", ActionType::Button);
    fixture.map->add_composite_binding("Shortcut", "ButtonWithOneModifier(modifiersOrder=2)");
    fixture.map->add_composite_part("Shortcut", "modifier", "<Keyboard>/leftShift");
    fixture.map->add_composite_part("Shortcut", "button", "<Keyboard>/a");

    fixture.asset.enable(fixture.manager);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::A}, 12.0);
    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::A, Key::LeftShift}, 12.1);
    REQUIRE(shortcut.state->phase(shortcut) == ActionPhase::Performed);
}

TEST_CASE("OneModifier preserves the gated Vector2 value") {
    Fixture fixture;
    Gamepad* gamepad = insys_test::add_gamepad(fixture.manager);
    ActionDef& look = fixture.map->add_action("Look", ActionType::Value);
    fixture.map->add_composite_binding("Look", "OneModifier");
    fixture.map->add_composite_part("Look", "modifier", "<Keyboard>/leftShift");
    fixture.map->add_composite_part("Look", "binding", "<Gamepad>/leftStick");

    fixture.asset.enable(fixture.manager);
    REQUIRE(look.state->value_type(look) == ValueType::Vector2);

    insys_test::set_gamepad_stick(fixture.manager, gamepad, {0.5f, -0.25f}, 13.0);
    REQUIRE(look.state->read_value<Vector2>(look).is_zero());

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::LeftShift}, 13.1);
    const Vector2 value = look.state->read_value<Vector2>(look);
    REQUIRE(value.x == Catch::Approx(0.5f));
    REQUIRE(value.y == Catch::Approx(-0.25f));

    insys_test::set_keys(fixture.manager, fixture.keyboard, {}, 13.2);
    REQUIRE(look.state->read_value<Vector2>(look).is_zero());
}

TEST_CASE("TwoModifiers requires both modifiers") {
    Fixture fixture;
    ActionDef& shortcut = fixture.map->add_action("Shortcut", ActionType::Button);
    fixture.map->add_composite_binding("Shortcut", "ButtonWithTwoModifiers");
    fixture.map->add_composite_part("Shortcut", "modifier1", "<Keyboard>/leftShift");
    fixture.map->add_composite_part("Shortcut", "modifier2", "<Keyboard>/leftCtrl");
    fixture.map->add_composite_part("Shortcut", "button", "<Keyboard>/a");

    fixture.asset.enable(fixture.manager);

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::LeftShift}, 14.0);
    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::LeftShift, Key::A}, 14.1);
    REQUIRE(shortcut.state->phase(shortcut) == ActionPhase::Waiting);

    insys_test::set_keys(fixture.manager, fixture.keyboard,
                         {Key::LeftShift, Key::LeftCtrl, Key::A}, 14.2);
    REQUIRE(shortcut.state->phase(shortcut) == ActionPhase::Performed);
}

TEST_CASE("CompensateDirection runs with the manager screen orientation") {
    Fixture fixture;
    fixture.runtime.set_screen_orientation(
        static_cast<int>(ScreenOrientation::LandscapeLeft));
    ActionDef& move = fixture.map->add_action("Move", ActionType::Value);
    fixture.map->add_composite_binding("Move", "3DVector");
    fixture.map->add_composite_part("Move", "up", "<Keyboard>/w");
    fixture.map->add_composite_part("Move", "down", "<Keyboard>/s");
    fixture.map->add_composite_part("Move", "left", "<Keyboard>/a");
    fixture.map->add_composite_part("Move", "right", "<Keyboard>/d");
    fixture.map->add_composite_part("Move", "forward", "<Keyboard>/e");
    fixture.map->add_composite_part("Move", "backward", "<Keyboard>/q");
    fixture.map->bindings[0].processors = "CompensateDirection";

    fixture.asset.enable(fixture.manager);
    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::W}, 15.0);

    const Vector3 value = move.state->read_value<Vector3>(move);
    REQUIRE(value.x == Catch::Approx(-1.0f));
    REQUIRE(value.y == Catch::Approx(0.0f).margin(1e-6f));
    REQUIRE(value.z == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Initial state check preserves modifier press order") {
    Settings settings;
    settings.shortcutKeysConsumeInput = true;
    Fixture fixture(settings);
    ActionDef& shortcut = fixture.map->add_action("Shortcut", ActionType::Value);
    fixture.map->add_composite_binding("Shortcut", "OneModifier");
    fixture.map->add_composite_part("Shortcut", "binding", "<Keyboard>/a");
    fixture.map->add_composite_part("Shortcut", "modifier", "<Keyboard>/leftShift");

    insys_test::set_keys(fixture.manager, fixture.keyboard,
                         {Key::LeftShift, Key::A}, 16.0);
    fixture.asset.enable(fixture.manager);
    fixture.manager.update();

    REQUIRE(shortcut.state->phase(shortcut) == ActionPhase::Started);
    REQUIRE(shortcut.state->read_value<float>(shortcut) == 1.0f);
}
