#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "action_test_helpers.h"
#include "insys/actions/action_asset.h"
#include "insys/actions/action_state.h"
#include "insys/device_states.h"
#include "insys/low/event.h"
#include "insys/low/state_format.h"
#include "insys/ui/ui_input_adapter.h"

using namespace insys;
using namespace insys::ui;

namespace {

class TestRuntime final : public IRuntime {
public:
    void set_manager(Manager* manager) override { m_manager = manager; }
    int allocate_device_id() override { return m_nextDeviceId++; }
    void queue_event(const low::Event* event) override {
        if (m_manager != nullptr)
            m_manager->queue_event(event);
    }
    long device_command(int, DeviceCommand*) override { return -1; }

    double now() const override { return m_now; }
    double now_for_fixed() const override { return m_now; }
    float unscaled_game_time() const override { return static_cast<float>(m_now); }
    Vector2 screen_size() const override { return {1280.0f, 720.0f}; }
    int screen_orientation() const override { return 0; }
    bool run_in_background() const override { return false; }
    void set_run_in_background(bool) override {}
    FocusState focus_state() const override { return m_focus; }
    void set_focus_state(FocusState value) override { m_focus = value; }
    float polling_frequency() const override { return 60.0f; }
    void set_polling_frequency(float) override {}

    double m_now = 1.0;
    FocusState m_focus = FocusState::Normal;

private:
    Manager* m_manager = nullptr;
    int m_nextDeviceId = 1;
};

class FakeSink final : public UiEventSink {
public:
    struct Move {
        float x;
        float y;
        int modifiers;
    };
    struct Button {
        int index;
        int modifiers;
    };

    bool process_mouse_move(float x, float y, int modifiers) override {
        moves.push_back({x, y, modifiers});
        return consumePointer || hover;
    }
    bool process_mouse_button_down(int button, int modifiers) override {
        buttonDowns.push_back({button, modifiers});
        return consumePointer;
    }
    bool process_mouse_button_up(int button, int modifiers) override {
        buttonUps.push_back({button, modifiers});
        return consumePointer;
    }
    bool process_mouse_wheel(float x, float y, int modifiers) override {
        wheels.push_back({x, y, modifiers});
        return consumePointer;
    }
    bool process_mouse_leave() override {
        ++mouseLeaves;
        return consumePointer;
    }

    bool process_key_down(Key key, int modifiers) override {
        keyDowns.push_back({key, modifiers});
        return consumeKeyboard;
    }
    bool process_key_up(Key key, int modifiers) override {
        keyUps.push_back({key, modifiers});
        return consumeKeyboard;
    }
    bool process_text_input(char32_t character) override {
        textInputs.push_back(character);
        return consumeText;
    }

    bool pointer_over_ui() const override { return hover; }
    bool text_input_focused() const override { return textFocused; }

    bool consumePointer = false;
    bool consumeKeyboard = false;
    bool consumeText = false;
    bool hover = false;
    bool textFocused = false;

    std::vector<Move> moves;
    std::vector<Button> buttonDowns;
    std::vector<Button> buttonUps;
    std::vector<Move> wheels;
    std::vector<std::pair<Key, int>> keyDowns;
    std::vector<std::pair<Key, int>> keyUps;
    std::vector<char32_t> textInputs;
    int mouseLeaves = 0;
};

Mouse* add_mouse(Manager& manager) {
    DeviceDescription description;
    description.interfaceName = "Test";
    description.product = "Test Mouse";
    return static_cast<Mouse*>(manager.create_device("Mouse", description));
}

void set_mouse_state(Manager& manager, Mouse* mouse, const MouseState& state,
                     double time) {
    const auto event = low::make_state_event(kFormatMouse, &state, sizeof(state),
                                             mouse->deviceId, time);
    manager.queue_event(reinterpret_cast<const low::Event*>(event.data()));
    manager.update(UpdateType::Dynamic);
}

void set_gamepad_state(Manager& manager, Gamepad* gamepad, const GamepadState& state,
                       double time) {
    const auto event = low::make_state_event(kFormatGamepad, &state, sizeof(state),
                                             gamepad->deviceId, time);
    manager.queue_event(reinterpret_cast<const low::Event*>(event.data()));
    manager.update(UpdateType::Dynamic);
}

struct Fixture {
    TestRuntime runtime;
    Manager manager{runtime};
    Keyboard* keyboard = insys_test::add_keyboard(manager);
    Mouse* mouse = add_mouse(manager);
    FakeSink sink;
    UiInputAdapter adapter{sink, manager};
};

} // namespace

