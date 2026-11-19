#include "insys/control.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "bit_math.h"
#include "insys/device.h"
#include "insys/low/state_format.h"

namespace insys {

float Control::evaluate_magnitude(const uint8_t* state) const {
    return std::fabs(read_float(state));
}

float Control::read_float(const uint8_t* state) const {
    float value = 0.0f;
    read_value_from_state(state, &value);
    return value;
}

int32_t Control::read_integer(const uint8_t* state) const {
    return static_cast<int32_t>(read_float(state));
}

double Control::read_double(const uint8_t* state) const {
    return static_cast<double>(read_float(state));
}

Vector2 Control::read_vector2(const uint8_t* state) const {
    Vector2 value{};
    read_value_from_state(state, &value);
    return value;
}

Vector3 Control::read_vector3(const uint8_t* state) const {
    Vector3 value{};
    read_value_from_state(state, &value);
    return value;
}

Quaternion Control::read_quaternion(const uint8_t* state) const {
    Quaternion value{};
    read_value_from_state(state, &value);
    return value;
}

bool Control::read_bool(const uint8_t* state, float threshold) const {
    return read_float(state) >= threshold;
}

std::string Control::path() const {
    std::vector<const Control*> chain;
    chain.push_back(this);
    const Control* cursor = parent;
    while (cursor != nullptr && cursor->parent != nullptr) {
        chain.push_back(cursor);
        cursor = cursor->parent;
    }

    std::string result;
    if (device != nullptr)
        result = device->name.c_str();
    else if (cursor != nullptr)
        result = cursor->name.c_str();

    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (!result.empty())
            result += '/';
        result += (*it)->name.c_str();
    }
    return result;
}

void AxisControl::read_value_from_state(const uint8_t* state, void* out) const {
    float value = insys::read_float(stateBlock, state);
    if (normalize) {
        if (value >= normalizeZero) {
            value = normalizeMax > normalizeZero
                        ? (value - normalizeZero) / (normalizeMax - normalizeZero)
                        : 0.0f;
        } else {
            value = normalizeZero > normalizeMin
                        ? (value - normalizeZero) / (normalizeZero - normalizeMin)
                        : 0.0f;
        }
        value = std::clamp(value, -1.0f, 1.0f);
    }
    if (invert)
        value = -value;
    std::memcpy(out, &value, sizeof(float));
}

void AxisControl::write_value_to_state(uint8_t* state, const void* in) const {
    float value = 0.0f;
    std::memcpy(&value, in, sizeof(float));
    insys::write_float(stateBlock, state, value);
}

void IntegerControl::read_value_from_state(const uint8_t* state, void* out) const {
    const int32_t value = insys::read_int(stateBlock, state);
    std::memcpy(out, &value, sizeof(int32_t));
}

void IntegerControl::write_value_to_state(uint8_t* state, const void* in) const {
    int32_t value = 0;
    std::memcpy(&value, in, sizeof(int32_t));
    insys::write_int(stateBlock, state, value);
}

int32_t IntegerControl::read_integer(const uint8_t* state) const {
    return insys::read_int(stateBlock, state);
}

double DoubleControl::read_double(const uint8_t* state) const {
    return insys::read_double(stateBlock, state);
}

void DoubleControl::read_value_from_state(const uint8_t* state, void* out) const {
    const double value = insys::read_double(stateBlock, state);
    std::memcpy(out, &value, sizeof(double));
}

void DoubleControl::write_value_to_state(uint8_t* state, const void* in) const {
    double value = 0.0;
    std::memcpy(&value, in, sizeof(double));
    insys::write_double(stateBlock, state, value);
}

void ButtonControl::read_value_from_state(const uint8_t* state, void* out) const {
    const float value = insys::read_float(stateBlock, state);
    std::memcpy(out, &value, sizeof(float));
}

void ButtonControl::write_value_to_state(uint8_t* state, const void* in) const {
    float value = 0.0f;
    std::memcpy(&value, in, sizeof(float));
    insys::write_float(stateBlock, state, value);
}

void Vector2Control::read_value_from_state(const uint8_t* state, void* out) const {
    const Vector2 value = insys::read_vector2(stateBlock, state);
    std::memcpy(out, &value, sizeof(Vector2));
}

void Vector2Control::write_value_to_state(uint8_t* state, const void* in) const {
    Vector2 value{};
    std::memcpy(&value, in, sizeof(Vector2));
    insys::write_vector2(stateBlock, state, value);
}

float Vector2Control::evaluate_magnitude(const uint8_t* state) const {
    return insys::read_vector2(stateBlock, state).length();
}

void Vector3Control::read_value_from_state(const uint8_t* state, void* out) const {
    if (stateBlock.format != kFormatVector3) {
        std::memset(out, 0, sizeof(Vector3));
        return;
    }
    std::memcpy(out, state + stateBlock.effective_byte_offset(), sizeof(Vector3));
}

void Vector3Control::write_value_to_state(uint8_t* state, const void* in) const {
    if (stateBlock.format != kFormatVector3)
        return;
    std::memcpy(state + stateBlock.effective_byte_offset(), in, sizeof(Vector3));
}

float Vector3Control::evaluate_magnitude(const uint8_t* state) const {
    return read_vector3(state).length();
}

void QuaternionControl::read_value_from_state(const uint8_t* state, void* out) const {
    if (stateBlock.format != kFormatQuaternion) {
        std::memset(out, 0, sizeof(Quaternion));
        return;
    }
    std::memcpy(out, state + stateBlock.effective_byte_offset(), sizeof(Quaternion));
}

void QuaternionControl::write_value_to_state(uint8_t* state, const void* in) const {
    if (stateBlock.format != kFormatQuaternion)
        return;
    std::memcpy(state + stateBlock.effective_byte_offset(), in, sizeof(Quaternion));
}

