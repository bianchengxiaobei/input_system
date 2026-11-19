#include <catch2/catch_test_macros.hpp>

#include "insys/backend/runtime.h"
#include "insys/device_commands.h"
#include "insys/system.h"

using namespace insys;
using namespace insys::backend;

TEST_CASE("Manager forwards device commands through runtime") {
    Runtime runtime;
    System system(runtime);

    Gamepad* gamepad = static_cast<Gamepad*>(
        system.manager().create_device("Gamepad", DeviceDescription{}));
    REQUIRE(gamepad != nullptr);

    int receivedDevice = 0;
    FourCC receivedType{};
    runtime.set_device_command_handler([&](int deviceId, DeviceCommand* command) {
        receivedDevice = deviceId;
        receivedType = command->type;
        return 7;
    });

    GetHapticCapabilitiesCommand request;
    DeviceCommand command{kGetHapticCapabilitiesCommand, &request};
    REQUIRE(system.send_device_command(gamepad, &command) == 7);
    REQUIRE(receivedDevice == gamepad->deviceId);
    REQUIRE(receivedType == kGetHapticCapabilitiesCommand);

    REQUIRE(system.send_device_command(nullptr, &command) == -1);
    REQUIRE(system.send_device_command(gamepad, nullptr) == -1);

    runtime.clear_device_command_handler();
    REQUIRE(system.send_device_command(gamepad, &command) == -1);
}
