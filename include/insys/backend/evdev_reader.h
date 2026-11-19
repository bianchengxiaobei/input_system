#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "insys/backend/evdev_device.h"

namespace insys::backend::evdev {

struct EvdevEvent {
    uint16_t type = 0;
    uint16_t code = 0;
    int32_t value = 0;
};

class EvdevEventDecoder {
public:
    static constexpr std::size_t kDefaultRecordSize = 24;
    static constexpr std::size_t kDefaultTypeOffset = 16;

    explicit EvdevEventDecoder(std::size_t recordSize = kDefaultRecordSize,
                               std::size_t typeOffset = kDefaultTypeOffset);

    std::size_t feed(const std::uint8_t* data, std::size_t size);

    const std::vector<EvdevEvent>& events() const { return m_events; }
    std::size_t buffered_bytes() const { return m_buffer.size(); }
    void clear_events() { m_events.clear(); }
    void reset();

private:
    std::vector<std::uint8_t> m_buffer;
    std::vector<EvdevEvent> m_events;
    std::size_t m_recordSize = 0;
    std::size_t m_typeOffset = 0;
};

void encode_evdev_event(std::uint8_t* record, EvdevEvent event,
                        std::size_t recordSize = EvdevEventDecoder::kDefaultRecordSize,
                        std::size_t typeOffset = EvdevEventDecoder::kDefaultTypeOffset);

struct EvdevCapabilities {
    bool keys = false;
    bool relative = false;
    bool absolute = false;
    bool mouseButtons = false;
    bool gamepadButtons = false;
};

struct EvdevCapabilityBits {
    std::vector<std::uint8_t> types;
    std::vector<std::uint8_t> keys;
    std::vector<std::uint8_t> relative;
    std::vector<std::uint8_t> absolute;
};

bool evdev_bit_is_set(const std::vector<std::uint8_t>& bits, std::uint16_t code);
EvdevCapabilities decode_evdev_capabilities(const EvdevCapabilityBits& bits);
EvdevDeviceType classify_evdev_device(const EvdevCapabilities& capabilities);

} // namespace insys::backend::evdev
