#include "insys/low/event_queue.h"

#include <cstring>

namespace insys::low {

EventQueue::EventQueue(std::size_t capacityBytes) : m_capacityBytes(capacityBytes) {
    m_pending.reserve(capacityBytes);
    m_recycle.reserve(capacityBytes);
}

void EventQueue::push(const Event* event) {
    if (event == nullptr || event->size < kHeaderSize)
        return;

    const uint32_t alignedSize = align_up(event->size, kEventAlignment);

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pending.size() + alignedSize > m_capacityBytes) {
        // M0 keeps overflow handling simple: drop everything accumulated so far.
        m_pending.clear();
    }

    const std::size_t offset = m_pending.size();
    m_pending.resize(offset + alignedSize, 0);
    std::memcpy(m_pending.data() + offset, event, event->size);
}

void EventQueue::push_front(const EventBuffer& buffer) {
    if (buffer.empty())
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<uint8_t> combined;
    combined.reserve(buffer.size_bytes() + m_pending.size());
    combined.insert(combined.end(), buffer.data(), buffer.data() + buffer.size_bytes());
    combined.insert(combined.end(), m_pending.begin(), m_pending.end());
    m_pending = std::move(combined);
}

EventBuffer EventQueue::drain() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_recycle.capacity() == 0)
        m_recycle.reserve(m_capacityBytes);
    m_pending.swap(m_recycle);
    return EventBuffer(std::move(m_recycle));
}

void EventQueue::drain_into(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_recycle.capacity() == 0)
        m_recycle.reserve(m_capacityBytes);
    out.clear();
    m_pending.swap(m_recycle);
    out.swap(m_recycle);
}

void EventQueue::reclaim(EventBuffer&& buffer) {
    if (buffer.capacity_bytes() == 0)
        return;
    std::vector<uint8_t> data = buffer.release_data();
    data.clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (data.capacity() > m_recycle.capacity())
        m_recycle = std::move(data);
}

void EventQueue::reclaim(std::vector<uint8_t>&& data) {
    data.clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (data.capacity() > m_recycle.capacity())
        m_recycle = std::move(data);
}

void EventQueue::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending.clear();
}

std::size_t EventQueue::size_bytes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pending.size();
}

bool EventQueue::empty() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pending.empty();
}

} // namespace insys::low
