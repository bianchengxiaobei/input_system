#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <thread>

#include "insys/backend/mobile_input_queue.h"
#include "insys/backend/runtime.h"
#include "insys/device.h"
#include "insys/device_states.h"
#include "insys/manager.h"

using namespace insys;
using namespace insys::backend;

namespace {

Device* by_serial(Manager& manager, const char* serial) {
    for (Device* device : manager.devices()) {
        if (device->description.serial == serial)
            return device;
    }
    return nullptr;
}

} // namespace

TEST_CASE("MobileInputQueue drains touch and motion in order") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    MobileInputQueue queue;
    queue.push_touch(MobileInputEventType::TouchBegan, 1, {10.0f, 20.0f});
    queue.push_touch(MobileInputEventType::TouchMoved, 1, {15.0f, 25.0f});
    queue.push_touch(MobileInputEventType::TouchEnded, 1, {});

    MobileMotionSample motion;
    motion.hasAngularVelocity = true;
    motion.angularVelocity = {1.0f, 2.0f, 3.0f};
    queue.push_motion(motion);

    REQUIRE(queue.size() == 4);
    REQUIRE(queue.drain(bridge) == 4);
    REQUIRE(queue.size() == 0);

    bridge.poll();
    manager.update(UpdateType::Dynamic);

    TouchscreenState touch;
    std::memcpy(&touch, by_serial(manager, "Touchscreen")->current_state(),
                sizeof(touch));
    REQUIRE(touch.touches[0].position == Vector2{15.0f, 25.0f});
    REQUIRE(touch.touches[0].phase == static_cast<float>(TouchPhase::Ended));

    GyroscopeState gyro;
    std::memcpy(&gyro, by_serial(manager, "MobileGyroscope")->current_state(),
                sizeof(gyro));
    REQUIRE(gyro.angularVelocity == Vector3{1.0f, 2.0f, 3.0f});
}

TEST_CASE("MobileInputQueue coalesces motion through the bridge") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    MobileInputQueue queue;
    MobileMotionSample motion;
    motion.hasAngularVelocity = true;
    motion.angularVelocity = {1.0f, 2.0f, 3.0f};
    queue.push_motion(motion);
    motion.angularVelocity = {4.0f, 5.0f, 6.0f};
    queue.push_motion(motion);

    REQUIRE(queue.drain(bridge) == 2);
    bridge.poll();
    manager.update(UpdateType::Dynamic);

    GyroscopeState gyro;
    std::memcpy(&gyro, by_serial(manager, "MobileGyroscope")->current_state(),
                sizeof(gyro));
    REQUIRE(gyro.angularVelocity == Vector3{4.0f, 5.0f, 6.0f});
}

TEST_CASE("MobileInputQueue clears and ignores invalid touch types") {
    MobileInputQueue queue;
    queue.push_touch(MobileInputEventType::Motion, 1, {});
    REQUIRE(queue.size() == 0);

    MobileMotionSample motion;
    motion.hasGravity = true;
    motion.gravity = {0.0f, -9.8f, 0.0f};
    queue.push_motion(motion);
    REQUIRE(queue.size() == 1);
    queue.clear();
    REQUIRE(queue.size() == 0);

    MobileInputBridge uninitialized;
    queue.push_touch(MobileInputEventType::TouchBegan, 1, {});
    REQUIRE(queue.drain(uninitialized) == 1);
    REQUIRE(queue.size() == 0);
}

TEST_CASE("MobileInputQueue accepts events from a producer thread") {
    MobileInputBridge uninitialized;
    MobileInputQueue queue;

    std::thread producer([&queue] {
        MobileMotionSample motion;
        motion.hasAcceleration = true;
        for (int i = 0; i < 100; ++i) {
            motion.acceleration = {static_cast<float>(i), 0.0f, 0.0f};
            queue.push_motion(motion);
        }
    });
    producer.join();

    REQUIRE(queue.size() == 100);
    REQUIRE(queue.drain(uninitialized) == 100);
    REQUIRE(queue.size() == 0);
}

TEST_CASE("MobileInputQueue forwards touch pressure") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    MobileInputQueue queue;
    queue.push_touch(MobileInputEventType::TouchBegan, 1, {10.0f, 20.0f}, 0.4f);
    REQUIRE(queue.drain(bridge) == 1);
    bridge.poll();
    manager.update(UpdateType::Dynamic);

    TouchscreenState state;
    std::memcpy(&state, by_serial(manager, "Touchscreen")->current_state(),
                sizeof(state));
    REQUIRE(state.touches[0].press == 0.4f);
}

TEST_CASE("MobileInputQueue defers suspend to the main thread drain") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    MobileInputQueue queue;
    queue.push_touch(MobileInputEventType::TouchBegan, 1, {10.0f, 20.0f});
    REQUIRE(queue.drain(bridge) == 1);
    bridge.poll();
    manager.update(UpdateType::Dynamic);

    queue.push_suspend();
    REQUIRE(queue.size() == 1);
    TouchscreenState before;
    std::memcpy(&before, by_serial(manager, "Touchscreen")->current_state(),
                sizeof(before));
    REQUIRE(before.touches[0].phase == static_cast<float>(TouchPhase::Began));

    REQUIRE(queue.drain(bridge) == 1);
    bridge.poll();
    manager.update(UpdateType::Dynamic);
    TouchscreenState after;
    std::memcpy(&after, by_serial(manager, "Touchscreen")->current_state(),
                sizeof(after));
    REQUIRE(after.touches[0].phase == static_cast<float>(TouchPhase::Canceled));
    REQUIRE(after.touches[0].press == 0.0f);
    REQUIRE(bridge.touch().active_touch_count() == 0);
}
