#pragma once

#include <cstdint>

namespace insys::detail {

inline bool read_single_bit(const uint8_t* data, uint32_t bitOffset) {
    return ((data[bitOffset >> 3] >> (bitOffset & 7u)) & 1u) != 0;
}

inline void write_single_bit(uint8_t* data, uint32_t bitOffset, bool value) {
    const uint32_t byteIndex = bitOffset >> 3;
    const uint8_t mask = static_cast<uint8_t>(1u << (bitOffset & 7u));
    if (value)
        data[byteIndex] |= mask;
    else
        data[byteIndex] &= static_cast<uint8_t>(~mask);
}

inline uint32_t read_multiple_bits(const uint8_t* data, uint32_t bitOffset, uint32_t count) {
    if (count == 0)
        return 0;
    if (count > 32)
        count = 32;

    uint32_t result = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (read_single_bit(data, bitOffset + i))
            result |= (1u << i);
    }
    return result;
}

inline void write_multiple_bits(uint8_t* data, uint32_t bitOffset, uint32_t count,
                                uint32_t value) {
    if (count > 32)
        count = 32;
    for (uint32_t i = 0; i < count; ++i)
        write_single_bit(data, bitOffset + i, ((value >> i) & 1u) != 0);
}

inline float read_normalized(const uint8_t* data, uint32_t bitOffset, uint32_t count) {
    if (count == 0)
        return 0.0f;
    const uint32_t raw = read_multiple_bits(data, bitOffset, count);
    const uint32_t maxValue = (count >= 32) ? 0xFFFFFFFFu : ((1u << count) - 1u);
    if (maxValue == 0)
        return 0.0f;
    return static_cast<float>(raw) / static_cast<float>(maxValue);
}

inline void write_normalized(uint8_t* data, uint32_t bitOffset, uint32_t count, float value) {
    if (count == 0)
        return;
    if (value < 0.0f)
        value = 0.0f;
    if (value > 1.0f)
        value = 1.0f;
    const uint32_t maxValue = (count >= 32) ? 0xFFFFFFFFu : ((1u << count) - 1u);
    const uint32_t raw = static_cast<uint32_t>(value * static_cast<float>(maxValue) + 0.5f);
    write_multiple_bits(data, bitOffset, count, raw);
}

} // namespace insys::detail
