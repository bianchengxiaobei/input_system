#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include "action_test_helpers.h"
#include "insys/actions/action_asset.h"
#include "insys/actions/action_state.h"

using namespace insys;

TEST_CASE("Malformed and unknown events do not crash the manager") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Keyboard* keyboard = insys_test::add_keyboard(manager);

    std::uint32_t seed = 0x12345678u;
    auto next = [&] {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };

    for (int i = 0; i < 500; ++i) {
        std::vector<uint8_t> bytes(64, 0);
        auto* event = reinterpret_cast<low::Event*>(bytes.data());
        event->type = next() % 8 == 0 ? low::kStateEvent.value
                       : next() % 8 == 1 ? low::kDeltaStateEvent.value
                       : next() % 8 == 2 ? low::kTextEvent.value
                       : next() % 8 == 3 ? low::kDeviceResetEvent.value
                                         : next();
        event->size = static_cast<uint16_t>(low::kHeaderSize + (next() % 40));
        event->deviceId = static_cast<uint16_t>(next() % 4);
        event->time = static_cast<double>(next() % 1000) / 100.0;
        if (event->type == low::kDeltaStateEvent.value) {
            auto* delta = reinterpret_cast<low::DeltaStateEvent*>(event);
            delta->stateOffset = next() % 128;
        }
        manager.queue_event(event);
        manager.update();
    }

    manager.clear_pending_text();

    // A text event with only a header must be ignored safely.
    std::vector<uint8_t> shortText(low::kHeaderSize, 0);
    auto* text = reinterpret_cast<low::Event*>(shortText.data());
    text->type = low::kTextEvent.value;
    text->size = low::kHeaderSize;
    text->deviceId = static_cast<uint16_t>(keyboard->deviceId);
    manager.queue_event(text);
    manager.update();
    REQUIRE(manager.pending_text().empty());
}

TEST_CASE("Device hotplug storm leaves no monitors behind") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);

    for (int i = 0; i < 1000; ++i) {
        auto keyboard = make_keyboard(runtime.allocate_device_id());
        Device* device = manager.add_device(std::move(keyboard));
        REQUIRE(device != nullptr);
        manager.remove_device(device);
    }

    REQUIRE(manager.devices().empty());
    REQUIRE(manager.state_monitors().count() == 0);
    REQUIRE(manager.state_monitors().timeout_count() == 0);
}

TEST_CASE("Action enable/disable storm leaves no monitors behind") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Keyboard* keyboard = insys_test::add_keyboard(manager);

    ActionAsset asset{"Player"};
    ActionMap& map = asset.add_map("Player");
    map.add_action("Jump", ActionType::Button);
    map.add_binding("Jump", "<Keyboard>/space");

    for (int i = 0; i < 1000; ++i) {
        asset.enable(manager);
        asset.disable();
    }

    REQUIRE_FALSE(map.enabled());
    REQUIRE(manager.state_monitors().count() == 0);
    REQUIRE(manager.state_monitors().timeout_count() == 0);
}

TEST_CASE("Action callbacks may disable their own action") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Keyboard* keyboard = insys_test::add_keyboard(manager);

    ActionAsset asset{"Player"};
    ActionMap& map = asset.add_map("Player");
    ActionDef& jump = map.add_action("Jump", ActionType::Button);
    map.add_binding("Jump", "<Keyboard>/space");

    int performed = 0;
    jump.performed = [&](const CallbackContext&) {
        ++performed;
        asset.disable();
    };

    asset.enable(manager);
    insys_test::set_keys(manager, keyboard, {Key::Space}, 1.0);
    REQUIRE(performed == 1);
    REQUIRE(jump.state->phase(jump) == ActionPhase::Disabled);
}