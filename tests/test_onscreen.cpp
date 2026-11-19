#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "action_test_helpers.h"
#include "insys/low/state_format.h"
#include "insys/onscreen/on_screen.h"
#include "insys/ui/ui_pointer_input.h"

using namespace insys;

namespace {

Touchscreen* add_touchscreen(Manager& manager) {
    return static_cast<Touchscreen*>(
        manager.create_device("Touchscreen", DeviceDescription{}));
}

TouchPointState touch(Vector2 position, float press, TouchPhase phase,
                      Vector2 start = {}) {
    TouchPointState point{};
    point.position = position;
    point.startPosition = start.is_zero() ? position : start;
    point.press = press;
    point.phase = static_cast<float>(phase);
    point.tapCount = press >= 0.5f ? 1.0f : 0.0f;
    return point;
}

void send_touches(Manager& manager, Touchscreen* screen, const TouchscreenState& state,
                  double time) {
    const auto event = low::make_state_event(
        kFormatTouchscreen, &state, static_cast<uint32_t>(sizeof(state)),
        screen->deviceId, time);
    manager.queue_event(reinterpret_cast<const low::Event*>(event.data()));
    manager.update(UpdateType::Dynamic);
}

void send_mouse(Manager& manager, Mouse* mouse, Vector2 position, bool down,
                double time) {
    MouseState state{};
    state.position = position;
    state.buttons[0] = down ? 1 : 0;
    const auto event = low::make_state_event(
        kFormatMouse, &state, static_cast<uint32_t>(sizeof(state)), mouse->deviceId,
        time);
    manager.queue_event(reinterpret_cast<const low::Event*>(event.data()));
    manager.update(UpdateType::Dynamic);
}

class TestTarget : public ui::UiPointerTarget {
public:
    explicit TestTarget(ui::UiRect bounds) : m_bounds(bounds) {}

    bool contains(Vector2 position) const override { return m_bounds.contains(position); }

    void on_pointer_down(const ui::UiPointerEvent& event) override {
        ++down;
        lastPosition = event.position;
    }
    void on_pointer_move(const ui::UiPointerEvent& event) override {
        ++move;
        lastPosition = event.position;
    }
    void on_pointer_up(const ui::UiPointerEvent& event) override {
        ++up;
        lastPosition = event.position;
    }

    int down = 0;
    int move = 0;
    int up = 0;
    Vector2 lastPosition{};

private:
    ui::UiRect m_bounds;
};

} // namespace

TEST_CASE("OnScreenButton drives a virtual gamepad control") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Touchscreen* screen = add_touchscreen(manager);

    onscreen::OnScreenInputModule input(manager);
    onscreen::OnScreenButton button;
    button.set_control_path("<Gamepad>/buttonSouth");
    button.set_bounds({0.0f, 0.0f, 100.0f, 100.0f});
    REQUIRE(input.add_control(&button));
    REQUIRE(button.is_bound());

    auto* gamepad = static_cast<Gamepad*>(input.on_screen_device("Gamepad"));
    REQUIRE(gamepad != nullptr);
    REQUIRE(gamepad->button("buttonSouth") != nullptr);

    TouchscreenState state{};
    state.touches[0] = touch({50.0f, 50.0f}, 1.0f, TouchPhase::Began);
    send_touches(manager, screen, state, 1.0);
    input.update();
    manager.update(UpdateType::Dynamic);

    REQUIRE(gamepad->button("buttonSouth")->read_float(gamepad->current_state()) >=
            0.5f);

    state.touches[0] = touch({50.0f, 50.0f}, 0.0f, TouchPhase::Ended);
    send_touches(manager, screen, state, 1.1);
    input.update();
    manager.update(UpdateType::Dynamic);

    REQUIRE(gamepad->button("buttonSouth")->read_float(gamepad->current_state()) <
            0.5f);

    onscreen::OnScreenButton second;
    second.set_control_path("<Gamepad>/buttonEast");
    second.set_bounds({0.0f, 100.0f, 100.0f, 100.0f});
    REQUIRE(input.add_control(&second));
    REQUIRE(input.on_screen_device("Gamepad") == gamepad);

    input.remove_control(&second);
    REQUIRE(input.on_screen_device("Gamepad") == gamepad);
    input.remove_control(&button);
    REQUIRE(input.on_screen_device("Gamepad") == nullptr);
}

