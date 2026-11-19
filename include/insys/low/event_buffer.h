#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "insys/low/event.h"

namespace insys::low {

class EventBuffer {
public:
    EventBuffer() = default;
    explicit EventBuffer(std::vector<uint8_t> data);

    int event_count() const;
    uint32_t size_bytes() const;
    uint32_t capacity_bytes() const;
    bool empty() const { return m_data.empty(); }

    uint8_t* data() { return m_data.data(); }
    const uint8_t* data() const { return m_data.data(); }

    Event* begin();
    const Event* begin() const;
    Event* end();
    const Event* end() const;

    void reset();

    // Allocates an event of `sizeBytes` (including header), zero-initialized,
    // aligned to kEventAlignment. Returns nullptr if sizeBytes is too small.
    Event* allocate(int sizeBytes);

    void append(const Event* event);

    bool contains(const Event* event) const;

    std::vector<uint8_t> release_data() { return std::move(m_data); }

private:
    std::vector<uint8_t> m_data;
};

} // namespace insys::low
