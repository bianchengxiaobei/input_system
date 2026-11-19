#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "insys/backend/inotify_monitor.h"
#include "insys/backend/linux_inotify.h"

using namespace insys::backend::linux_evdev;

namespace {

void append_event(std::vector<std::uint8_t>& bytes, std::int32_t watchDescriptor,
                  std::uint32_t mask, std::uint32_t cookie,
                  const std::string& name) {
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

TEST_CASE("InputHotplugMonitor filters input device events") {
    InputHotplugMonitor monitor;

    REQUIRE(monitor.feed(InotifyEvent{3, kInCreate, 0, "event0"}));
    REQUIRE(monitor.feed(InotifyEvent{3, kInDelete, 0, "event1"}));
    REQUIRE(monitor.feed(InotifyEvent{3, kInMovedTo, 7, "event2"}));
    REQUIRE(monitor.feed(InotifyEvent{3, kInAttrib, 0, "event3"}));

    REQUIRE_FALSE(monitor.feed(InotifyEvent{3, kInCreate, 0, "mice"}));
    REQUIRE_FALSE(monitor.feed(InotifyEvent{3, 0x00000002, 0, "event4"}));
    REQUIRE_FALSE(monitor.feed(InotifyEvent{3, kInCreate, 0, "by-id"}));
}

TEST_CASE("InputHotplugMonitor scans on watch loss and overflow") {
    InputHotplugMonitor monitor;
    REQUIRE(monitor.feed(InotifyEvent{1, kInQOverflow, 0, {}}));
    REQUIRE(monitor.feed(InotifyEvent{1, kInIgnored, 0, {}}));
    REQUIRE(monitor.feed(InotifyEvent{1, kInDeleteSelf, 0, {}}));
    REQUIRE(monitor.feed(InotifyEvent{1, kInMoveSelf, 0, {}}));
}

TEST_CASE("InputHotplugMonitor decodes byte streams") {
    std::vector<std::uint8_t> bytes;
    append_event(bytes, 3, kInCreate, 0, "event0");
    append_event(bytes, 3, kInDelete, 0, "mice");

    InputHotplugMonitor monitor;
    REQUIRE(monitor.feed(bytes.data(), bytes.size()));

    std::vector<std::uint8_t> unrelated;
    append_event(unrelated, 3, kInCreate, 0, "mice");
    REQUIRE_FALSE(monitor.feed(unrelated.data(), unrelated.size()));
}

TEST_CASE("InputHotplugMonitor buffers partial byte streams") {
    std::vector<std::uint8_t> bytes;
    append_event(bytes, 3, kInCreate, 0, "event5");

    InputHotplugMonitor monitor;
    REQUIRE_FALSE(monitor.feed(bytes.data(), 8));
    REQUIRE(monitor.feed(bytes.data() + 8, bytes.size() - 8));
}

TEST_CASE("Linux inotify watcher exposes a safe fallback") {
#if !defined(__linux__)
    LinuxInotifyWatcher watcher;
    REQUIRE_FALSE(watcher.initialize());
    REQUIRE_FALSE(watcher.valid());
    REQUIRE_FALSE(watcher.poll_scan_request());
    watcher.shutdown();
#else
    SUCCEED("Linux inotify watcher available");
#endif
}
