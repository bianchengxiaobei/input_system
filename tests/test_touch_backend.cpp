#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "insys/backend/runtime.h"
#include "insys/backend/touch_backend.h"
#include "insys/device.h"
#include "insys/device_states.h"
#include "insys/manager.h"

using namespace insys;
using namespace insys::backend;

namespace {

TouchscreenState current_touchscreen(Manager& manager) {
    TouchscreenState state;
    std::memcpy(&state, manager.devices()[0]->current_state(), sizeof(state));
    return state;
}

} // namespace

TEST_CASE("TouchBackend lazily creates Touchscreen and sends began state") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    TouchBackend backend;
    REQUIRE(backend.initialize(runtime, manager));
    REQUIRE_FALSE(backend.has_touchscreen());
    REQUIRE(manager.devices().empty());

    REQUIRE(backend.touch_began(7, {10.0f, 20.0f}));
    REQUIRE(backend.has_touchscreen());
    REQUIRE(manager.devices().size() == 1);
    REQUIRE(manager.devices()[0]->description.deviceClass == "Touchscreen");
    REQUIRE(backend.device_id() == manager.devices()[0]->deviceId);

    backend.poll();
    manager.update(UpdateType::Dynamic);
    const TouchscreenState state = current_touchscreen(manager);
    REQUIRE(state.touches[0].position == Vector2{10.0f, 20.0f});
    REQUIRE(state.touches[0].startPosition == Vector2{10.0f, 20.0f});
    REQUIRE(state.touches[0].phase == static_cast<float>(TouchPhase::Began));
    REQUIRE(state.touches[0].press == 1.0f);
}

TEST_CASE("TouchBackend accumulates movement and sends end state") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    TouchBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    REQUIRE(backend.touch_began(1, {10.0f, 20.0f}));
    backend.poll();
    manager.update(UpdateType::Dynamic);

    REQUIRE(backend.touch_moved(1, {15.0f, 25.0f}));
    REQUIRE(backend.touch_moved(1, {20.0f, 30.0f}));
    backend.poll();
    manager.update(UpdateType::Dynamic);
    TouchscreenState state = current_touchscreen(manager);
    REQUIRE(state.touches[0].position == Vector2{20.0f, 30.0f});
    REQUIRE(state.touches[0].delta == Vector2{10.0f, 10.0f});
    REQUIRE(state.touches[0].phase == static_cast<float>(TouchPhase::Moved));

    REQUIRE(backend.touch_ended(1));
    REQUIRE_FALSE(backend.touch_moved(1, {30.0f, 40.0f}));
    backend.poll();
    manager.update(UpdateType::Dynamic);
    state = current_touchscreen(manager);
    REQUIRE(state.touches[0].position == Vector2{20.0f, 30.0f});
    REQUIRE(state.touches[0].phase == static_cast<float>(TouchPhase::Ended));
    REQUIRE(state.touches[0].press == 0.0f);
}

TEST_CASE("TouchBackend tracks multiple touches and supports cancel") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    TouchBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    REQUIRE(backend.touch_began(1, {0.0f, 0.0f}));
    REQUIRE(backend.touch_began(2, {100.0f, 100.0f}));
    REQUIRE_FALSE(backend.touch_began(1, {1.0f, 1.0f}));
    REQUIRE(backend.touch_moved(2, {110.0f, 120.0f}));
    backend.poll();
    manager.update(UpdateType::Dynamic);

    const TouchscreenState state = current_touchscreen(manager);
    REQUIRE(state.touches[0].position == Vector2{0.0f, 0.0f});
    REQUIRE(state.touches[1].position == Vector2{110.0f, 120.0f});
    REQUIRE(state.touches[1].delta == Vector2{10.0f, 20.0f});

    REQUIRE(backend.touch_canceled(2));
    backend.poll();
    manager.update(UpdateType::Dynamic);
    REQUIRE(current_touchscreen(manager).touches[1].phase ==
            static_cast<float>(TouchPhase::Canceled));
}

TEST_CASE("TouchBackend enforces touch slot limit and shutdown") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    TouchBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    for (int i = 0; i < kMaxTouches; ++i)
        REQUIRE(backend.touch_began(i, {static_cast<float>(i), 0.0f}));
    REQUIRE_FALSE(backend.touch_began(100, {0.0f, 0.0f}));

    REQUIRE(backend.touch_ended(0));
    REQUIRE(backend.touch_began(100, {50.0f, 50.0f}));

    backend.shutdown();
    REQUIRE_FALSE(backend.has_touchscreen());
    REQUIRE(manager.devices().empty());
    REQUIRE_FALSE(backend.touch_began(1, {}));
    backend.poll();

    TouchBackend uninitialized;
    REQUIRE_FALSE(uninitialized.touch_began(1, {}));
}

TEST_CASE("TouchBackend cancels all active touches") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    TouchBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    REQUIRE(backend.touch_began(1, {10.0f, 20.0f}));
    REQUIRE(backend.touch_began(2, {30.0f, 40.0f}));
    REQUIRE(backend.active_touch_count() == 2);
    REQUIRE(backend.cancel_all() == 2);
    REQUIRE(backend.active_touch_count() == 0);
    REQUIRE(backend.cancel_all() == 0);

    backend.poll();
    manager.update(UpdateType::Dynamic);
    const TouchscreenState state = current_touchscreen(manager);
    REQUIRE(state.touches[0].phase == static_cast<float>(TouchPhase::Canceled));
    REQUIRE(state.touches[1].phase == static_cast<float>(TouchPhase::Canceled));
    REQUIRE(state.touches[0].press == 0.0f);
    REQUIRE(state.touches[1].press == 0.0f);

    REQUIRE(backend.touch_began(3, {50.0f, 60.0f}));
    REQUIRE(backend.active_touch_count() == 1);
}

TEST_CASE("TouchBackend clamps and forwards pressure") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    TouchBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    REQUIRE(backend.touch_began(1, {10.0f, 20.0f}, 0.25f));
    backend.poll();
    manager.update(UpdateType::Dynamic);
    REQUIRE(current_touchscreen(manager).touches[0].press == 0.25f);

    REQUIRE(backend.touch_moved(1, {15.0f, 25.0f}, 2.0f));
    backend.poll();
    manager.update(UpdateType::Dynamic);
    REQUIRE(current_touchscreen(manager).touches[0].press == 1.0f);

    REQUIRE(backend.touch_moved(1, {20.0f, 30.0f}, -1.0f));
    backend.poll();
    manager.update(UpdateType::Dynamic);
    REQUIRE(current_touchscreen(manager).touches[0].press == 0.0f);
}
