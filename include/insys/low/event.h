#pragma once

#include <cstdint>
#include <vector>

#include "insys/low/fourcc.h"

namespace insys::low {

constexpr uint32_t kEventAlignment = 8;
constexpr uint32_t kHeaderSize = 24;
constexpr uint32_t kHandledBit = 0x80000000u;

inline constexpr FourCC kStateEvent = FourCC('S', 'T', 'A', 'T');
inline constexpr FourCC kDeltaStateEvent = FourCC('D', 'L', 'T', 'A');
inline constexpr FourCC kTextEvent = FourCC('T', 'E', 'X', 'T');
inline constexpr FourCC kDeviceRemoveEvent = FourCC('D', 'R', 'E', 'M');
inline constexpr FourCC kDeviceConfigurationEvent = FourCC('D', 'C', 'F', 'G');
inline constexpr FourCC kDeviceResetEvent = FourCC('D', 'R', 'S', 'T');

struct Event {
    uint32_t type;
    uint16_t size;
    uint16_t deviceId;
    uint32_t eventId;
    uint32_t _pad0;
    double time;
};
static_assert(sizeof(Event) == kHeaderSize, "Event header must stay 24 bytes");

struct StateEvent {
    Event header;
    FourCC stateFormat;
    uint8_t stateData[1];
};

struct DeltaStateEvent {
    Event header;
    FourCC stateFormat;
    uint32_t stateOffset;
    uint8_t deltaState[1];
};

struct TextEvent {
    Event header;
    int32_t character;
};

struct DeviceRemoveEvent {
    Event header;
};

struct DeviceConfigurationEvent {
    Event header;
};

struct DeviceResetEvent {
    Event header;
    bool hardReset;
    uint8_t _pad[3];
};

inline uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

inline Event* next_event(Event* event) {
    return reinterpret_cast<Event*>(reinterpret_cast<uint8_t*>(event) +
                                    align_up(event->size, kEventAlignment));
}

inline const Event* next_event(const Event* event) {
    return reinterpret_cast<const Event*>(reinterpret_cast<const uint8_t*>(event) +
                                          align_up(event->size, kEventAlignment));
}

inline bool event_handled(const Event* event) {
    return (event->eventId & kHandledBit) != 0;
}

inline void set_event_handled(Event* event, bool handled) {
    if (handled)
        event->eventId |= kHandledBit;
    else
        event->eventId &= ~kHandledBit;
}

Event make_event(FourCC type, uint16_t size, int deviceId, double time);

std::vector<uint8_t> make_state_event(FourCC stateFormat, const void* state,
                                      uint32_t stateSize, int deviceId, double time);

std::vector<uint8_t> make_text_event(int32_t character, int deviceId, double time);

} // namespace insys::low
