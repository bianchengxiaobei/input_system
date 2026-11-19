#pragma once

#include <cstdint>

namespace insys {

enum class UpdateType : uint32_t {
    None = 0,
    Dynamic = 1u << 0,
    Fixed = 1u << 1,
    BeforeRender = 1u << 2,
    Manual = 1u << 3,
    Default = Dynamic | Fixed,
};

using UpdateMask = uint32_t;

inline bool has_update(UpdateMask mask, UpdateType type) {
    return (mask & static_cast<uint32_t>(type)) != 0;
}

} // namespace insys
