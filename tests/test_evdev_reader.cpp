#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

#include "insys/backend/evdev_reader.h"

using namespace insys::backend::evdev;

namespace {

void append_event(std::vector<std::uint8_t>& bytes, std::uint16_t type,
                  std::uint16_t code, std::int32_t value,
                  std::size_t recordSize = 24, std::size_t typeOffset = 16) {
    const std::size_t start = bytes.size();
    bytes.resize(start + recordSize);
    std::memset(bytes.data() + start, 0, recordSize);
    std::uint8_t* record = bytes.data() + start;
    record[typeOffset] = static_cast<std::uint8_t>(type & 0xff);
    record[typeOffset + 1] = static_cast<std::uint8_t>((type >> 8) & 0xff);
    record[typeOffset + 2] = static_cast<std::uint8_t>(code & 0xff);
    record[typeOffset + 3] = static_cast<std::uint8_t>((code >> 8) & 0xff);
    const auto raw = static_cast<std::uint32_t>(value);
    record[typeOffset + 4] = static_cast<std::uint8_t>(raw & 0xff);
    record[typeOffset + 5] = static_cast<std::uint8_t>((raw >> 8) & 0xff);
    record[typeOffset + 6] = static_cast<std::uint8_t>((raw >> 16) & 0xff);
    record[typeOffset + 7] = static_cast<std::uint8_t>((raw >> 24) & 0xff);
}

} // namespace

void set_capability_bit(std::vector<std::uint8_t>& bits, std::uint16_t code) {
    const std::size_t byte = code / 8;
    if (bits.size() <= byte)
        bits.resize(byte + 1, 0);
    bits[byte] |= static_cast<std::uint8_t>(1u << (code % 8));
}

TEST_CASE("EvdevEventDecoder decodes complete records") {
    std::vector<std::uint8_t> bytes;
    append_event(bytes, kEvKey, 30, 1);
    append_event(bytes, kEvSyn, kSynReport, 0);

    EvdevEventDecoder decoder;
    REQUIRE(decoder.feed(bytes.data(), bytes.size()) == 2);
    REQUIRE(decoder.buffered_bytes() == 0);
    REQUIRE(decoder.events().size() == 2);
    REQUIRE(decoder.events()[0].type == kEvKey);
    REQUIRE(decoder.events()[0].code == 30);
    REQUIRE(decoder.events()[0].value == 1);
    REQUIRE(decoder.events()[1].type == kEvSyn);
    REQUIRE(decoder.events()[1].value == 0);
}

TEST_CASE("EvdevEventDecoder buffers partial records") {
    std::vector<std::uint8_t> bytes;
    append_event(bytes, kEvRel, kRelX, -5);

    EvdevEventDecoder decoder;
    REQUIRE(decoder.feed(bytes.data(), 10) == 0);
    REQUIRE(decoder.events().empty());
    REQUIRE(decoder.buffered_bytes() == 10);

    REQUIRE(decoder.feed(bytes.data() + 10, bytes.size() - 10) == 1);
    REQUIRE(decoder.buffered_bytes() == 0);
    REQUIRE(decoder.events()[0].value == -5);
}

TEST_CASE("EvdevEventDecoder supports 32-bit record layout") {
    std::vector<std::uint8_t> bytes;
    append_event(bytes, kEvAbs, kAbsX, 1234, 16, 8);

    EvdevEventDecoder decoder(16, 8);
    REQUIRE(decoder.feed(bytes.data(), bytes.size()) == 1);
    REQUIRE(decoder.events()[0].type == kEvAbs);
    REQUIRE(decoder.events()[0].code == kAbsX);
    REQUIRE(decoder.events()[0].value == 1234);
}

TEST_CASE("Evdev event encoder round-trips through decoder") {
    std::uint8_t record[24] = {};
    encode_evdev_event(record, EvdevEvent{kEvAbs, kAbsY, -1234});

    EvdevEventDecoder decoder;
    REQUIRE(decoder.feed(record, sizeof(record)) == 1);
    REQUIRE(decoder.events()[0].type == kEvAbs);
    REQUIRE(decoder.events()[0].code == kAbsY);
    REQUIRE(decoder.events()[0].value == -1234);
}

