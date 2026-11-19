#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "insys/backend/evdev_backend.h"
#include "insys/backend/runtime.h"
#include "insys/device.h"
#include "insys/manager.h"

using namespace insys;
using namespace insys::backend;
using namespace insys::backend::evdev;

namespace {

void append_event(std::vector<std::uint8_t>& bytes, std::uint16_t type,
                  std::uint16_t code, std::int32_t value) {
    const std::size_t start = bytes.size();
    bytes.resize(start + 24);
    std::memset(bytes.data() + start, 0, 24);
    std::uint8_t* record = bytes.data() + start;
    record[16] = static_cast<std::uint8_t>(type & 0xff);
    record[17] = static_cast<std::uint8_t>((type >> 8) & 0xff);
    record[18] = static_cast<std::uint8_t>(code & 0xff);
    record[19] = static_cast<std::uint8_t>((code >> 8) & 0xff);
    const auto raw = static_cast<std::uint32_t>(value);
    record[20] = static_cast<std::uint8_t>(raw & 0xff);
    record[21] = static_cast<std::uint8_t>((raw >> 8) & 0xff);
    record[22] = static_cast<std::uint8_t>((raw >> 16) & 0xff);
    record[23] = static_cast<std::uint8_t>((raw >> 24) & 0xff);
}

EvdevCaptureSession::ReadFunction
make_reader(std::vector<std::uint8_t> bytes, std::size_t chunk = 256) {
    auto data = std::make_shared<std::vector<std::uint8_t>>(std::move(bytes));
    auto offset = std::make_shared<std::size_t>(0);
    return [data, offset, chunk](std::uint8_t* buffer, std::size_t capacity) {
        if (*offset >= data->size())
            return std::size_t{0};
        const std::size_t count =
            std::min({chunk, capacity, data->size() - *offset});
        std::memcpy(buffer, data->data() + *offset, count);
        *offset += count;
        return count;
    };
}

EvdevBackend::SourceFactory
single_source(const std::string& path, std::vector<std::uint8_t> bytes) {
    return [path, bytes = std::move(bytes)](const EvdevDeviceInfo& info) {
        if (info.path != path)
            return EvdevCaptureSession::ReadFunction{};
        return make_reader(bytes);
    };
}

bool key_down(const Keyboard& keyboard, Key key) {
    const auto bit = static_cast<std::uint32_t>(key);
    return (keyboard.current_state()[bit / 8] & (1u << (bit % 8))) != 0;
}

} // namespace

TEST_CASE("EvdevBackend creates keyboard and emits state events") {
    std::vector<std::uint8_t> bytes;
    append_event(bytes, evdev::kEvKey, 30, 1);
    append_event(bytes, evdev::kEvSyn, evdev::kSynReport, 0);

    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    EvdevBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    backend.update_devices({{"keyboard", evdev::EvdevDeviceType::Keyboard}},
                           single_source("keyboard", bytes));
    REQUIRE(manager.devices().size() == 1);
    auto* keyboard = static_cast<Keyboard*>(manager.devices()[0]);
    REQUIRE(keyboard->description.serial == "keyboard");
    REQUIRE_FALSE(key_down(*keyboard, Key::A));

    backend.poll();
    manager.update(UpdateType::Dynamic);
    REQUIRE(key_down(*keyboard, Key::A));
}

