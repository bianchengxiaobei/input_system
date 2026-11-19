#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace insys::backend::linux_evdev {

inline constexpr std::uint32_t kInAttrib = 0x00000004;
inline constexpr std::uint32_t kInMovedFrom = 0x00000040;
inline constexpr std::uint32_t kInMovedTo = 0x00000080;
inline constexpr std::uint32_t kInCreate = 0x00000100;
inline constexpr std::uint32_t kInDelete = 0x00000200;
inline constexpr std::uint32_t kInDeleteSelf = 0x00000400;
inline constexpr std::uint32_t kInMoveSelf = 0x00000800;
inline constexpr std::uint32_t kInQOverflow = 0x00004000;
inline constexpr std::uint32_t kInIgnored = 0x00008000;

struct InotifyEvent {
    std::int32_t watchDescriptor = 0;
    std::uint32_t mask = 0;
    std::uint32_t cookie = 0;
    std::string name;
};

class InotifyEventDecoder {
public:
    static constexpr std::size_t kHeaderSize = 16;
    static constexpr std::uint32_t kMaxNameLength = 4096;

    InotifyEventDecoder() = default;

    std::size_t feed(const std::uint8_t* data, std::size_t size);

    const std::vector<InotifyEvent>& events() const { return m_events; }
    std::size_t buffered_bytes() const { return m_buffer.size(); }
    void clear_events() { m_events.clear(); }
    void reset();

private:
    std::vector<std::uint8_t> m_buffer;
    std::vector<InotifyEvent> m_events;
};

bool is_input_scan_event(const InotifyEvent& event);

} // namespace insys::backend::linux_evdev
