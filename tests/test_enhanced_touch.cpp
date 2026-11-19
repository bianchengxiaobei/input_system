#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "action_test_helpers.h"
#include "insys/enhanced_touch.h"
#include "insys/low/state_format.h"

using namespace insys;

namespace {

Touchscreen* add_touchscreen(Manager& manager) {
    return static_cast<Touchscreen*>(
        manager.create_device("Touchscreen", DeviceDescription{}));
}

void send_touches(Manager& manager, Touchscreen* screen, const TouchscreenState& state,
                  double time) {
    manager.runtime().set_focus_state(FocusState::Normal);
    const auto event = low::make_state_event(
        kFormatTouchscreen, &state, static_cast<uint32_t>(sizeof(state)),
        screen->deviceId, time);
    manager.queue_event(reinterpret_cast<const low::Event*>(event.data()));
    manager.update(UpdateType::Dynamic);
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

} // namespace

TEST_CASE("EnhancedTouch tracks fingers, phases and history") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Touchscreen* screen = add_touchscreen(manager);

    enhanced_touch::EnhancedTouchSupport::enable(manager);
    REQUIRE(enhanced_touch::EnhancedTouchSupport::enabled());
    REQUIRE(enhanced_touch::Touch::screens().size() == 1);
    REQUIRE(enhanced_touch::Touch::fingers().size() == kMaxTouches);

    TouchscreenState state{};
    state.touches[0] = touch({10.0f, 10.0f}, 1.0f, TouchPhase::Began);
    send_touches(manager, screen, state, 1.0);

    const auto& active = enhanced_touch::Touch::active_touches();
    REQUIRE(active.size() == 1);
    REQUIRE(active[0].valid());
    REQUIRE(active[0].phase() == TouchPhase::Began);
    REQUIRE(active[0].screen() == screen);
    REQUIRE(active[0].finger() != nullptr);
    REQUIRE(active[0].finger()->index() == 0);
    REQUIRE(active[0].screen_position() == Vector2{10.0f, 10.0f});
    const int touchId = active[0].touch_id();
    REQUIRE(touchId != 0);

    state.touches[0] = touch({20.0f, 10.0f}, 1.0f, TouchPhase::Moved, {10.0f, 10.0f});
    state.touches[0].delta = {10.0f, 0.0f};
    runtime.m_now = 1.1;
    send_touches(manager, screen, state, 1.1);

    REQUIRE(enhanced_touch::Touch::active_touches().size() == 1);
    REQUIRE(enhanced_touch::Touch::active_touches()[0].phase() == TouchPhase::Moved);
    REQUIRE(enhanced_touch::Touch::active_touches()[0].touch_id() == touchId);

    state.touches[0] = touch({20.0f, 10.0f}, 0.0f, TouchPhase::Ended, {10.0f, 10.0f});
    runtime.m_now = 1.2;
    send_touches(manager, screen, state, 1.2);

    REQUIRE(enhanced_touch::Touch::active_touches().size() == 1);
    const auto& ended = enhanced_touch::Touch::active_touches()[0];
    REQUIRE(ended.phase() == TouchPhase::Ended);
    REQUIRE(ended.ended());
    REQUIRE(ended.is_tap());
    REQUIRE(ended.tap_count() == 1);
    REQUIRE(ended.history().count() == 2);

    const enhanced_touch::Finger* finger = ended.finger();
    REQUIRE(finger != nullptr);
    REQUIRE(finger->touch_history().count() == 3);

    runtime.m_now = 1.3;
    manager.update(UpdateType::Dynamic);
    REQUIRE(enhanced_touch::Touch::active_touches().empty());
    REQUIRE_FALSE(finger->current_touch().valid());

    enhanced_touch::EnhancedTouchSupport::disable();
    REQUIRE_FALSE(enhanced_touch::EnhancedTouchSupport::enabled());
    REQUIRE(enhanced_touch::Touch::active_touches().empty());
}

