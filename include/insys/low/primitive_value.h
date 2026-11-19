#pragma once

#include <cstdint>
#include <string>

#include "insys/types.h"

namespace insys {

struct PrimitiveValue {
    enum class Type { None, Bool, Int, Float, Vector2, String };

    Type type = Type::None;
    bool boolValue = false;
    int32_t intValue = 0;
    float floatValue = 0.0f;
    Vector2 vector2Value{};
    std::string stringValue;

    static PrimitiveValue from_bool(bool value);
    static PrimitiveValue from_int(int32_t value);
    static PrimitiveValue from_float(float value);
    static PrimitiveValue from_vector2(Vector2 value);
    static PrimitiveValue from_string(std::string value);

    bool as_bool(bool fallback = false) const;
    int32_t as_int(int32_t fallback = 0) const;
    float as_float(float fallback = 0.0f) const;
};

} // namespace insys
