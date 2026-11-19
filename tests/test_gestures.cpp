#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "insys/gestures.h"

using namespace insys;

namespace {

void set_touch(TouchscreenState& state, int index, Vector2 position, TouchPhase phase,
               bool pressed) {
    state.touches[index].position = position;
    state.touches[index].phase = static_cast<float>(phase);
    state.touches[index].press = pressed ? 1.0f : 0.0f;
}

} // namespace

TEST_CASE("Gesture recognizer detects tap and suppresses canceled tap") {
    TouchGestureRecognizer recognizer;
    TouchscreenState state{};

    set_touch(state, 0, {100.0f, 100.0f}, TouchPhase::Began, true);
    recognizer.update(state, 1.0);
    REQUIRE(recognizer.events().empty());

    set_touch(state, 0, {102.0f, 101.0f}, TouchPhase::Ended, false);
    recognizer.update(state, 1.1);
    REQUIRE(recognizer.events().size() == 1);
    REQUIRE(recognizer.events()[0].type == GestureType::Tap);
    REQUIRE(recognizer.events()[0].touchIndex == 0);
    REQUIRE(recognizer.events()[0].duration == Catch::Approx(0.1));

    recognizer.reset();
    set_touch(state, 0, {10.0f, 10.0f}, TouchPhase::Began, true);
    recognizer.update(state, 2.0);
    set_touch(state, 0, {10.0f, 10.0f}, TouchPhase::Canceled, false);
    recognizer.update(state, 2.1);
    REQUIRE(recognizer.events().empty());
}

TEST_CASE("Gesture recognizer detects swipe and long press") {
    TouchGestureRecognizer recognizer;
    TouchscreenState state{};

    set_touch(state, 0, {0.0f, 0.0f}, TouchPhase::Began, true);
    recognizer.update(state, 1.0);
    set_touch(state, 0, {100.0f, 0.0f}, TouchPhase::Moved, true);
    recognizer.update(state, 1.1);
    REQUIRE(recognizer.events().size() == 1);
    REQUIRE(recognizer.events()[0].type == GestureType::Swipe);
    REQUIRE(recognizer.events()[0].direction == SwipeDirection::Right);
    REQUIRE(recognizer.events()[0].magnitude == Catch::Approx(100.0f));

    set_touch(state, 0, {100.0f, 0.0f}, TouchPhase::Ended, false);
    recognizer.update(state, 1.2);
    REQUIRE(recognizer.events().empty());

    recognizer.reset();
    set_touch(state, 0, {20.0f, 20.0f}, TouchPhase::Began, true);
    recognizer.update(state, 2.0);
    set_touch(state, 0, {20.0f, 20.0f}, TouchPhase::Stationary, true);
    recognizer.update(state, 2.6);
    REQUIRE(recognizer.events().size() == 1);
    REQUIRE(recognizer.events()[0].type == GestureType::LongPress);
    recognizer.update(state, 2.7);
    REQUIRE(recognizer.events().empty());
}

TEST_CASE("Gesture recognizer detects pinch scale") {
    TouchGestureRecognizer recognizer;
    TouchscreenState state{};

    set_touch(state, 0, {0.0f, 0.0f}, TouchPhase::Began, true);
    set_touch(state, 1, {100.0f, 0.0f}, TouchPhase::Began, true);
    recognizer.update(state, 1.0);
    REQUIRE(recognizer.events().empty());

    set_touch(state, 1, {150.0f, 0.0f}, TouchPhase::Moved, true);
    recognizer.update(state, 1.1);
    REQUIRE(recognizer.events().size() == 1);
    REQUIRE(recognizer.events()[0].type == GestureType::Pinch);
    REQUIRE(recognizer.events()[0].touchIndex == 0);
    REQUIRE(recognizer.events()[0].secondTouchIndex == 1);
    REQUIRE(recognizer.events()[0].magnitude == Catch::Approx(1.5f));
    REQUIRE(recognizer.events()[0].position == Vector2{75.0f, 0.0f});
}
