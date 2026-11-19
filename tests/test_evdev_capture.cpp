#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "insys/backend/evdev_capture.h"

using namespace insys;
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

EvdevCaptureSession::ReadFunction make_reader(std::vector<std::uint8_t> bytes,
                                              std::size_t chunk = 256) {
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

bool key_down(const EvdevDeviceState& state, Key key) {
    const auto bit = static_cast<std::uint32_t>(key);
    return (state.keyboard_bits()[bit / 8] & (1u << (bit % 8))) != 0;
}

} // namespace

TEST_CASE("EvdevCaptureSession emits dirty keyboard frame on SYN_REPORT") {
    std::vector<std::uint8_t> bytes;
    append_event(bytes, kEvKey, 30, 1);
    append_event(bytes, kEvSyn, kSynReport, 0);

    EvdevCaptureSession session;
    REQUIRE(session.add_device("/dev/input/event0", EvdevDeviceType::Keyboard,
                               make_reader(bytes)));
    REQUIRE(session.poll() == 2);
    REQUIRE(session.device_state("/dev/input/event0") != nullptr);
    REQUIRE(key_down(*session.device_state("/dev/input/event0"), Key::A));

    const auto dirty = session.take_dirty_devices();
    REQUIRE(dirty.size() == 1);
    REQUIRE(dirty[0] == "/dev/input/event0");
    REQUIRE(session.take_dirty_devices().empty());
}

TEST_CASE("EvdevCaptureSession waits for SYN_REPORT") {
    std::vector<std::uint8_t> bytes;
    append_event(bytes, kEvKey, 31, 1);

    EvdevCaptureSession session;
    REQUIRE(session.add_device("keyboard", EvdevDeviceType::Keyboard,
                               make_reader(bytes)));
    REQUIRE(session.poll() == 1);
    REQUIRE(key_down(*session.device_state("keyboard"), Key::S));
    REQUIRE(session.take_dirty_devices().empty());
}

TEST_CASE("EvdevCaptureSession handles records split across reads") {
    std::vector<std::uint8_t> bytes;
    append_event(bytes, kEvRel, kRelX, 5);
    append_event(bytes, kEvRel, kRelY, -2);
    append_event(bytes, kEvKey, kBtnLeft, 1);
    append_event(bytes, kEvSyn, kSynReport, 0);

    EvdevCaptureSession session;
    REQUIRE(session.add_device("mouse", EvdevDeviceType::Mouse, make_reader(bytes, 7)));
    REQUIRE(session.poll() == 4);

    const MouseState& mouse = session.device_state("mouse")->mouse();
    REQUIRE(mouse.delta.x == 5.0f);
    REQUIRE(mouse.delta.y == -2.0f);
    REQUIRE((mouse.buttons[0] & 1u) != 0);
    REQUIRE(session.take_dirty_devices().size() == 1);
}

TEST_CASE("EvdevCaptureSession manages multiple devices") {
    std::vector<std::uint8_t> keyboardBytes;
    append_event(keyboardBytes, kEvKey, 30, 1);
    append_event(keyboardBytes, kEvSyn, kSynReport, 0);
    std::vector<std::uint8_t> gamepadBytes;
    append_event(gamepadBytes, kEvKey, kBtnSouth, 1);
    append_event(gamepadBytes, kEvSyn, kSynReport, 0);

    EvdevCaptureSession session;
    REQUIRE(session.add_device("keyboard", EvdevDeviceType::Keyboard,
                               make_reader(keyboardBytes)));
    REQUIRE(session.add_device("gamepad", EvdevDeviceType::Gamepad,
                               make_reader(gamepadBytes)));
    REQUIRE_FALSE(session.add_device("keyboard", EvdevDeviceType::Keyboard,
                                     make_reader({})));
    REQUIRE(session.device_count() == 2);

    REQUIRE(session.poll() == 4);
    const auto dirty = session.take_dirty_devices();
    REQUIRE(dirty.size() == 2);
    REQUIRE(std::find(dirty.begin(), dirty.end(), "keyboard") != dirty.end());
    REQUIRE(std::find(dirty.begin(), dirty.end(), "gamepad") != dirty.end());

    REQUIRE(session.remove_device("keyboard"));
    REQUIRE_FALSE(session.remove_device("keyboard"));
    REQUIRE(session.device_count() == 1);
    REQUIRE(session.device_state("keyboard") == nullptr);

    session.clear();
    REQUIRE(session.device_count() == 0);
}

TEST_CASE("EvdevCaptureSession ignores empty reads") {
    EvdevCaptureSession session;
    REQUIRE(session.add_device("empty", EvdevDeviceType::Mouse, make_reader({})));
    REQUIRE(session.poll() == 0);
    REQUIRE(session.take_dirty_devices().empty());
}
