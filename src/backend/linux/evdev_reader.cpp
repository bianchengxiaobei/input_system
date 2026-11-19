#include "insys/backend/evdev_reader.h"

#include <cstring>
#include <initializer_list>

namespace insys::backend::evdev {
namespace {

std::uint16_t read_u16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(data[1] << 8);
}

std::int32_t read_i32(const std::uint8_t* data) {
    const std::uint32_t value = static_cast<std::uint32_t>(data[0]) |
                                (static_cast<std::uint32_t>(data[1]) << 8) |
                                (static_cast<std::uint32_t>(data[2]) << 16) |
                                (static_cast<std::uint32_t>(data[3]) << 24);
    return static_cast<std::int32_t>(value);
}

bool any_button(const std::vector<std::uint8_t>& bits,
                std::initializer_list<std::uint16_t> codes) {
    for (const std::uint16_t code : codes) {
        if (evdev_bit_is_set(bits, code))
            return true;
    }
    return false;
}

} // namespace

EvdevEventDecoder::EvdevEventDecoder(std::size_t recordSize, std::size_t typeOffset)
    : m_recordSize(recordSize), m_typeOffset(typeOffset) {
    if (m_typeOffset + 8 > m_recordSize)
        m_recordSize = 0;
}

std::size_t EvdevEventDecoder::feed(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0 || m_recordSize == 0)
        return 0;

    const std::size_t previousCount = m_events.size();
    m_buffer.insert(m_buffer.end(), data, data + size);

    std::size_t offset = 0;
    while (m_buffer.size() - offset >= m_recordSize) {
        const std::uint8_t* record = m_buffer.data() + offset;
        EvdevEvent event;
        event.type = read_u16(record + m_typeOffset);
        event.code = read_u16(record + m_typeOffset + 2);
        event.value = read_i32(record + m_typeOffset + 4);
        m_events.push_back(event);
        offset += m_recordSize;
    }

    if (offset > 0)
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(offset));

    return m_events.size() - previousCount;
}

void EvdevEventDecoder::reset() {
    m_buffer.clear();
    m_events.clear();
}

void encode_evdev_event(std::uint8_t* record, EvdevEvent event,
                        std::size_t recordSize, std::size_t typeOffset) {
    if (record == nullptr || typeOffset + 8 > recordSize)
        return;
    std::memset(record, 0, recordSize);
    record[typeOffset] = static_cast<std::uint8_t>(event.type & 0xff);
    record[typeOffset + 1] = static_cast<std::uint8_t>((event.type >> 8) & 0xff);
    record[typeOffset + 2] = static_cast<std::uint8_t>(event.code & 0xff);
    record[typeOffset + 3] = static_cast<std::uint8_t>((event.code >> 8) & 0xff);
    const auto raw = static_cast<std::uint32_t>(event.value);
    record[typeOffset + 4] = static_cast<std::uint8_t>(raw & 0xff);
    record[typeOffset + 5] = static_cast<std::uint8_t>((raw >> 8) & 0xff);
    record[typeOffset + 6] = static_cast<std::uint8_t>((raw >> 16) & 0xff);
    record[typeOffset + 7] = static_cast<std::uint8_t>((raw >> 24) & 0xff);
}

bool evdev_bit_is_set(const std::vector<std::uint8_t>& bits, std::uint16_t code) {
    const std::size_t byte = code / 8;
    if (byte >= bits.size())
        return false;
    return (bits[byte] & (1u << (code % 8))) != 0;
}

EvdevCapabilities decode_evdev_capabilities(const EvdevCapabilityBits& bits) {
    EvdevCapabilities capabilities;
    capabilities.keys =
        evdev_bit_is_set(bits.types, kEvKey) || !bits.keys.empty();
    capabilities.relative =
        evdev_bit_is_set(bits.types, kEvRel) || !bits.relative.empty();
    capabilities.absolute =
        evdev_bit_is_set(bits.types, kEvAbs) || !bits.absolute.empty();
    capabilities.mouseButtons =
        any_button(bits.keys, {kBtnLeft, kBtnRight, kBtnMiddle, kBtnSide, kBtnExtra});
    capabilities.gamepadButtons = any_button(
        bits.keys, {kBtnSouth, kBtnEast, kBtnNorth, kBtnWest, kBtnTL, kBtnTR,
                    kBtnTL2, kBtnTR2, kBtnSelect, kBtnStart, kBtnThumbL,
                    kBtnThumbR, kBtnDpadUp, kBtnDpadDown, kBtnDpadLeft,
                    kBtnDpadRight});
    return capabilities;
}

EvdevDeviceType classify_evdev_device(const EvdevCapabilities& capabilities) {
    if (capabilities.gamepadButtons)
        return EvdevDeviceType::Gamepad;
    if (capabilities.mouseButtons && capabilities.relative)
        return EvdevDeviceType::Mouse;
    if (capabilities.keys)
        return EvdevDeviceType::Keyboard;
    return EvdevDeviceType::Unknown;
}

} // namespace insys::backend::evdev