TEST_CASE("OnScreenStick maps pointer motion to a stick value") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Touchscreen* screen = add_touchscreen(manager);

    onscreen::OnScreenInputModule input(manager);
    onscreen::OnScreenStick stick;
    stick.set_control_path("<Gamepad>/leftStick");
    stick.set_bounds({0.0f, 0.0f, 200.0f, 200.0f});
    stick.set_movement_range(100.0f);
    REQUIRE(input.add_control(&stick));

    auto* gamepad = static_cast<Gamepad*>(input.on_screen_device("Gamepad"));
    REQUIRE(gamepad != nullptr);

    TouchscreenState state{};
    state.touches[0] = touch({100.0f, 100.0f}, 1.0f, TouchPhase::Began);
    send_touches(manager, screen, state, 1.0);
    input.update();
    manager.update(UpdateType::Dynamic);
    REQUIRE(gamepad->left_stick()->read_vector2(gamepad->current_state()) == Vector2{});

    state.touches[0] =
        touch({150.0f, 100.0f}, 1.0f, TouchPhase::Moved, {100.0f, 100.0f});
    send_touches(manager, screen, state, 1.1);
    input.update();
    manager.update(UpdateType::Dynamic);
    const Vector2 half = gamepad->left_stick()->read_vector2(gamepad->current_state());
    REQUIRE(half.x == Catch::Approx(0.5f).margin(0.001f));
    REQUIRE(half.y == Catch::Approx(0.0f).margin(0.001f));

    state.touches[0] =
        touch({500.0f, 100.0f}, 1.0f, TouchPhase::Moved, {100.0f, 100.0f});
    send_touches(manager, screen, state, 1.2);
    input.update();
    manager.update(UpdateType::Dynamic);
    const Vector2 clamped = gamepad->left_stick()->read_vector2(gamepad->current_state());
    REQUIRE(clamped.x == Catch::Approx(1.0f).margin(0.001f));
    REQUIRE(clamped.y == Catch::Approx(0.0f).margin(0.001f));

    state.touches[0] = touch({500.0f, 100.0f}, 0.0f, TouchPhase::Ended);
    send_touches(manager, screen, state, 1.3);
    input.update();
    manager.update(UpdateType::Dynamic);
    REQUIRE(gamepad->left_stick()->read_vector2(gamepad->current_state()) ==
            Vector2{});
}

TEST_CASE("OnScreenStick exact dynamic origin resets at pointer down") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Touchscreen* screen = add_touchscreen(manager);

    onscreen::OnScreenInputModule input(manager);
    onscreen::OnScreenStick stick;
    stick.set_control_path("<Gamepad>/rightStick");
    stick.set_bounds({0.0f, 0.0f, 200.0f, 200.0f});
    stick.set_behaviour(onscreen::OnScreenStickBehaviour::ExactPositionWithDynamicOrigin);
    stick.set_movement_range(50.0f);
    stick.set_dynamic_origin_range(100.0f);
    REQUIRE(input.add_control(&stick));

    auto* gamepad = static_cast<Gamepad*>(input.on_screen_device("Gamepad"));
    REQUIRE(gamepad != nullptr);

    TouchscreenState state{};
    state.touches[0] = touch({120.0f, 120.0f}, 1.0f, TouchPhase::Began);
    send_touches(manager, screen, state, 1.0);
    input.update();
    manager.update(UpdateType::Dynamic);
    REQUIRE(gamepad->right_stick()->read_vector2(gamepad->current_state()) ==
            Vector2{});

    state.touches[0] =
        touch({170.0f, 120.0f}, 1.0f, TouchPhase::Moved, {120.0f, 120.0f});
    send_touches(manager, screen, state, 1.1);
    input.update();
    manager.update(UpdateType::Dynamic);
    const Vector2 value =
        gamepad->right_stick()->read_vector2(gamepad->current_state());
    REQUIRE(value.x == Catch::Approx(1.0f).margin(0.001f));
    REQUIRE(value.y == Catch::Approx(0.0f).margin(0.001f));
}

TEST_CASE("UiPointerInputModule dispatches captured mouse pointers") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Mouse* mouse = static_cast<Mouse*>(manager.add_device(
        make_mouse(manager.runtime().allocate_device_id())));

    ui::UiPointerInputModule input(manager);
    TestTarget lower({0.0f, 0.0f, 200.0f, 200.0f});
    TestTarget upper({0.0f, 0.0f, 100.0f, 100.0f});
    input.add_target(&lower);
    input.add_target(&upper);

    send_mouse(manager, mouse, {50.0f, 50.0f}, true, 1.0);
    input.update();
    REQUIRE(upper.down == 1);
    REQUIRE(lower.down == 0);
    REQUIRE(input.captures_pointer());

    send_mouse(manager, mouse, {150.0f, 50.0f}, true, 1.1);
    input.update();
    REQUIRE(upper.move == 1);
    REQUIRE(upper.lastPosition == Vector2{150.0f, 50.0f});

    send_mouse(manager, mouse, {150.0f, 50.0f}, false, 1.2);
    input.update();
    REQUIRE(upper.up == 1);
    REQUIRE_FALSE(input.captures_pointer());
}

