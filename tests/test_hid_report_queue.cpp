#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstring>

#include "insys/backend/hid_backend.h"
#include "insys/backend/hid_report_queue.h"
#include "insys/backend/runtime.h"
#include "insys/device.h"
#include "insys/device_states.h"
#include "insys/hid/hid_gamepad.h"
#include "insys/manager.h"

using namespace insys;
using namespace insys::backend;

TEST_CASE("HID report queue drains reports and removals") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);

    HidBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    hid::DeviceInfo info;
    info.id = "queue-pad";
    info.vendorId = 0x054C;
    info.productId = 0x05C4;
    info.description.product = "DualShock 4";
    REQUIRE(backend.add_device(info));
    REQUIRE(manager.devices().size() == 1);
    Device* device = manager.devices()[0];

    HidReportQueue queue;
    REQUIRE(queue.empty());

    const std::array<uint8_t, 10> report = {
        0x01, 255, 128, 0, 128, 0x20, 0x00, 0x00, 255, 0,
    };
    queue.push_report("queue-pad", report.data(), report.size());
    REQUIRE_FALSE(queue.empty());
    REQUIRE(queue.drain(backend) == 1);
    REQUIRE(queue.empty());
    backend.poll();
    manager.update(UpdateType::Dynamic);

    GamepadState state;
    std::memcpy(&state, device->current_state(), sizeof(state));
    REQUIRE(state.leftStick.x == 1.0f);
    REQUIRE(state.leftTrigger == 1.0f);

    queue.push_report("queue-pad", report.data(), report.size());
    REQUIRE(queue.drain(backend) == 0);

    queue.push_report("missing", report.data(), report.size());
    REQUIRE(queue.drain(backend) == 0);

    queue.push_removal("queue-pad");
    REQUIRE(queue.drain(backend) == 1);
    REQUIRE(manager.devices().empty());
    REQUIRE_FALSE(backend.connected("queue-pad"));

    queue.push_removal("missing");
    REQUIRE(queue.drain(backend) == 0);

    backend.shutdown();
}

TEST_CASE("HID report queue applies backpressure and discards device reports") {
    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);

    HidBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    hid::DeviceInfo info;
    info.id = "queue-limit";
    info.vendorId = 0x054C;
    info.productId = 0x05C4;
    info.description.product = "DualShock 4";
    REQUIRE(backend.add_device(info));

    HidReportQueue queue(2);
    REQUIRE(queue.capacity() == 2);
    const std::array<uint8_t, 10> report = {
        0x01, 255, 128, 0, 128, 0x20, 0x00, 0x00, 255, 0,
    };
    REQUIRE(queue.push_report("queue-limit", report.data(), report.size()));
    REQUIRE(queue.push_report("queue-limit", report.data(), report.size()));
    REQUIRE(queue.push_report("queue-limit", report.data(), report.size()));
    REQUIRE(queue.pending() == 2);
    REQUIRE(queue.take_dropped_count() == 1);
    REQUIRE(queue.take_dropped_count() == 0);

    queue.push_removal("queue-limit");
    REQUIRE(queue.pending() == 2);

    queue.discard("queue-limit");
    REQUIRE(queue.empty());

    queue.set_capacity(1);
    REQUIRE(queue.push_report("queue-limit", report.data(), report.size()));
    REQUIRE(queue.pending() == 1);
    queue.clear();
    REQUIRE(queue.empty());

    backend.shutdown();
}