void KeyControl::read_value_from_state(const uint8_t* state, void* out) const {
    const float value = insys::read_bit(stateBlock, state) ? 1.0f : 0.0f;
    std::memcpy(out, &value, sizeof(float));
}

void KeyControl::write_value_to_state(uint8_t* state, const void* in) const {
    float value = 0.0f;
    std::memcpy(&value, in, sizeof(float));
    insys::write_bit(stateBlock, state, value >= 0.5f);
}

void BitButtonControl::read_value_from_state(const uint8_t* state, void* out) const {
    const float value = insys::read_bit(stateBlock, state) ? 1.0f : 0.0f;
    std::memcpy(out, &value, sizeof(float));
}

void BitButtonControl::write_value_to_state(uint8_t* state, const void* in) const {
    float value = 0.0f;
    std::memcpy(&value, in, sizeof(float));
    insys::write_bit(stateBlock, state, value >= 0.5f);
}

void DiscreteButtonControl::read_value_from_state(const uint8_t* state, void* out) const {
    int32_t raw = 0;
    if (stateBlock.format == kFormatBit && stateBlock.sizeInBits > 1) {
        raw = static_cast<int32_t>(detail::read_multiple_bits(
            state, stateBlock.effective_bit_offset(), stateBlock.sizeInBits));
    } else {
        raw = insys::read_int(stateBlock, state);
    }

    bool pressed = false;
    if (minValue > maxValue) {
        const int32_t wrap = wrapAtValue == nullValue ? minValue : wrapAtValue;
        pressed = (raw >= minValue && raw <= wrap) ||
                  (raw != nullValue && raw <= maxValue);
    } else {
        pressed = raw >= minValue && raw <= maxValue;
    }

    const float value = pressed ? 1.0f : 0.0f;
    std::memcpy(out, &value, sizeof(float));
}

void DiscreteButtonControl::write_value_to_state(uint8_t* state, const void* in) const {
    if (!writeNullAndMaxValue)
        return;

    float value = 0.0f;
    std::memcpy(&value, in, sizeof(float));
    const int32_t raw = value >= pressPoint ? maxValue : nullValue;
    if (stateBlock.format == kFormatBit && stateBlock.sizeInBits > 1) {
        detail::write_multiple_bits(state, stateBlock.effective_bit_offset(),
                                    stateBlock.sizeInBits,
                                    static_cast<uint32_t>(raw));
    } else {
        insys::write_int(stateBlock, state, raw);
    }
}

void AnyKeyControl::read_value_from_state(const uint8_t* state, void* out) const {
    bool pressed = false;
    for (std::size_t i = 0; i < kKeyCount; ++i) {
        if (detail::read_single_bit(state, static_cast<uint32_t>(i))) {
            pressed = true;
            break;
        }
    }
    const float value = pressed ? 1.0f : 0.0f;
    std::memcpy(out, &value, sizeof(float));
}

void AnyKeyControl::write_value_to_state(uint8_t* state, const void* in) const {
    // Synthetic control: derived from the key bits, never written directly.
    (void)state;
    (void)in;
}

void DpadControl::read_value_from_state(const uint8_t* state, void* out) const {
    if (!children.empty()) {
        bool up = false;
        bool down = false;
        bool left = false;
        bool right = false;
        for (const Control* child : children) {
            const std::string childName = child->name.str();
            if (childName == "up")
                up = child->read_float(state) > 0.5f;
            else if (childName == "down")
                down = child->read_float(state) > 0.5f;
            else if (childName == "left")
                left = child->read_float(state) > 0.5f;
            else if (childName == "right")
                right = child->read_float(state) > 0.5f;
        }
        const Vector2 value{(right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f),
                            (up ? 1.0f : 0.0f) - (down ? 1.0f : 0.0f)};
        std::memcpy(out, &value, sizeof(Vector2));
        return;
    }

    const uint32_t base = stateBlock.effective_bit_offset();
    const bool up = detail::read_single_bit(state, base + 0);
    const bool down = detail::read_single_bit(state, base + 1);
    const bool left = detail::read_single_bit(state, base + 2);
    const bool right = detail::read_single_bit(state, base + 3);

    const Vector2 value{(right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f),
                        (up ? 1.0f : 0.0f) - (down ? 1.0f : 0.0f)};
    std::memcpy(out, &value, sizeof(Vector2));
}

void DpadControl::write_value_to_state(uint8_t* state, const void* in) const {
    Vector2 value{};
    std::memcpy(&value, in, sizeof(Vector2));

    if (!children.empty()) {
        const float pressed = 1.0f;
        const float released = 0.0f;
        for (Control* child : children) {
            const std::string childName = child->name.str();
            const float* childValue = nullptr;
            if (childName == "up")
                childValue = value.y > 0.5f ? &pressed : &released;
            else if (childName == "down")
                childValue = value.y < -0.5f ? &pressed : &released;
            else if (childName == "left")
                childValue = value.x < -0.5f ? &pressed : &released;
            else if (childName == "right")
                childValue = value.x > 0.5f ? &pressed : &released;
            if (childValue != nullptr)
                child->write_value_to_state(state, childValue);
        }
        return;
    }

    const uint32_t base = stateBlock.effective_bit_offset();
    detail::write_single_bit(state, base + 0, value.y > 0.5f);
    detail::write_single_bit(state, base + 1, value.y < -0.5f);
    detail::write_single_bit(state, base + 2, value.x < -0.5f);
    detail::write_single_bit(state, base + 3, value.x > 0.5f);
}

float DpadControl::evaluate_magnitude(const uint8_t* state) const {
    Vector2 value{};
    read_value_from_state(state, &value);
    return value.length();
}

} // namespace insys






