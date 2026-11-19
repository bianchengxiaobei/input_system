#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "action_test_helpers.h"
#include "insys/actions/action_asset.h"
#include "insys/actions/action_state.h"
#include "insys/event_trace.h"
#include "insys/input_listeners.h"

using namespace insys;

namespace {

struct ListenerFixture {
    insys_test::TestRuntime runtime;
    Manager manager{runtime};
    Keyboard* keyboard = insys_test::add_keyboard(manager);
};

} // namespace

TEST_CASE("Event listeners run in order and can suppress processing") {
    ListenerFixture fixture;
    std::vector<std::string> order;

    const int first = fixture.manager.add_event_listener(
        [&](low::Event& event, Device* device) {
            order.push_back("first");
            REQUIRE(device != nullptr);
            REQUIRE(event.type == low::kStateEvent.value);
        });
    fixture.manager.add_event_listener([&](low::Event&, Device*) { order.push_back("second"); });

    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::Space}, 1.0);
    REQUIRE(order == std::vector<std::string>{"first", "second"});
    REQUIRE(fixture.keyboard->key_control(Key::Space)->read_bool(
        fixture.keyboard->current_state()));

    fixture.manager.remove_event_listener(first);
    order.clear();
    insys_test::set_keys(fixture.manager, fixture.keyboard, {}, 1.1);
    REQUIRE(order == std::vector<std::string>{"second"});

    fixture.manager.add_event_listener([&](low::Event& event, Device*) {
        low::set_event_handled(&event, true);
    });
    insys_test::set_keys(fixture.manager, fixture.keyboard, {Key::A}, 1.2);
    REQUIRE_FALSE(fixture.keyboard->key_control(Key::A)->read_bool(
        fixture.keyboard->current_state()));
}

TEST_CASE("Device change listeners observe add/remove") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);

    std::vector<DeviceChange> changes;
    const int listener = manager.add_device_change_listener(
        [&](Device*, DeviceChange change) { changes.push_back(change); });

    auto keyboard = make_keyboard(runtime.allocate_device_id());
    Device* raw = manager.add_device(std::move(keyboard));
    REQUIRE(changes == std::vector<DeviceChange>{DeviceChange::Added});

    manager.remove_device(raw);
    REQUIRE(changes.size() == 2);
    REQUIRE(changes[1] == DeviceChange::Removed);

    manager.remove_device_change_listener(listener);
    auto second = make_keyboard(runtime.allocate_device_id());
    manager.add_device(std::move(second));
    REQUIRE(changes.size() == 2);
}

TEST_CASE("Action change listeners observe enable and phases") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Keyboard* keyboard = insys_test::add_keyboard(manager);
    ActionAsset asset{"Player"};
    ActionMap& map = asset.add_map("Player");
    ActionDef& jump = map.add_action("Jump", ActionType::Button);
    map.add_binding("Jump", "<Keyboard>/space");

    std::vector<ActionChange> changes;
    manager.add_action_change_listener(
        [&](ActionDef* action, ActionChange change) {
            REQUIRE(action != nullptr);
            changes.push_back(change);
        });

    asset.enable(manager);
    REQUIRE(changes.size() >= 2);
    REQUIRE(changes[0] == ActionChange::ActionEnabled);
    REQUIRE(changes[1] == ActionChange::ActionMapEnabled);

    insys_test::set_keys(manager, keyboard, {Key::Space}, 2.0);
    REQUIRE(std::find(changes.begin(), changes.end(), ActionChange::ActionStarted) != changes.end());
    REQUIRE(std::find(changes.begin(), changes.end(), ActionChange::ActionPerformed) != changes.end());

    const std::size_t before = changes.size();
    asset.disable();
    REQUIRE(changes.size() > before);
}

TEST_CASE("EventTrace records, saves, loads and replays state") {
    insys_test::TestRuntime runtime;
    Manager sourceManager(runtime);
    Keyboard* sourceKeyboard = insys_test::add_keyboard(sourceManager);

    EventTrace trace;
    trace.attach(sourceManager);
    trace.enable();
    insys_test::set_keys(sourceManager, sourceKeyboard, {Key::Space}, 3.0);
    trace.disable();
    REQUIRE(trace.event_count() >= 1);

    const auto path = std::filesystem::temp_directory_path() / "insys_m5_trace_test.bin";
    REQUIRE(trace.save(path));

    EventTrace loaded;
    REQUIRE(loaded.load(path));
    REQUIRE(loaded.event_count() == trace.event_count());
    REQUIRE(loaded.raw() == trace.raw());

    insys_test::TestRuntime replayRuntime;
    Manager replayManager(replayRuntime);
    Keyboard* replayKeyboard = insys_test::add_keyboard(replayManager);
    REQUIRE(replayKeyboard->deviceId == sourceKeyboard->deviceId);

    loaded.replay(replayManager);
    REQUIRE(replayKeyboard->key_control(Key::Space)->read_bool(
        replayKeyboard->current_state()));

    std::filesystem::remove(path);
}
TEST_CASE("EventTrace records device changes when enabled") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);

    EventTraceOptions options;
    options.recordDeviceChanges = true;
    EventTrace trace(options);
    trace.attach(manager);
    trace.enable();

    auto keyboard = make_keyboard(runtime.allocate_device_id());
    Device* raw = manager.add_device(std::move(keyboard));
    manager.remove_device(raw);
    trace.disable();

    REQUIRE(trace.event_count() == 2);
    REQUIRE(trace.total_bytes() > 0);

    const auto path = std::filesystem::temp_directory_path() / "insys_m5_trace_devices.bin";
    REQUIRE(trace.save(path));
    EventTrace loaded;
    REQUIRE(loaded.load(path));
    REQUIRE(loaded.event_count() == 2);
    std::filesystem::remove(path);
}
TEST_CASE("EventTrace honors its byte limit") {
    insys_test::TestRuntime runtime;
    Manager manager(runtime);
    Keyboard* keyboard = insys_test::add_keyboard(manager);

    EventTraceOptions options;
    options.maxBufferBytes = 32;
    options.recordDeviceChanges = false;
    options.stopWhenFull = true;
    EventTrace trace(options);
    trace.attach(manager);
    trace.enable();

    const auto first = low::make_text_event(U'a', keyboard->deviceId, 1.0);
    const auto second = low::make_text_event(U'b', keyboard->deviceId, 1.1);
    manager.queue_event(reinterpret_cast<const low::Event*>(first.data()));
    manager.queue_event(reinterpret_cast<const low::Event*>(second.data()));
    manager.update();

    REQUIRE(trace.event_count() == 1);
    REQUIRE_FALSE(trace.enabled());
    REQUIRE_FALSE(trace.last_error().empty());
}