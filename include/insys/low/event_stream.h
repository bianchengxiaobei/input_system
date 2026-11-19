#pragma once

#include "insys/low/event_buffer.h"

namespace insys::low {

class EventStream {
public:
    EventStream(EventBuffer nativeBuffer, int maxAppendedEvents);

    bool is_open() const { return m_open; }
    int remaining() const { return m_remainingNative + m_remainingAppend; }
    int remaining_native() const { return m_remainingNative; }

    Event* current() const;
    Event* peek() const;

    // Advances past the current event. If `keepInBuffer` is true the event is
    // copied into the appended buffer so it is carried over to the next update.
    Event* advance(bool keepInBuffer);

    // Appends an event that will be processed after all native events.
    void write(Event* event);

    // Hands over leftover events (kept + appended but unprocessed).
    void close(EventBuffer& out);

    // Returns the native buffer after iteration so the queue can recycle its
    // allocation. Must be called after the last current()/advance().
    EventBuffer release_native_buffer();

    void cleanup_after_exception();

private:
    EventBuffer m_native;
    EventBuffer m_append;

    Event* m_nativeRead = nullptr;
    Event* m_appendRead = nullptr;
    int m_remainingNative = 0;
    int m_remainingAppend = 0;
    int m_maxAppended = 0;
    bool m_open = false;
};

} // namespace insys::low
