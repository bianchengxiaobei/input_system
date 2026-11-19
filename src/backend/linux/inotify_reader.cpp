#include "insys/backend/inotify_reader.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace insys::backend::linux_evdev {
namespace {

std::uint32_t read_u32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

std::int32_t read_i32(const std::uint8_t* data) {
    return static_cast<std::int32_t>(read_u32(data));
}

} // namespace

std::size_t InotifyEventDecoder::feed(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0)
        return 0;

    const std::size_t previousCount = m_events.size();
    m_buffer.insert(m_buffer.end(), data, data + size);

    std::size_t offset = 0;
    while (m_buffer.size() - offset >= kHeaderSize) {
        const std::uint8_t* header = m_buffer.data() + offset;
        const std::uint32_t nameLength = read_u32(header + 12);
        if (nameLength > kMaxNameLength) {
            m_buffer.clear();
            return m_events.size() - previousCount;
        }

        const std::size_t eventSize = kHeaderSize + nameLength;
        if (m_buffer.size() - offset < eventSize)
            break;

        InotifyEvent event;
        event.watchDescriptor = read_i32(header);
        event.mask = read_u32(header + 4);
        event.cookie = read_u32(header + 8);
        if (nameLength > 0) {
            const char* name = reinterpret_cast<const char*>(header + kHeaderSize);
            const char* end = static_cast<const char*>(
                std::find(name, name + nameLength, '\0'));
            event.name.assign(name, end);
        }
        m_events.push_back(std::move(event));
        offset += eventSize;
    }

    if (offset > 0)
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(offset));

    return m_events.size() - previousCount;
}

void InotifyEventDecoder::reset() {
    m_buffer.clear();
    m_events.clear();
}

bool is_input_scan_event(const InotifyEvent& event) {
    constexpr std::uint32_t kScanMask =
        kInAttrib | kInMovedFrom | kInMovedTo | kInCreate | kInDelete |
        kInDeleteSelf | kInMoveSelf | kInQOverflow | kInIgnored;
    return (event.mask & kScanMask) != 0;
}

} // namespace insys::backend::linux_evdev
