#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace insys {

struct Guid {
    uint64_t high = 0;
    uint64_t low = 0;

    static Guid create();
    static Guid parse(std::string_view text);

    bool valid() const { return high != 0 || low != 0; }
    std::string to_string() const;
    bool operator==(const Guid& other) const { return high == other.high && low == other.low; }
    bool operator!=(const Guid& other) const { return !(*this == other); }
};

} // namespace insys
