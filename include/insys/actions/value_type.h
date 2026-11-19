#pragma once

#include <cstddef>

namespace insys {

enum class ValueType {
    None,
    Float,
    Vector2,
    Vector3,
    Quaternion,
    Int,
    Double,
};

inline std::size_t value_type_size(ValueType type) {
    switch (type) {
    case ValueType::Float: return sizeof(float);
    case ValueType::Vector2: return 2 * sizeof(float);
    case ValueType::Vector3: return 3 * sizeof(float);
    case ValueType::Quaternion: return 4 * sizeof(float);
    case ValueType::Int: return sizeof(int32_t);
    case ValueType::Double: return sizeof(double);
    case ValueType::None:
    default: return 0;
    }
}

} // namespace insys
