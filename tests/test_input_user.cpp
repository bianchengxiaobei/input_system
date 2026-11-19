#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "action_test_helpers.h"
#include "insys/actions/action_asset.h"
#include "insys/actions/action_state.h"
#include "insys/actions/control_scheme.h"
#include "insys/actions/input_user.h"
#include "insys/actions/player_input.h"

using namespace insys;

namespace {

ActionAsset make_player_actions() {
    ActionAsset asset("Player");

    ActionMap& gameplay = asset.add_map("Gameplay");
    gameplay.add_action("KeyboardJump", ActionType::Button);
    gameplay.add_action("GamepadJump", ActionType::Button);
    gameplay.add_binding("KeyboardJump", "<Keyboard>/space").groups = "Keyboard";
    gameplay.add_binding("GamepadJump", "<Gamepad>/buttonSouth").groups = "Gamepad";

    ActionMap& ui = asset.add_map("UI");
    ui.add_action("Submit", ActionType::Button);
    ui.add_binding("Submit", "<Keyboard>/enter").groups = "Keyboard";

    ControlScheme keyboard;
    keyboard.name = "Keyboard";
    keyboard.bindingGroup = "Keyboard";
    keyboard.devices = {"Keyboard"};
    asset.controlSchemes.push_back(keyboard);

    ControlScheme gamepad;
    gamepad.name = "Gamepad";
    gamepad.bindingGroup = "Gamepad";
    gamepad.devices = {"Gamepad"};
    asset.controlSchemes.push_back(gamepad);

    return asset;
}

struct UserFixture {
    insys_test::TestRuntime runtime;
    Manager manager{runtime};

    ~UserFixture() {
        InputUser::leave_all(&manager);
    }
};

} // namespace

TEST_CASE("InputUser auto-pairs a matching control scheme") {
    UserFixture fixture;
    Gamepad* gamepad = insys_test::add_gamepad(fixture.manager);
    ActionAsset actions = make_player_actions();

    PlayerInput* player = PlayerInput::join(fixture.manager, actions, "Gameplay");
    REQUIRE(player != nullptr);
    REQUIRE(player->control_scheme() != nullptr);
    REQUIRE(player->control_scheme()->name == "Gamepad");
    REQUIRE(player->paired_devices().size() == 1);
    REQUIRE(player->paired_devices()[0] == gamepad);
    REQUIRE(player->current_action_map() == "Gameplay");
    REQUIRE(player->action_map_enabled("Gameplay"));
    REQUIRE_FALSE(player->action_map_enabled("UI"));
    REQUIRE_FALSE(player->has_missing_required_devices());
}

TEST_CASE("Each player owns an isolated action copy and device filter") {
    UserFixture fixture;
    Keyboard* firstKeyboard = insys_test::add_keyboard(fixture.manager);
    Keyboard* secondKeyboard = insys_test::add_keyboard(fixture.manager);
    ActionAsset actions = make_player_actions();

    PlayerInput* first = PlayerInput::join(fixture.manager, actions, "Gameplay");
    PlayerInput* second = PlayerInput::join(fixture.manager, actions, "Gameplay");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(first->paired_devices()[0] == firstKeyboard);
    REQUIRE(second->paired_devices()[0] == secondKeyboard);
    REQUIRE(PlayerInput::all().size() == 2);
    REQUIRE(first->player_index() == 0);
    REQUIRE(second->player_index() == 1);

    ActionDef* firstJump = first->actions().find_action("KeyboardJump");
    ActionDef* secondJump = second->actions().find_action("KeyboardJump");
    REQUIRE(firstJump != nullptr);
    REQUIRE(secondJump != nullptr);
    REQUIRE(firstJump != secondJump);

    insys_test::set_keys(fixture.manager, firstKeyboard, {Key::Space}, 20.0);
    REQUIRE(firstJump->state->phase(*firstJump) == ActionPhase::Performed);
    REQUIRE(secondJump->state->phase(*secondJump) == ActionPhase::Waiting);

    insys_test::set_keys(fixture.manager, firstKeyboard, {}, 20.1);
    insys_test::set_keys(fixture.manager, secondKeyboard, {Key::Space}, 20.2);
    REQUIRE(secondJump->state->phase(*secondJump) == ActionPhase::Performed);

    REQUIRE(InputUser::leave(first));
    REQUIRE(PlayerInput::all().size() == 1);
    REQUIRE(second->player_index() == 0);
}

TEST_CASE("InputUser pairs newly connected devices when auto pairing is enabled") {
    UserFixture fixture;
    ActionAsset actions = make_player_actions();
    PlayerInput* player = PlayerInput::join(fixture.manager, actions, "Gameplay");
    REQUIRE(player != nullptr);
    REQUIRE(player->paired_devices().empty());

    Keyboard* keyboard = insys_test::add_keyboard(fixture.manager);
    REQUIRE(player->paired_devices().size() == 1);
    REQUIRE(player->paired_devices()[0] == keyboard);
    REQUIRE(player->control_scheme()->name == "Keyboard");

    ActionDef* jump = player->actions().find_action("KeyboardJump");
    REQUIRE(jump != nullptr);
    insys_test::set_keys(fixture.manager, keyboard, {Key::Space}, 21.0);
    REQUIRE(jump->state->phase(*jump) == ActionPhase::Performed);
}

