#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "insys/low/event_buffer.h"
#include "insys/low/event_stream.h"

using namespace insys::low;

namespace {

Event* append_text(EventBuffer& buffer, int32_t character, double time) {
    Event* event = buffer.allocate(sizeof(TextEvent));
    REQUIRE(event != nullptr);
    auto* text = reinterpret_cast<TextEvent*>(event);
    text->header.type = kTextEvent.value;
    text->header.size = static_cast<uint16_t>(align_up(sizeof(TextEvent), kEventAlignment));
    text->header.deviceId = 1;
    text->header.time = time;
    text->character = character;
    return event;
}

EventBuffer make_buffer(std::initializer_list<int32_t> characters) {
    EventBuffer buffer;
    double time = 0.0;
    for (int32_t character : characters) {
        append_text(buffer, character, time);
        time += 1.0;
    }
    return buffer;
}

int32_t character_of(const Event* event) {
    return reinterpret_cast<const TextEvent*>(event)->character;
}

} // namespace

TEST_CASE("EventStream iterates native events in order") {
    EventStream stream(make_buffer({'a', 'b', 'c'}), 8);
    REQUIRE(stream.is_open());
    REQUIRE(stream.remaining() == 3);

    std::string sequence;
    while (Event* event = stream.current()) {
        sequence.push_back(static_cast<char>(character_of(event)));
        stream.advance(false);
    }

    REQUIRE(sequence == "abc");
    EventBuffer out;
    stream.close(out);
    REQUIRE(out.event_count() == 0);
}

TEST_CASE("Events written while iterating are processed after native events") {
    EventStream stream(make_buffer({'a', 'b'}), 8);

    EventBuffer extra;
    append_text(extra, 'z', 2.0);

    std::string sequence;
    while (Event* event = stream.current()) {
        sequence.push_back(static_cast<char>(character_of(event)));
        if (sequence.size() == 1)
            stream.write(extra.begin());
        stream.advance(false);
    }

    REQUIRE(sequence == "abz");
}

TEST_CASE("Keep-in-buffer carries events into the output") {
    EventStream stream(make_buffer({'a', 'b', 'c'}), 8);

    REQUIRE(character_of(stream.current()) == 'a');
    stream.advance(true);

    REQUIRE(character_of(stream.current()) == 'b');
    stream.advance(false);

    REQUIRE(character_of(stream.current()) == 'c');
    stream.advance(false);

    EventBuffer out;
    stream.close(out);
    REQUIRE(out.event_count() == 1);
    REQUIRE(character_of(out.begin()) == 'a');
}

TEST_CASE("Appended event limit is enforced") {
    EventStream stream(make_buffer({'a'}), 1);

    EventBuffer first;
    append_text(first, 'x', 1.0);
    EventBuffer second;
    append_text(second, 'y', 2.0);

    stream.write(first.begin());
    stream.write(second.begin());

    std::string sequence;
    while (Event* event = stream.current()) {
        sequence.push_back(static_cast<char>(character_of(event)));
        stream.advance(false);
    }

    REQUIRE(sequence == "ax");
}

TEST_CASE("EventBuffer reports sizes and counts") {
    EventBuffer buffer = make_buffer({'a', 'b', 'c'});
    REQUIRE(buffer.event_count() == 3);
    REQUIRE(buffer.size_bytes() > 0);

    const uint32_t size = buffer.size_bytes();
    REQUIRE(size == static_cast<uint32_t>(align_up(sizeof(TextEvent), kEventAlignment) * 3));

    buffer.reset();
    REQUIRE(buffer.event_count() == 0);
    REQUIRE(buffer.empty());
}