TEST_CASE("EnhancedTouch handles multiple fingers and history limits") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Touchscreen* screen = add_touchscreen(manager);

    enhanced_touch::EnhancedTouchSupport::enable(manager);
    enhanced_touch::EnhancedTouchSupport::set_max_history_length_per_finger(4);
    REQUIRE(enhanced_touch::EnhancedTouchSupport::max_history_length_per_finger() == 4);

    int downCount = 0;
    int moveCount = 0;
    int upCount = 0;
    const int downId = enhanced_touch::EnhancedTouchSupport::add_on_finger_down(
        [&](const enhanced_touch::Finger&) { ++downCount; });
    const int moveId = enhanced_touch::EnhancedTouchSupport::add_on_finger_move(
        [&](const enhanced_touch::Finger&) { ++moveCount; });
    const int upId = enhanced_touch::EnhancedTouchSupport::add_on_finger_up(
        [&](const enhanced_touch::Finger&) { ++upCount; });

    TouchscreenState state{};
    state.touches[0] = touch({10.0f, 10.0f}, 1.0f, TouchPhase::Began);
    state.touches[1] = touch({100.0f, 10.0f}, 1.0f, TouchPhase::Began);
    send_touches(manager, screen, state, 1.0);

    REQUIRE(enhanced_touch::Touch::active_touches().size() == 2);
    REQUIRE(enhanced_touch::Touch::active_fingers().size() == 2);
    REQUIRE(downCount == 2);

    for (int i = 1; i <= 6; ++i) {
        state.touches[0].position.x = 10.0f + static_cast<float>(i);
        state.touches[0].delta = {1.0f, 0.0f};
        state.touches[0].phase = static_cast<float>(TouchPhase::Moved);
        state.touches[1].position.y = 10.0f + static_cast<float>(i);
        state.touches[1].delta = {0.0f, 1.0f};
        state.touches[1].phase = static_cast<float>(TouchPhase::Moved);
        runtime.m_now = 1.0 + static_cast<double>(i) * 0.1;
        send_touches(manager, screen, state, runtime.m_now);
        enhanced_touch::EnhancedTouchSupport::update();
    }

    REQUIRE(moveCount == 12);
    for (const enhanced_touch::Finger* finger :
         enhanced_touch::Touch::active_fingers()) {
        REQUIRE(finger->touch_history().count() <= 4);
    }

    state.touches[0] = touch({20.0f, 10.0f}, 0.0f, TouchPhase::Ended, {10.0f, 10.0f});
    state.touches[1] = touch({100.0f, 20.0f}, 0.0f, TouchPhase::Ended, {100.0f, 10.0f});
    runtime.m_now = 2.0;
    send_touches(manager, screen, state, 2.0);
    enhanced_touch::EnhancedTouchSupport::update();
    REQUIRE(upCount == 2);
    REQUIRE(enhanced_touch::Touch::active_touches().size() == 2);

    enhanced_touch::EnhancedTouchSupport::remove_callback(downId);
    enhanced_touch::EnhancedTouchSupport::remove_callback(moveId);
    enhanced_touch::EnhancedTouchSupport::remove_callback(upId);
    enhanced_touch::EnhancedTouchSupport::disable();
}

TEST_CASE("TouchSimulation creates a simulated touchscreen from a mouse") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Mouse* mouse = static_cast<Mouse*>(manager.add_device(
        make_mouse(manager.runtime().allocate_device_id())));
    REQUIRE(mouse != nullptr);

    MouseState mouseState{};
    mouseState.position = {50.0f, 60.0f};
    mouseState.buttons[0] = 1;
    auto mouseEvent = low::make_state_event(
        kFormatMouse, &mouseState, static_cast<uint32_t>(sizeof(mouseState)),
        mouse->deviceId, 1.0);
    manager.queue_event(reinterpret_cast<const low::Event*>(mouseEvent.data()));
    manager.update(UpdateType::Dynamic);

    REQUIRE(mouse->left_button()->read_float(mouse->current_state()) >= 0.5f);
    enhanced_touch::TouchSimulation simulation(manager);
    simulation.enable();
    REQUIRE(simulation.enabled());
    REQUIRE(simulation.simulated_touchscreen() != nullptr);

    simulation.update();
    manager.update(UpdateType::Dynamic);
    TouchscreenState simulatedState{};
    std::memcpy(&simulatedState, simulation.simulated_touchscreen()->current_state(),
                sizeof(simulatedState));
    REQUIRE(simulatedState.touches[0].press == 1.0f);
    enhanced_touch::EnhancedTouchSupport::enable(manager);

    const auto& active = enhanced_touch::Touch::active_touches();
    REQUIRE(active.size() == 1);
    REQUIRE(active[0].screen_position() == Vector2{50.0f, 60.0f});
    REQUIRE(active[0].phase() == TouchPhase::Began);
    REQUIRE(active[0].screen() == simulation.simulated_touchscreen());

    mouseState.buttons[0] = 0;
    mouseEvent = low::make_state_event(
        kFormatMouse, &mouseState, static_cast<uint32_t>(sizeof(mouseState)),
        mouse->deviceId, 1.1);
    manager.queue_event(reinterpret_cast<const low::Event*>(mouseEvent.data()));
    manager.update(UpdateType::Dynamic);
    simulation.update();
    manager.update(UpdateType::Dynamic);
    REQUIRE(enhanced_touch::Touch::active_touches().size() == 1);
    REQUIRE(enhanced_touch::Touch::active_touches()[0].phase() == TouchPhase::Ended);

    simulation.disable();
    REQUIRE_FALSE(simulation.enabled());
    REQUIRE(simulation.simulated_touchscreen() == nullptr);
    enhanced_touch::EnhancedTouchSupport::disable();
}