TEST_CASE("Unpairing and re-pairing rebuilds the filtered bindings") {
    UserFixture fixture;
    Gamepad* gamepad = insys_test::add_gamepad(fixture.manager);
    ActionAsset actions = make_player_actions();
    PlayerInput* player = PlayerInput::join(fixture.manager, actions, "Gameplay");
    REQUIRE(player != nullptr);

    ActionDef* jump = player->actions().find_action("GamepadJump");
    REQUIRE(jump != nullptr);
    insys_test::set_gamepad_buttons(fixture.manager, gamepad, 1u << 6, 22.0);
    REQUIRE(jump->state->phase(*jump) == ActionPhase::Performed);

    REQUIRE(player->unpair_device(gamepad));
    REQUIRE(player->paired_devices().empty());
    REQUIRE(player->has_missing_required_devices());
    REQUIRE(jump->state->phase(*jump) == ActionPhase::Waiting);

    REQUIRE(player->pair_device(gamepad));
    REQUIRE(player->paired_devices().size() == 1);
    REQUIRE_FALSE(player->has_missing_required_devices());
    insys_test::set_gamepad_buttons(fixture.manager, gamepad, 0, 22.1);
    insys_test::set_gamepad_buttons(fixture.manager, gamepad, 1u << 6, 22.2);
    REQUIRE(jump->state->phase(*jump) == ActionPhase::Performed);
}

TEST_CASE("Removing a paired device clears stale action bindings") {
    UserFixture fixture;
    Gamepad* gamepad = insys_test::add_gamepad(fixture.manager);
    ActionAsset actions = make_player_actions();
    PlayerInput* player = PlayerInput::join(fixture.manager, actions, "Gameplay");
    REQUIRE(player != nullptr);
    ActionDef* jump = player->actions().find_action("GamepadJump");
    REQUIRE(jump != nullptr);
    insys_test::set_gamepad_buttons(fixture.manager, gamepad, 1u << 6, 23.0);
    REQUIRE(jump->state->phase(*jump) == ActionPhase::Performed);

    fixture.manager.remove_device(gamepad);
    REQUIRE(player->paired_devices().empty());
    REQUIRE(jump->state->phase(*jump) == ActionPhase::Waiting);
    REQUIRE(jump->state->read_value<float>(*jump) == 0.0f);
}

TEST_CASE("InputUser can activate a control scheme explicitly") {
    UserFixture fixture;
    Keyboard* keyboard = insys_test::add_keyboard(fixture.manager);
    Gamepad* gamepad = insys_test::add_gamepad(fixture.manager);
    ActionAsset actions = make_player_actions();
    PlayerInput* player = PlayerInput::join(fixture.manager, actions, "Gameplay");
    REQUIRE(player != nullptr);
    REQUIRE(player->control_scheme() != nullptr);
    REQUIRE(player->control_scheme()->name == "Keyboard");

    REQUIRE(player->set_control_scheme("Gamepad"));
    REQUIRE(player->control_scheme() != nullptr);
    REQUIRE(player->control_scheme()->name == "Gamepad");
    REQUIRE(player->has_missing_required_devices());

    ActionDef* gamepadJump = player->actions().find_action("GamepadJump");
    REQUIRE(gamepadJump != nullptr);
    insys_test::set_gamepad_buttons(fixture.manager, gamepad, 1u << 6, 25.0);
    REQUIRE(gamepadJump->state->phase(*gamepadJump) == ActionPhase::Waiting);

    REQUIRE(player->pair_device(gamepad));
    REQUIRE_FALSE(player->has_missing_required_devices());
    insys_test::set_gamepad_buttons(fixture.manager, gamepad, 0, 25.1);
    insys_test::set_gamepad_buttons(fixture.manager, gamepad, 1u << 6, 25.2);
    REQUIRE(gamepadJump->state->phase(*gamepadJump) == ActionPhase::Performed);

    ActionDef* keyboardJump = player->actions().find_action("KeyboardJump");
    insys_test::set_keys(fixture.manager, keyboard, {Key::Space}, 25.3);
    REQUIRE(keyboardJump->state->phase(*keyboardJump) == ActionPhase::Waiting);
}

