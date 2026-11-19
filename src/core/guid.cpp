#include "insys/guid.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace insys {
namespace {

std::atomic<uint64_t> g_counter{1};

int hex_value(char character) {
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

} // namespace

Guid Guid::create() {
    Guid result;
    result.high = g_counter.fetch_add(1, std::memory_order_relaxed);
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    result.low = static_cast<uint64_t>(now) ^ (result.high * 0x9E3779B97F4A7C15ull);
    if (!result.valid())
        result.low = 1;
    return result;
}

Guid Guid::parse(std::string_view text) {
    uint8_t nibbles[32] = {};
    std::size_t count = 0;
    for (char character : text) {
        if (character == '-')
            continue;
        const int value = hex_value(character);
        if (value < 0 || count >= 32)
            return {};
        nibbles[count++] = static_cast<uint8_t>(value);
    }
    if (count != 32)
        return {};

    Guid result;
    for (std::size_t i = 0; i < 16; ++i)
        result.high = (result.high << 4) | nibbles[i];
    for (std::size_t i = 16; i < 32; ++i)
        result.low = (result.low << 4) | nibbles[i];
    return result;
}

std::string Guid::to_string() const {
    char buffer[33] = {};
    std::snprintf(buffer, sizeof(buffer), "%016llx%016llx",
                  static_cast<unsigned long long>(high),
                  static_cast<unsigned long long>(low));
    return std::string(buffer);
}

} // namespace insys
