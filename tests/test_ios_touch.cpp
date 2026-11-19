#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "insys/backend/ios_touch.h"
#include "insys/backend/mobile_input.h"
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

TouchPointState touch_state(Manager& manager, int index) {
    TouchscreenState state;
    std::memcpy(&state, by_serial(manager, "Touchscreen")->current_state(),
                sizeof(state));
    return state.touches[index];
}

} // namespace

TEST_CASE("iOS touch samples feed the mobile bridge") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    REQUIRE(feed_ios_touch(bridge, {1, {10.0f, 20.0f}, 0.5f,
                                  IosTouchPhase::Began}));
    bridge.poll();
    manager.update(UpdateType::Dynamic);
    TouchPointState point = touch_state(manager, 0);
    REQUIRE(point.position == Vector2{10.0f, 20.0f});
    REQUIRE(point.press == 0.5f);
    REQUIRE(point.phase == static_cast<float>(TouchPhase::Began));

    REQUIRE(feed_ios_touch(bridge, {1, {15.0f, 25.0f}, 2.0f,
                                  IosTouchPhase::Moved}));
    bridge.poll();
    manager.update(UpdateType::Dynamic);
    point = touch_state(manager, 0);
    REQUIRE(point.position == Vector2{15.0f, 25.0f});
    REQUIRE(point.delta == Vector2{5.0f, 5.0f});
    REQUIRE(point.press == 1.0f);

    REQUIRE(feed_ios_touch(bridge, {1, {15.0f, 25.0f}, 0.0f,
                                  IosTouchPhase::Ended}));
    bridge.poll();
    manager.update(UpdateType::Dynamic);
    point = touch_state(manager, 0);
    REQUIRE(point.phase == static_cast<float>(TouchPhase::Ended));
    REQUIRE(point.press == 0.0f);
}

TEST_CASE("iOS touch samples reject stationary phase") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    MobileInputBridge bridge;
    REQUIRE(bridge.initialize(runtime, manager));

    REQUIRE_FALSE(feed_ios_touch(bridge, {1, {10.0f, 20.0f}, 1.0f,
                                        IosTouchPhase::Stationary}));
    REQUIRE(manager.devices().empty());
}

TEST_CASE("iOS touch attachment is unavailable outside iOS") {
    IosTouchCallbacks callbacks;
    REQUIRE_FALSE(ios_touch_attach(nullptr, callbacks, nullptr));
    REQUIRE_FALSE(ios_touch_attached());
    ios_touch_detach();
    REQUIRE_FALSE(ios_touch_attached());
}

#if !defined(__APPLE__)

TEST_CASE("iOS touch attachment stub rejects a non-iOS view") {
    IosTouchCallbacks callbacks;
    callbacks.touch = [](void*, const IosTouchSample&) {};
    REQUIRE_FALSE(ios_touch_attach(reinterpret_cast<void*>(1), callbacks, nullptr));
    REQUIRE_FALSE(ios_touch_attached());
}

#endif
