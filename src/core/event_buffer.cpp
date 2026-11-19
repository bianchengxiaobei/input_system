#include "insys/low/event_buffer.h"

#include <algorithm>
#include <cstring>

namespace insys::low {

EventBuffer::EventBuffer(std::vector<uint8_t> data) : m_data(std::move(data)) {}

int EventBuffer::event_count() const {
    int count = 0;
    const uint8_t* cursor = m_data.data();
    const uint8_t* end = m_data.data() + m_data.size();
    while (cursor < end) {
        const auto* event = reinterpret_cast<const Event*>(cursor);
        if (event->size < kHeaderSize)
            break;
        cursor += align_up(event->size, kEventAlignment);
        ++count;
    }
    return count;
}

uint32_t EventBuffer::size_bytes() const {
    return static_cast<uint32_t>(m_data.size());
}

uint32_t EventBuffer::capacity_bytes() const {
    return static_cast<uint32_t>(m_data.capacity());
}

Event* EventBuffer::begin() {
    return reinterpret_cast<Event*>(m_data.data());
}

const Event* EventBuffer::begin() const {
    return reinterpret_cast<const Event*>(m_data.data());
}

Event* EventBuffer::end() {
    return reinterpret_cast<Event*>(m_data.data() + m_data.size());
}

const Event* EventBuffer::end() const {
    return reinterpret_cast<const Event*>(m_data.data() + m_data.size());
}

void EventBuffer::reset() {
    m_data.clear();
}

Event* EventBuffer::allocate(int sizeBytes) {
    if (sizeBytes < static_cast<int>(kHeaderSize))
        return nullptr;

    const uint32_t alignedSize = align_up(static_cast<uint32_t>(sizeBytes), kEventAlignment);
    const std::size_t offset = m_data.size();
    m_data.resize(offset + alignedSize, 0);

    auto* event = reinterpret_cast<Event*>(m_data.data() + offset);
    event->size = static_cast<uint16_t>(alignedSize);
    return event;
}

void EventBuffer::append(const Event* event) {
    if (event == nullptr || event->size < kHeaderSize)
        return;

    Event* destination = allocate(event->size);
    if (destination == nullptr)
        return;

    std::memcpy(destination, event, event->size);
}

bool EventBuffer::contains(const Event* event) const {
    const uint8_t* pointer = reinterpret_cast<const uint8_t*>(event);
    return pointer >= m_data.data() && pointer < m_data.data() + m_data.size();
}

} // namespace insys::low
