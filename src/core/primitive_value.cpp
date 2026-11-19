#include "insys/low/primitive_value.h"

namespace insys {

PrimitiveValue PrimitiveValue::from_bool(bool value) {
    PrimitiveValue result;
    result.type = Type::Bool;
    result.boolValue = value;
    return result;
}

PrimitiveValue PrimitiveValue::from_int(int32_t value) {
    PrimitiveValue result;
    result.type = Type::Int;
    result.intValue = value;
    return result;
}

PrimitiveValue PrimitiveValue::from_float(float value) {
    PrimitiveValue result;
    result.type = Type::Float;
    result.floatValue = value;
    return result;
}

PrimitiveValue PrimitiveValue::from_vector2(Vector2 value) {
    PrimitiveValue result;
    result.type = Type::Vector2;
    result.vector2Value = value;
    return result;
}

PrimitiveValue PrimitiveValue::from_string(std::string value) {
    PrimitiveValue result;
    result.type = Type::String;
    result.stringValue = std::move(value);
    return result;
}

bool PrimitiveValue::as_bool(bool fallback) const {
    switch (type) {
    case Type::Bool: return boolValue;
    case Type::Int: return intValue != 0;
    case Type::Float: return floatValue != 0.0f;
    default: return fallback;
    }
}

int32_t PrimitiveValue::as_int(int32_t fallback) const {
    switch (type) {
    case Type::Int: return intValue;
    case Type::Bool: return boolValue ? 1 : 0;
    case Type::Float: return static_cast<int32_t>(floatValue);
    default: return fallback;
    }
}

float PrimitiveValue::as_float(float fallback) const {
    switch (type) {
    case Type::Float: return floatValue;
    case Type::Int: return static_cast<float>(intValue);
    case Type::Bool: return boolValue ? 1.0f : 0.0f;
    default: return fallback;
    }
}

} // namespace insys