TEST_CASE("Adapter maps mouse movement, buttons and wheel") {
    Fixture fixture;
    fixture.adapter.set_coordinate_scale({2.0f, 3.0f});
    fixture.sink.consumePointer = true;

    MouseState state;
    state.position = {10.0f, 20.0f};
    state.scroll = {1.0f, 2.0f};
    state.buttons[0] = 0x01;
    set_mouse_state(fixture.manager, fixture.mouse, state, 2.0);

    fixture.adapter.flush();
    REQUIRE_FALSE(fixture.sink.moves.empty());
    REQUIRE(fixture.sink.moves.front().x == 20.0f);
    REQUIRE(fixture.sink.moves.front().y == 60.0f);
    REQUIRE(fixture.sink.buttonDowns.size() == 1);
    REQUIRE(fixture.sink.buttonDowns[0].index == 0);
    REQUIRE(fixture.sink.wheels.size() == 1);
    REQUIRE(fixture.sink.wheels[0].x == 1.0f);
    REQUIRE(fixture.sink.wheels[0].y == -2.0f);
    REQUIRE(fixture.adapter.is_consuming_pointer());

    state.buttons[0] = 0;
    state.scroll = {};
    set_mouse_state(fixture.manager, fixture.mouse, state, 2.1);
    fixture.adapter.flush();
    REQUIRE(fixture.sink.buttonUps.size() == 1);
    REQUIRE(fixture.sink.buttonUps[0].index == 0);
    REQUIRE(fixture.sink.wheels.size() == 1);
}

TEST_CASE("Adapter forwards keyboard, text and modifier state") {
    Fixture fixture;
    fixture.sink.consumeKeyboard = true;
    fixture.sink.textFocused = true;

    insys_test::set_keys(fixture.manager, fixture.keyboard,
                         {Key::LeftShift, Key::A}, 3.0);
    fixture.adapter.flush();

    REQUIRE(fixture.sink.keyDowns.size() == 2);
    bool sawA = false;
    for (const auto& [key, modifiers] : fixture.sink.keyDowns) {
        if (key == Key::A) {
            sawA = true;
            REQUIRE((modifiers & UiModifierShift) != 0);
        }
    }
    REQUIRE(sawA);
    REQUIRE(fixture.adapter.is_capturing_keyboard());

    const auto text = low::make_text_event(U'x', fixture.keyboard->deviceId, 3.1);
    fixture.manager.queue_event(reinterpret_cast<const low::Event*>(text.data()));
    fixture.manager.update(UpdateType::Dynamic);
    fixture.sink.consumeText = true;
    fixture.adapter.flush();
    REQUIRE(fixture.sink.textInputs.size() == 1);
    REQUIRE(fixture.sink.textInputs[0] == U'x');
    REQUIRE(fixture.adapter.is_capturing_text());
    fixture.manager.clear_pending_text();

    insys_test::set_keys(fixture.manager, fixture.keyboard, {}, 3.2);
    fixture.adapter.flush();
    REQUIRE(fixture.sink.keyUps.size() == 2);
}

