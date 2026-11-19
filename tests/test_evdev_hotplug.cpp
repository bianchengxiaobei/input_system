#include <catch2/catch_test_macros.hpp>

#include "insys/backend/evdev_hotplug.h"

using namespace insys::backend::evdev;

TEST_CASE("EvdevDeviceTracker reports initial devices as added") {
    EvdevDeviceTracker tracker;
    const auto changes = tracker.update({
        {"/dev/input/event0", EvdevDeviceType::Keyboard},
        {"/dev/input/event1", EvdevDeviceType::Mouse},
    });

    REQUIRE(changes.size() == 2);
    REQUIRE(changes[0].kind == EvdevChangeKind::Added);
    REQUIRE(changes[0].path == "/dev/input/event0");
    REQUIRE(changes[1].kind == EvdevChangeKind::Added);
    REQUIRE(changes[1].type == EvdevDeviceType::Mouse);
    REQUIRE(tracker.devices().size() == 2);
}

TEST_CASE("EvdevDeviceTracker ignores unchanged devices") {
    EvdevDeviceTracker tracker;
    tracker.update({{"keyboard", EvdevDeviceType::Keyboard}});
    const auto changes = tracker.update({{"keyboard", EvdevDeviceType::Keyboard}});

    REQUIRE(changes.empty());
    REQUIRE(tracker.devices().size() == 1);
}

TEST_CASE("EvdevDeviceTracker reports removals and additions") {
    EvdevDeviceTracker tracker;
    tracker.update({
        {"keyboard", EvdevDeviceType::Keyboard},
        {"mouse", EvdevDeviceType::Mouse},
    });

    const auto changes = tracker.update({
        {"keyboard", EvdevDeviceType::Keyboard},
        {"gamepad", EvdevDeviceType::Gamepad},
    });

    REQUIRE(changes.size() == 2);
    REQUIRE(changes[0].kind == EvdevChangeKind::Removed);
    REQUIRE(changes[0].path == "mouse");
    REQUIRE(changes[1].kind == EvdevChangeKind::Added);
    REQUIRE(changes[1].path == "gamepad");
    REQUIRE(tracker.devices().size() == 2);
}

TEST_CASE("EvdevDeviceTracker treats type changes as replace") {
    EvdevDeviceTracker tracker;
    tracker.update({{"event0", EvdevDeviceType::Keyboard}});

    const auto changes = tracker.update({{"event0", EvdevDeviceType::Gamepad}});
    REQUIRE(changes.size() == 2);
    REQUIRE(changes[0].kind == EvdevChangeKind::Removed);
    REQUIRE(changes[0].type == EvdevDeviceType::Keyboard);
    REQUIRE(changes[1].kind == EvdevChangeKind::Added);
    REQUIRE(changes[1].type == EvdevDeviceType::Gamepad);
    REQUIRE(tracker.devices()[0].type == EvdevDeviceType::Gamepad);
}

TEST_CASE("EvdevDeviceTracker skips empty and duplicate paths") {
    EvdevDeviceTracker tracker;
    const auto changes = tracker.update({
        {"", EvdevDeviceType::Keyboard},
        {"event0", EvdevDeviceType::Mouse},
        {"event0", EvdevDeviceType::Gamepad},
    });

    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].path == "event0");
    REQUIRE(tracker.devices().size() == 1);
    REQUIRE(tracker.devices()[0].type == EvdevDeviceType::Mouse);

    tracker.clear();
    REQUIRE(tracker.devices().empty());
    REQUIRE(tracker.update({{"event0", EvdevDeviceType::Mouse}}).size() == 1);
}
