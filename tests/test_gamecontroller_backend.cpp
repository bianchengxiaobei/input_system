#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "insys/backend/gamecontroller_backend.h"
#include "insys/backend/runtime.h"
#include "insys/device.h"
#include "insys/manager.h"

using namespace insys;
using namespace insys::backend;

namespace {

GamepadState current_gamepad(Device* device) {
    GamepadState state;
    std::memcpy(&state, device->current_state(), sizeof(state));
    return state;
}

} // namespace

TEST_CASE("GameControllerBackend creates devices and emits latest state") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    GameControllerBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    DeviceDescription description;
    description.product = "DualSense";
    description.manufacturer = "Sony";
    REQUIRE(backend.add_controller("pad1", description));
    REQUIRE(backend.connected("pad1"));
    REQUIRE(manager.devices().size() == 1);
    REQUIRE(manager.devices()[0]->description.deviceClass == "Gamepad");
    REQUIRE(manager.devices()[0]->description.serial == "pad1");
    REQUIRE(manager.devices()[0]->description.product == "DualSense");
    REQUIRE(backend.device_id("pad1") == manager.devices()[0]->deviceId);

    GameControllerState state;
    state.buttonA = true;
    state.leftThumbstickX = 0.5f;
    state.leftTrigger = 1.0f;
    REQUIRE(backend.update_controller("pad1", state));
    REQUIRE_FALSE(backend.update_controller("pad1", state));

    state.buttonB = true;
    REQUIRE(backend.update_controller("pad1", state));
    backend.poll();
    manager.update(UpdateType::Dynamic);

    const GamepadState gamepad = current_gamepad(manager.devices()[0]);
    REQUIRE((gamepad.buttons & (1u << 6)) != 0);
    REQUIRE((gamepad.buttons & (1u << 5)) != 0);
    REQUIRE(gamepad.leftStick.x == 0.5f);
    REQUIRE(gamepad.leftTrigger == 1.0f);
}

TEST_CASE("GameControllerBackend manages multiple controllers") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    GameControllerBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    REQUIRE(backend.add_controller("pad1"));
    REQUIRE(backend.add_controller("pad2"));
    REQUIRE_FALSE(backend.add_controller("pad1"));
    REQUIRE_FALSE(backend.add_controller(""));
    REQUIRE(manager.devices().size() == 2);

    GameControllerState first;
    first.buttonA = true;
    GameControllerState second;
    second.buttonX = true;
    REQUIRE(backend.update_controller("pad1", first));
    REQUIRE(backend.update_controller("pad2", second));
    backend.poll();
    manager.update(UpdateType::Dynamic);

    for (Device* device : manager.devices()) {
        const GamepadState state = current_gamepad(device);
        if (device->description.serial == "pad1")
            REQUIRE((state.buttons & (1u << 6)) != 0);
        if (device->description.serial == "pad2")
            REQUIRE((state.buttons & (1u << 7)) != 0);
    }

    REQUIRE(backend.remove_controller("pad1"));
    REQUIRE_FALSE(backend.remove_controller("pad1"));
    REQUIRE_FALSE(backend.connected("pad1"));
    REQUIRE(backend.device_id("pad1") == 0);
    REQUIRE(manager.devices().size() == 1);

    REQUIRE_FALSE(backend.update_controller("pad1", first));
    backend.poll();
    manager.update(UpdateType::Dynamic);
    REQUIRE(manager.devices().size() == 1);

    backend.shutdown();
    REQUIRE(manager.devices().empty());
}

TEST_CASE("GameControllerBackend ignores updates before initialization") {
    GameControllerBackend backend;
    GameControllerState state;
    REQUIRE_FALSE(backend.add_controller("pad"));
    REQUIRE_FALSE(backend.update_controller("pad", state));
    REQUIRE_FALSE(backend.connected("pad"));
    backend.poll();
    backend.shutdown();
}