TEST_CASE("EvdevBackend maps mouse deltas and gamepad state") {
    std::vector<std::uint8_t> mouseBytes;
    append_event(mouseBytes, evdev::kEvRel, evdev::kRelX, 5);
    append_event(mouseBytes, evdev::kEvRel, evdev::kRelY, -2);
    append_event(mouseBytes, evdev::kEvSyn, evdev::kSynReport, 0);

    std::vector<std::uint8_t> gamepadBytes;
    append_event(gamepadBytes, evdev::kEvAbs, evdev::kAbsX, 32767);
    append_event(gamepadBytes, evdev::kEvAbs, evdev::kAbsY, -32768);
    append_event(gamepadBytes, evdev::kEvKey, evdev::kBtnSouth, 1);
    append_event(gamepadBytes, evdev::kEvSyn, evdev::kSynReport, 0);

    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    EvdevBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    backend.update_devices(
        {{"mouse", evdev::EvdevDeviceType::Mouse},
         {"gamepad", evdev::EvdevDeviceType::Gamepad}},
        [mouseBytes, gamepadBytes](const EvdevDeviceInfo& info) {
            if (info.path == "mouse")
                return make_reader(mouseBytes);
            if (info.path == "gamepad")
                return make_reader(gamepadBytes);
            return EvdevCaptureSession::ReadFunction{};
        });

    REQUIRE(manager.devices().size() == 2);
    backend.poll();
    manager.update(UpdateType::Dynamic);

    MouseState mouse;
    GamepadState gamepad;
    for (Device* device : manager.devices()) {
        if (device->description.serial == "mouse")
            std::memcpy(&mouse, device->current_state(), sizeof(mouse));
        if (device->description.serial == "gamepad")
            std::memcpy(&gamepad, device->current_state(), sizeof(gamepad));
    }

    REQUIRE(mouse.delta.x == 5.0f);
    REQUIRE(mouse.delta.y == -2.0f);
    REQUIRE(gamepad.leftStick.x == 1.0f);
    REQUIRE(gamepad.leftStick.y == -1.0f);
    REQUIRE((gamepad.buttons & (1u << 6)) != 0);
}

TEST_CASE("EvdevBackend removes devices and retries unavailable sources") {
    std::vector<std::uint8_t> bytes;
    append_event(bytes, evdev::kEvKey, 30, 1);
    append_event(bytes, evdev::kEvSyn, evdev::kSynReport, 0);

    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    EvdevBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    backend.update_devices({{"keyboard", evdev::EvdevDeviceType::Keyboard}},
                           [](const EvdevDeviceInfo&) {
                               return EvdevCaptureSession::ReadFunction{};
                           });
    REQUIRE(manager.devices().empty());

    backend.update_devices({{"keyboard", evdev::EvdevDeviceType::Keyboard}},
                           single_source("keyboard", bytes));
    REQUIRE(manager.devices().size() == 1);

    backend.update_devices({}, single_source("keyboard", bytes));
    REQUIRE(manager.devices().empty());

    backend.shutdown();
    REQUIRE(manager.devices().empty());
}

TEST_CASE("EvdevBackend replaces devices when type changes") {
    std::vector<std::uint8_t> keyboardBytes;
    append_event(keyboardBytes, evdev::kEvKey, 30, 1);
    append_event(keyboardBytes, evdev::kEvSyn, evdev::kSynReport, 0);

    std::vector<std::uint8_t> gamepadBytes;
    append_event(gamepadBytes, evdev::kEvAbs, evdev::kAbsX, 32767);
    append_event(gamepadBytes, evdev::kEvSyn, evdev::kSynReport, 0);

    Runtime runtime;
    Manager manager(runtime);
    runtime.set_manager(&manager);
    EvdevBackend backend;
    REQUIRE(backend.initialize(runtime, manager));

    backend.update_devices({{"event0", evdev::EvdevDeviceType::Keyboard}},
                           single_source("event0", keyboardBytes));
    REQUIRE(manager.devices().size() == 1);
    REQUIRE(manager.devices()[0]->description.deviceClass == "Keyboard");

    backend.update_devices({{"event0", evdev::EvdevDeviceType::Gamepad}},
                           single_source("event0", gamepadBytes));
    REQUIRE(manager.devices().size() == 1);
    REQUIRE(manager.devices()[0]->description.deviceClass == "Gamepad");

    backend.poll();
    manager.update(UpdateType::Dynamic);
    GamepadState gamepad;
    std::memcpy(&gamepad, manager.devices()[0]->current_state(), sizeof(gamepad));
    REQUIRE(gamepad.leftStick.x == 1.0f);
}



