#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "insys/low/event.h"
#include "insys/low/state_format.h"

using namespace insys::low;

TEST_CASE("Event header is 24 bytes and aligned to 8") {
    static_assert(sizeof(Event) == 24);
    static_assert(alignof(Event) == 8);
    REQUIRE(sizeof(Event) == kHeaderSize);
}

TEST_CASE("make_event fills the header") {
    const Event event = make_event(kTextEvent, sizeof(TextEvent), 7, 1.25);
    REQUIRE(event.type == kTextEvent.value);
    REQUIRE(event.size == sizeof(TextEvent));
    REQUIRE(event.deviceId == 7);
    REQUIRE(event.eventId == 0);
    REQUIRE(event.time == 1.25);
}

TEST_CASE("next_event advances by the aligned size") {
    const uint32_t firstSize = align_up(sizeof(TextEvent), kEventAlignment);
    std::vector<uint8_t> storage(firstSize * 2, 0);

    auto* first = reinterpret_cast<Event*>(storage.data());
    first->size = static_cast<uint16_t>(sizeof(TextEvent));

    const Event* second = next_event(first);
    REQUIRE(reinterpret_cast<const uint8_t*>(second) == storage.data() + firstSize);
}

TEST_CASE("Handled bit round-trips") {
    Event event = make_event(kTextEvent, sizeof(TextEvent), 1, 0.0);
    REQUIRE_FALSE(event_handled(&event));
    set_event_handled(&event, true);
    REQUIRE(event_handled(&event));
    set_event_handled(&event, false);
    REQUIRE_FALSE(event_handled(&event));
}

TEST_CASE("make_state_event copies the payload") {
    uint8_t state[4] = {1, 2, 3, 4};
    const auto buffer = make_state_event(insys::kFormatKeyboard, state, sizeof(state), 42, 3.5);

    const auto* event = reinterpret_cast<const StateEvent*>(buffer.data());
    REQUIRE(event->header.type == kStateEvent.value);
    REQUIRE(event->header.deviceId == 42);
    REQUIRE(event->header.time == 3.5);
    REQUIRE(event->header.size == align_up(static_cast<uint32_t>(offsetof(StateEvent, stateData)) + sizeof(state), kEventAlignment));
    REQUIRE(event->stateFormat == insys::kFormatKeyboard);
    REQUIRE(std::memcmp(event->stateData, state, sizeof(state)) == 0);
}

TEST_CASE("make_text_event encodes the codepoint") {
    const auto buffer = make_text_event(0x1F600, 3, 0.5);
    const auto* event = reinterpret_cast<const TextEvent*>(buffer.data());
    REQUIRE(event->header.type == kTextEvent.value);
    REQUIRE(event->header.deviceId == 3);
    REQUIRE(event->character == 0x1F600);
}
