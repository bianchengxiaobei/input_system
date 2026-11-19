#include "insys/low/event.h"

#include <cstring>

#include "insys/low/event_stream.h" // not required, kept for symmetry with other TUs

namespace insys::low {
namespace {

std::vector<uint8_t> make_buffer(uint32_t size, FourCC type, int deviceId, double time) {
    const uint32_t alignedSize = align_up(size, kEventAlignment);
    std::vector<uint8_t> buffer(alignedSize, 0);
    auto* header = reinterpret_cast<Event*>(buffer.data());
    header->type = type.value;
    header->size = static_cast<uint16_t>(alignedSize);
    header->deviceId = static_cast<uint16_t>(deviceId);
    header->eventId = 0;
    header->time = time;
    return buffer;
}

} // namespace

Event make_event(FourCC type, uint16_t size, int deviceId, double time) {
    Event event{};
    event.type = type.value;
    event.size = size;
    event.deviceId = static_cast<uint16_t>(deviceId);
    event.eventId = 0;
    event.time = time;
    return event;
}

std::vector<uint8_t> make_state_event(FourCC stateFormat, const void* state,
                                      uint32_t stateSize, int deviceId, double time) {
    const uint32_t payloadOffset = static_cast<uint32_t>(offsetof(StateEvent, stateData));
    auto buffer = make_buffer(payloadOffset + stateSize, kStateEvent, deviceId, time);
    auto* event = reinterpret_cast<StateEvent*>(buffer.data());
    event->stateFormat = stateFormat;
    if (stateSize > 0 && state != nullptr)
        std::memcpy(event->stateData, state, stateSize);
    return buffer;
}

std::vector<uint8_t> make_text_event(int32_t character, int deviceId, double time) {
    auto buffer = make_buffer(sizeof(TextEvent), kTextEvent, deviceId, time);
    auto* event = reinterpret_cast<TextEvent*>(buffer.data());
    event->character = character;
    return buffer;
}

} // namespace insys::low