TEST_CASE("EvdevEventDecoder keeps trailing bytes and resets") {    std::vector<std::uint8_t> bytes;
    append_event(bytes, kEvKey, 31, 0);
    bytes.push_back(0xff);

    EvdevEventDecoder decoder;
    REQUIRE(decoder.feed(bytes.data(), bytes.size()) == 1);
    REQUIRE(decoder.buffered_bytes() == 1);

    decoder.clear_events();
    REQUIRE(decoder.events().empty());
    REQUIRE(decoder.buffered_bytes() == 1);

    decoder.reset();
    REQUIRE(decoder.events().empty());
    REQUIRE(decoder.buffered_bytes() == 0);
}

TEST_CASE("EvdevEventDecoder rejects invalid layout") {
    EvdevEventDecoder decoder(10, 8);
    const std::uint8_t byte = 0;
    REQUIRE(decoder.feed(&byte, 1) == 0);
    REQUIRE(decoder.buffered_bytes() == 0);
}

TEST_CASE("Evdev capabilities classify devices") {
    EvdevCapabilities capabilities;
    REQUIRE(classify_evdev_device(capabilities) == EvdevDeviceType::Unknown);

    capabilities.keys = true;
    REQUIRE(classify_evdev_device(capabilities) == EvdevDeviceType::Keyboard);

    capabilities.relative = true;
    capabilities.mouseButtons = true;
    REQUIRE(classify_evdev_device(capabilities) == EvdevDeviceType::Mouse);

    capabilities.absolute = true;
    capabilities.gamepadButtons = true;
    REQUIRE(classify_evdev_device(capabilities) == EvdevDeviceType::Gamepad);
}

TEST_CASE("Evdev capability bits decode keyboard") {
    EvdevCapabilityBits bits;
    set_capability_bit(bits.types, kEvKey);
    set_capability_bit(bits.keys, 30);

    const EvdevCapabilities capabilities = decode_evdev_capabilities(bits);
    REQUIRE(capabilities.keys);
    REQUIRE_FALSE(capabilities.relative);
    REQUIRE_FALSE(capabilities.mouseButtons);
    REQUIRE(classify_evdev_device(capabilities) == EvdevDeviceType::Keyboard);
    REQUIRE(evdev_bit_is_set(bits.keys, 30));
    REQUIRE_FALSE(evdev_bit_is_set(bits.keys, 31));
    REQUIRE_FALSE(evdev_bit_is_set(bits.keys, 1000));
}

TEST_CASE("Evdev capability bits decode mouse") {
    EvdevCapabilityBits bits;
    set_capability_bit(bits.types, kEvKey);
    set_capability_bit(bits.types, kEvRel);
    set_capability_bit(bits.keys, kBtnLeft);
    set_capability_bit(bits.relative, kRelX);

    const EvdevCapabilities capabilities = decode_evdev_capabilities(bits);
    REQUIRE(capabilities.keys);
    REQUIRE(capabilities.relative);
    REQUIRE(capabilities.mouseButtons);
    REQUIRE_FALSE(capabilities.gamepadButtons);
    REQUIRE(classify_evdev_device(capabilities) == EvdevDeviceType::Mouse);
}

TEST_CASE("Evdev capability bits decode gamepad") {
    EvdevCapabilityBits bits;
    set_capability_bit(bits.types, kEvKey);
    set_capability_bit(bits.types, kEvAbs);
    set_capability_bit(bits.keys, kBtnSouth);
    set_capability_bit(bits.absolute, kAbsX);

    const EvdevCapabilities capabilities = decode_evdev_capabilities(bits);
    REQUIRE(capabilities.absolute);
    REQUIRE(capabilities.gamepadButtons);
    REQUIRE_FALSE(capabilities.mouseButtons);
    REQUIRE(classify_evdev_device(capabilities) == EvdevDeviceType::Gamepad);
}
