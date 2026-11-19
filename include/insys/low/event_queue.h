#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "insys/low/event_buffer.h"

namespace insys::low {

class EventQueue {
public:
    explicit EventQueue(std::size_t capacityBytes = 1u << 20);

    void push(const Event* event);
    void push_front(const EventBuffer& buffer);

    EventBuffer drain();
    void drain_into(std::vector<uint8_t>& out);
    void reclaim(EventBuffer&& buffer);
    void reclaim(std::vector<uint8_t>&& data);
    void clear();

    std::size_t size_bytes() const;
    bool empty() const;

private:
    mutable std::mutex m_mutex;
    std::vector<uint8_t> m_pending;
    std::vector<uint8_t> m_recycle;
    std::size_t m_capacityBytes;
};

} // namespace insys::low