TEST_CASE("Capture policies gate only the configured input categories") {
    Fixture fixture;
    fixture.adapter.set_capture_policy(CapturePolicy::None);
    fixture.sink.hover = true;
    fixture.adapter.flush();
    REQUIRE_FALSE(fixture.adapter.is_capturing_pointer());
    REQUIRE_FALSE(fixture.adapter.is_capturing_keyboard());
    REQUIRE_FALSE(fixture.adapter.is_capturing_text());

    fixture.adapter.set_capture_policy(CapturePolicy::PointerAndText);
    fixture.sink.consumePointer = true;
    fixture.adapter.flush();
    REQUIRE(fixture.adapter.is_capturing_pointer());
    REQUIRE_FALSE(fixture.adapter.is_capturing_keyboard());
    REQUIRE_FALSE(fixture.adapter.is_capturing_text());

    fixture.sink.textFocused = true;
    fixture.adapter.flush();
    REQUIRE(fixture.adapter.is_capturing_keyboard());
    REQUIRE(fixture.adapter.is_capturing_text());

    fixture.adapter.set_capture_policy(CapturePolicy::Full);
    fixture.adapter.set_ui_active(false);
    fixture.adapter.flush();
    REQUIRE_FALSE(fixture.adapter.blocks_game_input());

    fixture.adapter.set_ui_active(true);
    fixture.adapter.flush();
    REQUIRE(fixture.adapter.is_capturing_pointer());
    REQUIRE(fixture.adapter.is_capturing_keyboard());
    REQUIRE(fixture.adapter.is_capturing_text());
}

TEST_CASE("Raw gamepad navigation emits edge-triggered UI keys") {
    Fixture fixture;
    fixture.sink.consumeKeyboard = true;
    Gamepad* gamepad = insys_test::add_gamepad(fixture.manager);

    GamepadState state;
    state.buttons = 1u << 2; // dpad left
    set_gamepad_state(fixture.manager, gamepad, state, 4.0);
    fixture.adapter.flush();
    REQUIRE(fixture.sink.keyDowns.size() == 1);
    REQUIRE(fixture.sink.keyDowns[0].first == Key::ArrowLeft);

    fixture.adapter.flush();
    REQUIRE(fixture.sink.keyDowns.size() == 1);

    state.buttons = 0;
    set_gamepad_state(fixture.manager, gamepad, state, 4.1);
    fixture.adapter.flush();
    REQUIRE(fixture.sink.keyUps.size() == 1);
    REQUIRE(fixture.sink.keyUps[0].first == Key::ArrowLeft);

    state.buttons = 1u << 6; // buttonSouth
    set_gamepad_state(fixture.manager, gamepad, state, 4.2);
    fixture.adapter.flush();
    REQUIRE(fixture.sink.keyDowns.back().first == Key::Enter);

    state.buttons = 1u << 5; // buttonEast
    set_gamepad_state(fixture.manager, gamepad, state, 4.3);
    fixture.adapter.flush();
    REQUIRE(fixture.sink.keyUps.back().first == Key::Enter);
    REQUIRE(fixture.sink.keyDowns.back().first == Key::Escape);
    REQUIRE(fixture.adapter.is_consuming_keyboard());
}

TEST_CASE("Action navigation consumes UI actions") {
    Fixture fixture;
    fixture.sink.consumeKeyboard = true;
    Gamepad* gamepad = insys_test::add_gamepad(fixture.manager);

    ActionAsset asset{"UI"};
    ActionMap& map = asset.add_map("UI");
    map.add_action("Navigate", ActionType::Value).expectedControlType = "Vector2";
    map.add_binding("Navigate", "<Gamepad>/leftStick");
    map.add_action("Submit", ActionType::Button);
    map.add_binding("Submit", "<Gamepad>/buttonSouth");
    map.add_action("Cancel", ActionType::Button);
    map.add_binding("Cancel", "<Gamepad>/buttonEast");
    asset.enable(fixture.manager);

    ActionDef* navigate = map.find_action("Navigate");
    ActionDef* submit = map.find_action("Submit");
    ActionDef* cancel = map.find_action("Cancel");
    REQUIRE(navigate != nullptr);
    REQUIRE(submit != nullptr);
    REQUIRE(cancel != nullptr);

    fixture.adapter.set_navigation_source(NavigationSource::Actions);
    fixture.adapter.set_ui_actions(navigate, submit, cancel);

    GamepadState state;
    state.leftStick = {-1.0f, 0.0f};
    state.buttons = 1u << 6;
    set_gamepad_state(fixture.manager, gamepad, state, 5.0);
    fixture.adapter.flush();
    REQUIRE(fixture.sink.keyDowns.size() == 2);
    REQUIRE(fixture.sink.keyDowns[0].first == Key::ArrowLeft);
    REQUIRE(fixture.sink.keyDowns[1].first == Key::Enter);

    state.leftStick = {};
    state.buttons = 1u << 5;
    set_gamepad_state(fixture.manager, gamepad, state, 5.1);
    fixture.adapter.flush();
    REQUIRE(fixture.sink.keyUps.size() >= 2);
    REQUIRE(fixture.sink.keyUps[0].first == Key::ArrowLeft);
    REQUIRE(fixture.sink.keyUps[1].first == Key::Enter);
    REQUIRE(fixture.sink.keyDowns.back().first == Key::Escape);
}

