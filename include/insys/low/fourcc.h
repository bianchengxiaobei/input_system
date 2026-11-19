#pragma once

#include <cstdint>
#include <string>

namespace insys {

struct FourCC {
    uint32_t value = 0;

    constexpr FourCC() = default;
    constexpr explicit FourCC(uint32_t v) : value(v) {}
    constexpr FourCC(char a, char b, char c, char d = ' ')
        : value((uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16) |
                (uint32_t(uint8_t(c)) << 8) | uint32_t(uint8_t(d))) {}

    constexpr bool operator==(const FourCC& o) const { return value == o.value; }
    constexpr bool operator!=(const FourCC& o) const { return value != o.value; }
    constexpr explicit operator bool() const { return value != 0; }
};

std::string to_string(FourCC code);

} // namespace insys
