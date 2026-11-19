#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "insys/backend/inotify_reader.h"

using namespace insys::backend::linux_evdev;

namespace {

void append_event(std::vector<std::uint8_t>& bytes, std::int32_t watchDescriptor,
                  std::uint32_t mask, std::uint32_t cookie,
                  const std::string& name = {}) {
    const std::size_t nameLength =
        name.empty() ? 0 : (name.size() + 1 + 3) & ~std::size_t{3};
    const std::size_t start = bytes.size();
    bytes.resize(start + InotifyEventDecoder::kHeaderSize + nameLength);
    std::memset(bytes.data() + start, 0, InotifyEventDecoder::kHeaderSize + nameLength);

    std::uint8_t* header = bytes.data() + start;
    const auto write_u32 = [](std::uint8_t* out, std::uint32_t value) {
        out[0] = static_cast<std::uint8_t>(value & 0xff);
        out[1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
        out[2] = static_cast<std::uint8_t>((value >> 16) & 0xff);
        out[3] = static_cast<std::uint8_t>((value >> 24) & 0xff);
    };
    write_u32(header, static_cast<std::uint32_t>(watchDescriptor));
    write_u32(header + 4, mask);
    write_u32(header + 8, cookie);
    write_u32(header + 12, static_cast<std::uint32_t>(nameLength));
    if (!name.empty())
        std::memcpy(header + InotifyEventDecoder::kHeaderSize, name.c_str(),
                    name.size() + 1);
}

} // namespace

TEST_CASE("InotifyEventDecoder decodes complete events") {
    std::vector<std::uint8_t> bytes;
    append_event(bytes, 3, kInCreate, 0, "event0");
    append_event(bytes, 3, kInMovedFrom, 42, "event1");

    InotifyEventDecoder decoder;
    REQUIRE(decoder.feed(bytes.data(), bytes.size()) == 2);
    REQUIRE(decoder.buffered_bytes() == 0);
    REQUIRE(decoder.events()[0].watchDescriptor == 3);
    REQUIRE(decoder.events()[0].mask == kInCreate);
    REQUIRE(decoder.events()[0].name == "event0");
    REQUIRE(decoder.events()[1].mask == kInMovedFrom);
    REQUIRE(decoder.events()[1].cookie == 42);
    REQUIRE(decoder.events()[1].name == "event1");
}

TEST_CASE("InotifyEventDecoder buffers split events") {
    std::vector<std::uint8_t> bytes;
    append_event(bytes, 1, kInDelete, 0, "event9");

    InotifyEventDecoder decoder;
    REQUIRE(decoder.feed(bytes.data(), 8) == 0);
    REQUIRE(decoder.buffered_bytes() == 8);
    REQUIRE(decoder.feed(bytes.data() + 8, bytes.size() - 8) == 1);
    REQUIRE(decoder.buffered_bytes() == 0);
    REQUIRE(decoder.events()[0].name == "event9");
}

TEST_CASE("InotifyEventDecoder handles events without a name") {
    std::vector<std::uint8_t> bytes;
    append_event(bytes, 5, kInQOverflow, 0);

    InotifyEventDecoder decoder;
    REQUIRE(decoder.feed(bytes.data(), bytes.size()) == 1);
    REQUIRE(decoder.events()[0].watchDescriptor == 5);
    REQUIRE(decoder.events()[0].name.empty());
    REQUIRE(is_input_scan_event(decoder.events()[0]));
}

TEST_CASE("InotifyEventDecoder rejects oversized names and resets") {
    std::vector<std::uint8_t> header(InotifyEventDecoder::kHeaderSize, 0);
    header[12] = 0xff;
    header[13] = 0xff;

    InotifyEventDecoder decoder;
    REQUIRE(decoder.feed(header.data(), header.size()) == 0);
    REQUIRE(decoder.buffered_bytes() == 0);

    std::vector<std::uint8_t> valid;
    append_event(valid, 2, kInIgnored, 0, "event2");
    REQUIRE(decoder.feed(valid.data(), valid.size()) == 1);
    decoder.clear_events();
    REQUIRE(decoder.events().empty());
    decoder.reset();
    REQUIRE(decoder.buffered_bytes() == 0);
}

TEST_CASE("Inotify scan-event masks cover input hotplug changes") {
    InotifyEvent event;
    const std::uint32_t masks[] = {kInCreate, kInDelete, kInMovedFrom, kInMovedTo,
                                   kInAttrib, kInDeleteSelf, kInMoveSelf,
                                   kInQOverflow, kInIgnored};
    for (const std::uint32_t mask : masks) {
        event.mask = mask;
        REQUIRE(is_input_scan_event(event));
    }

    event.mask = 0x00000002;
    REQUIRE_FALSE(is_input_scan_event(event));
    event.mask = 0x40000000;
    REQUIRE_FALSE(is_input_scan_event(event));
}
