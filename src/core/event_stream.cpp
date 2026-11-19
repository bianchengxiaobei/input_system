#include "insys/low/event_stream.h"

namespace insys::low {

EventStream::EventStream(EventBuffer nativeBuffer, int maxAppendedEvents)
    : m_native(std::move(nativeBuffer)),
      m_maxAppended(maxAppendedEvents < 0 ? 0 : maxAppendedEvents) {
    m_nativeRead = m_native.begin();
    m_remainingNative = m_native.event_count();
    m_appendRead = nullptr;
    m_remainingAppend = 0;
    m_open = true;
}

Event* EventStream::current() const {
    if (m_remainingNative > 0)
        return m_nativeRead;
    if (m_remainingAppend > 0)
        return m_appendRead;
    return nullptr;
}

Event* EventStream::peek() const {
    return current();
}

Event* EventStream::advance(bool keepInBuffer) {
    Event* event = current();
    if (event == nullptr)
        return nullptr;

    // Only native events can be kept. Writing while reading the appended
    // buffer could reallocate it and invalidate our read pointer.
    if (keepInBuffer && m_remainingNative > 0)
        write(event);

    if (m_remainingNative > 0) {
        m_nativeRead = next_event(m_nativeRead);
        --m_remainingNative;
        if (m_remainingNative == 0 && m_remainingAppend > 0)
            m_appendRead = m_append.begin();
    } else {
        m_appendRead = next_event(m_appendRead);
        --m_remainingAppend;
    }

    return current();
}

void EventStream::write(Event* event) {
    if (event == nullptr || !m_open)
        return;
    if (m_remainingAppend >= m_maxAppended)
        return;

    const bool readingAppend = (m_remainingNative == 0) && (m_remainingAppend > 0);
    const int appendIndex = m_append.event_count();

    m_append.append(event);
    ++m_remainingAppend;

    if (!readingAppend) {
        if (m_appendRead == nullptr)
            m_appendRead = m_append.begin();
    } else {
        // Keep the read cursor at the same event; append() may have reallocated.
        Event* cursor = m_append.begin();
        for (int i = 0; i < appendIndex; ++i)
            cursor = next_event(cursor);
        m_appendRead = cursor;
    }
}

void EventStream::close(EventBuffer& out) {
    m_open = false;
    out = std::move(m_append);
    m_append = EventBuffer();
    m_nativeRead = nullptr;
    m_appendRead = nullptr;
    m_remainingNative = 0;
    m_remainingAppend = 0;
}

EventBuffer EventStream::release_native_buffer() {
    m_nativeRead = nullptr;
    m_remainingNative = 0;
    return std::move(m_native);
}

void EventStream::cleanup_after_exception() {
    m_open = false;
    m_native = EventBuffer();
    m_append = EventBuffer();
    m_nativeRead = nullptr;
    m_appendRead = nullptr;
    m_remainingNative = 0;
    m_remainingAppend = 0;
}

} // namespace insys::low