TEST_CASE("Adapter releases navigation when switching source") {
    Fixture fixture;
    fixture.sink.consumeKeyboard = true;
    Gamepad* gamepad = insys_test::add_gamepad(fixture.manager);

    GamepadState state;
    state.leftStick = {1.0f, 0.0f};
    set_gamepad_state(fixture.manager, gamepad, state, 6.0);
    fixture.adapter.flush();
    REQUIRE(fixture.sink.keyDowns.size() == 1);

    fixture.adapter.set_navigation_source(NavigationSource::Actions);
    REQUIRE(fixture.sink.keyUps.size() == 1);
    REQUIRE(fixture.sink.keyUps[0].first == Key::ArrowRight);
}

TEST_CASE("Action pointer input drives movement, click and scroll") {
    Fixture fixture;
    fixture.sink.consumePointer = true;

    ActionAsset asset{"UI"};
    ActionMap& map = asset.add_map("UI");
    map.add_action("Point", ActionType::Value).expectedControlType = "Vector2";
    map.add_binding("Point", "<Mouse>/position");
    map.add_action("Click", ActionType::Button);
    map.add_binding("Click", "<Mouse>/rightButton");
    map.add_action("Scroll", ActionType::Value).expectedControlType = "Vector2";
    map.add_binding("Scroll", "<Mouse>/scroll");
    asset.enable(fixture.manager);

    ActionDef* point = map.find_action("Point");
    ActionDef* click = map.find_action("Click");
    ActionDef* scroll = map.find_action("Scroll");
    REQUIRE(point != nullptr);
    REQUIRE(click != nullptr);
    REQUIRE(scroll != nullptr);
    fixture.adapter.set_ui_actions(nullptr, nullptr, nullptr, point, click, scroll);

    MouseState state;
    state.position = {30.0f, 40.0f};
    state.scroll = {0.0f, 1.0f};
    state.buttons[0] = 0x02;
    set_mouse_state(fixture.manager, fixture.mouse, state, 7.0);
    REQUIRE(click->state != nullptr);
    REQUIRE(click->state->phase(*click) == ActionPhase::Performed);
    REQUIRE(click->state->was_pressed_this_frame(*click));

    fixture.adapter.flush();

    REQUIRE_FALSE(fixture.sink.moves.empty());
    REQUIRE(fixture.sink.moves.front().x == 30.0f);
    REQUIRE(fixture.sink.moves.front().y == 40.0f);
    REQUIRE(fixture.sink.buttonDowns.size() == 1);
    REQUIRE(fixture.sink.buttonDowns.front().index == 0);
    REQUIRE(fixture.sink.wheels.size() == 1);
    REQUIRE(fixture.sink.wheels.front().y == -1.0f);

    state.buttons[0] = 0;
    state.scroll = {};
    set_mouse_state(fixture.manager, fixture.mouse, state, 7.1);
    fixture.adapter.flush();
    REQUIRE(fixture.sink.buttonUps.size() == 1);
    REQUIRE(fixture.sink.wheels.size() == 1);
}

TEST_CASE("Adapter releases pointer when the window loses focus") {
    Fixture fixture;
    fixture.sink.consumePointer = true;
    MouseState state;
    state.position = {5.0f, 6.0f};
    set_mouse_state(fixture.manager, fixture.mouse, state, 8.0);
    fixture.adapter.flush();
    REQUIRE(fixture.sink.mouseLeaves == 0);

    fixture.runtime.m_focus = FocusState::Unfocused;
    fixture.adapter.flush();
    REQUIRE(fixture.sink.mouseLeaves == 1);
}