TEST_CASE("Control-scheme binding masks filter grouped bindings") {
    UserFixture fixture;
    Keyboard* keyboard = insys_test::add_keyboard(fixture.manager);
    Gamepad* gamepad = insys_test::add_gamepad(fixture.manager);
    ActionAsset actions = make_player_actions();

    PlayerInput* keyboardPlayer = PlayerInput::join(fixture.manager, actions, "Gameplay");
    PlayerInput* gamepadPlayer = PlayerInput::join(fixture.manager, actions, "Gameplay");
    REQUIRE(keyboardPlayer != nullptr);
    REQUIRE(gamepadPlayer != nullptr);
    REQUIRE(keyboardPlayer->control_scheme()->name == "Keyboard");
    REQUIRE(gamepadPlayer->control_scheme()->name == "Gamepad");

    ActionDef* keyboardJump = keyboardPlayer->actions().find_action("KeyboardJump");
    ActionDef* gamepadJump = keyboardPlayer->actions().find_action("GamepadJump");
    REQUIRE(keyboardJump != nullptr);
    REQUIRE(gamepadJump != nullptr);

    insys_test::set_keys(fixture.manager, keyboard, {Key::Space}, 24.0);
    REQUIRE(keyboardJump->state->phase(*keyboardJump) == ActionPhase::Performed);
    REQUIRE(gamepadJump->state->phase(*gamepadJump) == ActionPhase::Waiting);

    ActionDef* otherKeyboardJump = gamepadPlayer->actions().find_action("KeyboardJump");
    ActionDef* otherGamepadJump = gamepadPlayer->actions().find_action("GamepadJump");
    insys_test::set_gamepad_buttons(fixture.manager, gamepad, 1u << 6, 24.1);
    REQUIRE(otherGamepadJump->state->phase(*otherGamepadJump) == ActionPhase::Performed);
    REQUIRE(otherKeyboardJump->state->phase(*otherKeyboardJump) == ActionPhase::Waiting);
}

TEST_CASE("InputUser exposes per-player action map management") {
    UserFixture fixture;
    Keyboard* keyboard = insys_test::add_keyboard(fixture.manager);
    ActionAsset actions = make_player_actions();
    PlayerInput* player = PlayerInput::join(fixture.manager, actions, "Gameplay");
    REQUIRE(player != nullptr);

    REQUIRE(player->switch_current_action_map("UI"));
    REQUIRE(player->current_action_map() == "UI");
    REQUIRE(player->action_map_enabled("UI"));
    REQUIRE_FALSE(player->action_map_enabled("Gameplay"));
    REQUIRE_FALSE(player->switch_current_action_map("Missing"));

    REQUIRE(player->enable_action_map("Gameplay"));
    REQUIRE(player->current_action_map() == "Gameplay");
    REQUIRE(player->action_map_enabled("Gameplay"));

    player->disable_all_action_maps();
    REQUIRE_FALSE(player->action_map_enabled("Gameplay"));
    REQUIRE_FALSE(player->action_map_enabled("UI"));

    player->enable_all_action_maps();
    REQUIRE(player->action_map_enabled("Gameplay"));
    REQUIRE(player->action_map_enabled("UI"));
    REQUIRE(player->current_action_map().empty());

    player->disable();
    REQUIRE_FALSE(player->enabled());
    player->enable();
    REQUIRE(player->enabled());
    REQUIRE(player->action_map_enabled("Gameplay"));
    REQUIRE(player->action_map_enabled("UI"));
    (void)keyboard;
}

TEST_CASE("Manager destruction removes remaining players") {
    insys_test::TestRuntime runtime;
    {
        Manager manager(runtime);
        insys_test::add_keyboard(manager);
        ActionAsset actions = make_player_actions();
        REQUIRE(PlayerInput::join(manager, actions, "Gameplay") != nullptr);
        REQUIRE(PlayerInput::all().size() == 1);
    }
    REQUIRE(PlayerInput::all().empty());
}

TEST_CASE("InputUser join enables all maps by default") {
    UserFixture fixture;
    insys_test::add_keyboard(fixture.manager);
    ActionAsset actions = make_player_actions();
    InputUser* user = InputUser::join(fixture.manager, actions);
    REQUIRE(user != nullptr);
    REQUIRE(user->action_map_enabled("Gameplay"));
    REQUIRE(user->action_map_enabled("UI"));
    REQUIRE(user->control_scheme() != nullptr);
    REQUIRE(user->control_scheme()->name == "Keyboard");
}

TEST_CASE("Control scheme matching prefers non-optional schemes") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Keyboard* keyboard = insys_test::add_keyboard(manager);
    Gamepad* gamepad = insys_test::add_gamepad(manager);

    ControlScheme fallback;
    fallback.name = "Fallback";
    fallback.isOptional = true;
    fallback.devices = {"Gamepad"};

    ControlScheme keyboardScheme;
    keyboardScheme.name = "Keyboard";
    keyboardScheme.devices = {"Keyboard"};

    const auto match = find_control_scheme({fallback, keyboardScheme}, {keyboard, gamepad});
    REQUIRE(match.has_value());
    REQUIRE(match->matched);
    REQUIRE(match->scheme.name == "Keyboard");
    REQUIRE(match->successful());
}

TEST_CASE("Control scheme matching reports missing required devices") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Keyboard* keyboard = insys_test::add_keyboard(manager);

    ControlScheme gamepad;
    gamepad.name = "Gamepad";
    gamepad.devices = {"Gamepad"};

    const ControlSchemeMatch match = match_control_scheme(gamepad, {keyboard});
    REQUIRE(match.matched);
    REQUIRE_FALSE(match.successful());
    REQUIRE(match.missingDevices.size() == 1);
    REQUIRE(match.missingDevices[0] == "Gamepad");
}

