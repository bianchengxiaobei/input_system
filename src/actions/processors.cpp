#include "insys/actions/processor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "insys/backend/iruntime.h"
#include "insys/control.h"
#include "insys/settings.h"
#include "insys/string_utils.h"

namespace insys {
namespace {

constexpr float kDefaultDeadzoneMin = 0.125f;
constexpr float kDefaultDeadzoneMax = 0.925f;

class InvertProcessor final : public IProcessor {
public:
    ValueType value_type() const override { return ValueType::Float; }
    void process(void* value, std::size_t size, const Control*) const override {
        if (size < sizeof(float))
            return;
        auto* number = static_cast<float*>(value);
        *number = -*number;
    }
};

class InvertVector2Processor final : public IProcessor {
public:
    bool invertX = true;
    bool invertY = true;

    ValueType value_type() const override { return ValueType::Vector2; }
    void process(void* value, std::size_t size, const Control*) const override {
        if (size < sizeof(Vector2))
            return;
        auto* vector = static_cast<Vector2*>(value);
        if (invertX)
            vector->x = -vector->x;
        if (invertY)
            vector->y = -vector->y;
    }
    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (equals_ignore_case(name, "invertX")) {
            invertX = value.as_bool(invertX);
            return true;
        }
        if (equals_ignore_case(name, "invertY")) {
            invertY = value.as_bool(invertY);
            return true;
        }
        return false;
    }
};

class InvertVector3Processor final : public IProcessor {
public:
    bool invertX = true;
    bool invertY = true;
    bool invertZ = true;

    ValueType value_type() const override { return ValueType::Vector3; }
    void process(void* value, std::size_t size, const Control*) const override {
        if (size < sizeof(Vector3))
            return;
        auto* vector = static_cast<Vector3*>(value);
        if (invertX)
            vector->x = -vector->x;
        if (invertY)
            vector->y = -vector->y;
        if (invertZ)
            vector->z = -vector->z;
    }
    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (equals_ignore_case(name, "invertX")) {
            invertX = value.as_bool(invertX);
            return true;
        }
        if (equals_ignore_case(name, "invertY")) {
            invertY = value.as_bool(invertY);
            return true;
        }
        if (equals_ignore_case(name, "invertZ")) {
            invertZ = value.as_bool(invertZ);
            return true;
        }
        return false;
    }
};

class ScaleProcessor final : public IProcessor {
public:
    float factor = 1.0f;

    ValueType value_type() const override { return ValueType::Float; }
    void process(void* value, std::size_t size, const Control*) const override {
        if (size < sizeof(float))
            return;
        *static_cast<float*>(value) *= factor;
    }
    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (equals_ignore_case(name, "factor")) {
            factor = value.as_float(factor);
            return true;
        }
        return false;
    }
};

class ScaleVector2Processor final : public IProcessor {
public:
    float x = 1.0f;
    float y = 1.0f;

    ValueType value_type() const override { return ValueType::Vector2; }
    void process(void* value, std::size_t size, const Control*) const override {
        if (size < sizeof(Vector2))
            return;
        auto* vector = static_cast<Vector2*>(value);
        vector->x *= x;
        vector->y *= y;
    }
    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (equals_ignore_case(name, "x")) {
            x = value.as_float(x);
            return true;
        }
        if (equals_ignore_case(name, "y")) {
            y = value.as_float(y);
            return true;
        }
        return false;
    }
};

class ScaleVector3Processor final : public IProcessor {
public:
    float x = 1.0f;
    float y = 1.0f;
    float z = 1.0f;

    ValueType value_type() const override { return ValueType::Vector3; }
    void process(void* value, std::size_t size, const Control*) const override {
        if (size < sizeof(Vector3))
            return;
        auto* vector = static_cast<Vector3*>(value);
        vector->x *= x;
        vector->y *= y;
        vector->z *= z;
    }
    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (equals_ignore_case(name, "x")) {
            x = value.as_float(x);
            return true;
        }
        if (equals_ignore_case(name, "y")) {
            y = value.as_float(y);
            return true;
        }
        if (equals_ignore_case(name, "z")) {
            z = value.as_float(z);
            return true;
        }
        return false;
    }
};

float normalize_value(float value, float min, float max, float zero) {
    if (zero < min)
        zero = min;
    if (max == min)
        return 0.0f;
    if (std::fabs(value - min) < 1e-6f)
        return min < zero ? -1.0f : 0.0f;
    const float percentage = (value - min) / (max - min);
    return min < zero ? 2.0f * percentage - 1.0f : percentage;
}

class NormalizeProcessor final : public IProcessor {
public:
    float min = 0.0f;
    float max = 0.0f;
    float zero = 0.0f;

    ValueType value_type() const override { return ValueType::Float; }
    void process(void* value, std::size_t size, const Control*) const override {
        if (size < sizeof(float))
            return;
        auto* number = static_cast<float*>(value);
        *number = normalize_value(*number, min, max, zero);
    }
    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (equals_ignore_case(name, "min")) {
            min = value.as_float(min);
            return true;
        }
        if (equals_ignore_case(name, "max")) {
            max = value.as_float(max);
            return true;
        }
        if (equals_ignore_case(name, "zero")) {
            zero = value.as_float(zero);
            return true;
        }
        return false;
    }
};

class NormalizeVector2Processor final : public IProcessor {
public:
    ValueType value_type() const override { return ValueType::Vector2; }
    void process(void* value, std::size_t size, const Control*) const override {
        if (size < sizeof(Vector2))
            return;
        auto* vector = static_cast<Vector2*>(value);
        const float length = vector->length();
        if (length > 1e-6f)
            *vector = *vector / length;
        else
            *vector = {};
    }
};

class NormalizeVector3Processor final : public IProcessor {
public:
    ValueType value_type() const override { return ValueType::Vector3; }
    void process(void* value, std::size_t size, const Control*) const override {
        if (size < sizeof(Vector3))
            return;
        auto* vector = static_cast<Vector3*>(value);
        const float length = vector->length();
        if (length > 1e-6f)
            *vector = *vector * (1.0f / length);
        else
            *vector = {};
    }
};

class ClampProcessor final : public IProcessor {
public:
    float min = 0.0f;
    float max = 0.0f;

    ValueType value_type() const override { return ValueType::Float; }
    void process(void* value, std::size_t size, const Control*) const override {
        if (size < sizeof(float))
            return;
        auto* number = static_cast<float*>(value);
        *number = std::clamp(*number, min, max);
    }
    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (equals_ignore_case(name, "min")) {
            min = value.as_float(min);
            return true;
        }
        if (equals_ignore_case(name, "max")) {
            max = value.as_float(max);
            return true;
        }
        return false;
    }
};

float deadzone_adjusted(float value, float min, float max) {
    const float absolute = std::fabs(value);
    if (absolute < min)
        return 0.0f;
    if (absolute > max)
        return std::copysign(1.0f, value);
    return std::copysign((absolute - min) / (max - min), value);
}

bool parse_float(std::string_view text, float& out) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    if (text.empty())
        return false;

    std::string buffer(text);
    char* end = nullptr;
    const float value = std::strtof(buffer.c_str(), &end);
    if (end == buffer.c_str() || *end != '\0' || !std::isfinite(value))
        return false;
    out = value;
    return true;
}

bool parse_curve(std::string_view text, std::vector<Vector2>& out) {
    std::vector<Vector2> points;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(';', start);
        const std::string_view item =
            text.substr(start, end == std::string_view::npos ? std::string_view::npos
                                                              : end - start);
        std::size_t separator = item.find(',');
        const std::size_t colon = item.find(':');
        if (separator == std::string_view::npos ||
            (colon != std::string_view::npos && colon < separator))
            separator = colon;
        if (separator == std::string_view::npos)
            return false;

        float x = 0.0f;
        float y = 0.0f;
        if (!parse_float(item.substr(0, separator), x) ||
            !parse_float(item.substr(separator + 1), y))
            return false;
        if (x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f)
            return false;
        points.push_back({x, y});

        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }

    if (points.size() < 2)
        return false;
    std::stable_sort(points.begin(), points.end(),
                     [](const Vector2& left, const Vector2& right) {
                         return left.x < right.x;
                     });

    std::vector<Vector2> unique;
    unique.reserve(points.size());
    for (const Vector2& point : points) {
        if (!unique.empty() && std::fabs(point.x - unique.back().x) < 1e-6f)
            unique.back() = point;
        else
            unique.push_back(point);
    }
    if (unique.size() < 2)
        return false;
    out = std::move(unique);
    return true;
}

float apply_curve(const std::vector<Vector2>& curve, float value) {
    if (curve.empty())
        return value;
    value = std::clamp(value, 0.0f, 1.0f);
    if (value <= curve.front().x)
        return curve.front().y;
    if (value >= curve.back().x)
        return curve.back().y;
    for (std::size_t i = 1; i < curve.size(); ++i) {
        if (value > curve[i].x)
            continue;
        const Vector2& left = curve[i - 1];
        const Vector2& right = curve[i];
        const float width = right.x - left.x;
        if (width <= 1e-6f)
            return right.y;
        const float t = (value - left.x) / width;
        return left.y + (right.y - left.y) * t;
    }
    return curve.back().y;
}

class AxisDeadzoneProcessor final : public IProcessor {
public:
    float min = 0.0f;
    float max = 0.0f;
    std::vector<Vector2> curve;

    float min_value() const { return min == 0.0f ? kDefaultDeadzoneMin : min; }
    float max_value() const { return max == 0.0f ? kDefaultDeadzoneMax : max; }

    ValueType value_type() const override { return ValueType::Float; }
    void process(void* value, std::size_t size, const Control*) const override {
        if (size < sizeof(float))
            return;
        auto* number = static_cast<float*>(value);
        const float adjusted = deadzone_adjusted(*number, min_value(), max_value());
        *number = std::copysign(apply_curve(curve, std::fabs(adjusted)), adjusted);
    }
    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (equals_ignore_case(name, "min")) {
            min = value.as_float(min);
            return true;
        }
        if (equals_ignore_case(name, "max")) {
            max = value.as_float(max);
            return true;
        }
        if (equals_ignore_case(name, "curve") || equals_ignore_case(name, "points")) {
            curve.clear();
            if (value.type == PrimitiveValue::Type::String)
                parse_curve(value.stringValue, curve);
            return true;
        }
        return false;
    }
};

class StickDeadzoneProcessor final : public IProcessor {
public:
    float min = 0.0f;
    float max = 0.0f;
    std::vector<Vector2> curve;

    float min_value() const { return min == 0.0f ? kDefaultDeadzoneMin : min; }
    float max_value() const { return max == 0.0f ? kDefaultDeadzoneMax : max; }

    ValueType value_type() const override { return ValueType::Vector2; }
    void process(void* value, std::size_t size, const Control*) const override {
        if (size < sizeof(Vector2))
            return;
        auto* vector = static_cast<Vector2*>(value);
        const float magnitude = vector->length();
        if (magnitude <= 0.0f)
            return;
        const float adjusted = deadzone_adjusted(magnitude, min_value(), max_value());
        if (adjusted == 0.0f) {
            *vector = {};
            return;
        }
        const float curved = apply_curve(curve, adjusted);
        if (curved == 0.0f)
            *vector = {};
        else
            *vector = *vector * (curved / magnitude);
    }
    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (equals_ignore_case(name, "min")) {
            min = value.as_float(min);
            return true;
        }
        if (equals_ignore_case(name, "max")) {
            max = value.as_float(max);
            return true;
        }
        if (equals_ignore_case(name, "curve") || equals_ignore_case(name, "points")) {
            curve.clear();
            if (value.type == PrimitiveValue::Type::String)
                parse_curve(value.stringValue, curve);
            return true;
        }
        return false;
    }
};

bool compensate_for_screen_orientation(const ProcessorContext& context) {
    return context.settings == nullptr || context.settings->compensateForScreenOrientation;
}

int screen_orientation(const ProcessorContext& context) {
    return context.runtime != nullptr ? context.runtime->screen_orientation()
                                     : static_cast<int>(ScreenOrientation::Unknown);
}

void compensate_direction(Vector3* direction, const ProcessorContext& context) {
    if (!compensate_for_screen_orientation(context))
        return;

    constexpr float kPi = 3.14159265358979323846f;
    Quaternion rotation;
    switch (static_cast<ScreenOrientation>(screen_orientation(context))) {
    case ScreenOrientation::PortraitUpsideDown:
        rotation = Quaternion::from_axis_angle({0.0f, 0.0f, 1.0f}, kPi);
        break;
    case ScreenOrientation::LandscapeLeft:
        rotation = Quaternion::from_axis_angle({0.0f, 0.0f, 1.0f}, kPi * 0.5f);
        break;
    case ScreenOrientation::LandscapeRight:
        rotation = Quaternion::from_axis_angle({0.0f, 0.0f, 1.0f}, kPi * 1.5f);
        break;
    default:
        return;
    }

    *direction = rotation.rotate(*direction);
}

void compensate_rotation(Quaternion* rotation, const ProcessorContext& context) {
    if (!compensate_for_screen_orientation(context))
        return;

    constexpr float kSqrtOfTwo = 1.4142135623730950488f;
    Quaternion compensation;
    switch (static_cast<ScreenOrientation>(screen_orientation(context))) {
    case ScreenOrientation::PortraitUpsideDown:
        compensation = {0.0f, 0.0f, 1.0f, 0.0f};
        break;
    case ScreenOrientation::LandscapeLeft:
        compensation = {0.0f, 0.0f, kSqrtOfTwo * 0.5f, -kSqrtOfTwo * 0.5f};
        break;
    case ScreenOrientation::LandscapeRight:
        compensation = {0.0f, 0.0f, -kSqrtOfTwo * 0.5f, -kSqrtOfTwo * 0.5f};
        break;
    default:
        return;
    }

    *rotation = *rotation * compensation;
}

class CompensateDirectionProcessor final : public IProcessor {
public:
    ValueType value_type() const override { return ValueType::Vector3; }

    void process(void* value, std::size_t size, const Control* control) const override {
        process(value, size, control, ProcessorContext{});
    }

    void process(void* value, std::size_t size, const Control*,
                 const ProcessorContext& context) const override {
        if (size < sizeof(Vector3))
            return;
        compensate_direction(static_cast<Vector3*>(value), context);
    }

    CachingPolicy caching_policy() const override {
        return CachingPolicy::EvaluateOnEveryRead;
    }
};

class CompensateRotationProcessor final : public IProcessor {
public:
    ValueType value_type() const override { return ValueType::Quaternion; }

    void process(void* value, std::size_t size, const Control* control) const override {
        process(value, size, control, ProcessorContext{});
    }

    void process(void* value, std::size_t size, const Control*,
                 const ProcessorContext& context) const override {
        if (size < sizeof(Quaternion))
            return;
        compensate_rotation(static_cast<Quaternion*>(value), context);
    }

    CachingPolicy caching_policy() const override {
        return CachingPolicy::EvaluateOnEveryRead;
    }
};

class AndroidCompensateDirectionProcessor final : public IProcessor {
public:
    ValueType value_type() const override { return ValueType::Vector3; }

    void process(void* value, std::size_t size, const Control* control) const override {
        process(value, size, control, ProcessorContext{});
    }

    void process(void* value, std::size_t size, const Control*,
                 const ProcessorContext& context) const override {
        if (size < sizeof(Vector3))
            return;
        auto* direction = static_cast<Vector3*>(value);
        constexpr float kSensorStandardGravity = 9.80665f;
        *direction = *direction * (-1.0f / kSensorStandardGravity);
        compensate_direction(direction, context);
    }

    CachingPolicy caching_policy() const override {
        return CachingPolicy::EvaluateOnEveryRead;
    }
};

class AndroidCompensateRotationProcessor final : public IProcessor {
public:
    ValueType value_type() const override { return ValueType::Quaternion; }

    void process(void* value, std::size_t size, const Control* control) const override {
        process(value, size, control, ProcessorContext{});
    }

    void process(void* value, std::size_t size, const Control*,
                 const ProcessorContext& context) const override {
        if (size < sizeof(Quaternion))
            return;
        auto* rotation = static_cast<Quaternion*>(value);
        const float sinRho2 = rotation->x * rotation->x +
                             rotation->y * rotation->y +
                             rotation->z * rotation->z;
        rotation->w = sinRho2 < 1.0f ? std::sqrt(1.0f - sinRho2) : 0.0f;
        compensate_rotation(rotation, context);
    }

    CachingPolicy caching_policy() const override {
        return CachingPolicy::EvaluateOnEveryRead;
    }
};

float smoothing_alpha(float halfLife, double deltaTime) {
    if (halfLife <= 0.0f || deltaTime <= 0.0)
        return 1.0f;
    constexpr float kLn2 = 0.6931471805599453f;
    return 1.0f - std::exp(-static_cast<float>(deltaTime) * kLn2 / halfLife);
}

class SmoothProcessor final : public IProcessor {
public:
    ValueType value_type() const override { return ValueType::Float; }

    void process(void* value, std::size_t size, const Control*) const override {
        if (size < sizeof(float))
            return;
        m_value = *static_cast<float*>(value);
        m_initialized = false;
    }

    void process(void* value, std::size_t size, const Control*,
                 const ProcessorContext& context) const override {
        if (size < sizeof(float))
            return;
        const float input = *static_cast<float*>(value);
        if (context.runtime == nullptr || halfLife <= 0.0f) {
            m_initialized = false;
            *static_cast<float*>(value) = input;
            return;
        }

        const double now = context.runtime->now();
        if (!m_initialized) {
            m_value = input;
            m_time = now;
            m_initialized = true;
            *static_cast<float*>(value) = m_value;
            return;
        }

        const double deltaTime = now - m_time;
        if (deltaTime > 0.0) {
            const float alpha = smoothing_alpha(halfLife, deltaTime);
            m_value += (input - m_value) * alpha;
            m_time = now;
        }
        *static_cast<float*>(value) = m_value;
    }

    CachingPolicy caching_policy() const override {
        return CachingPolicy::EvaluateOnEveryRead;
    }

    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (!equals_ignore_case(name, "halfLife"))
            return false;
        halfLife = std::max(0.0f, value.as_float(halfLife));
        return true;
    }

private:
    float halfLife = 0.1f;
    mutable bool m_initialized = false;
    mutable double m_time = 0.0;
    mutable float m_value = 0.0f;
};

class SmoothVector2Processor final : public IProcessor {
public:
    ValueType value_type() const override { return ValueType::Vector2; }

    void process(void* value, std::size_t size, const Control*) const override {
        if (size < sizeof(Vector2))
            return;
        m_value = *static_cast<Vector2*>(value);
        m_initialized = false;
    }

    void process(void* value, std::size_t size, const Control*,
                 const ProcessorContext& context) const override {
        if (size < sizeof(Vector2))
            return;
        const Vector2 input = *static_cast<Vector2*>(value);
        if (context.runtime == nullptr || halfLife <= 0.0f) {
            m_initialized = false;
            *static_cast<Vector2*>(value) = input;
            return;
        }

        const double now = context.runtime->now();
        if (!m_initialized) {
            m_value = input;
            m_time = now;
            m_initialized = true;
            *static_cast<Vector2*>(value) = m_value;
            return;
        }

        const double deltaTime = now - m_time;
        if (deltaTime > 0.0) {
            const float alpha = smoothing_alpha(halfLife, deltaTime);
            m_value.x += (input.x - m_value.x) * alpha;
            m_value.y += (input.y - m_value.y) * alpha;
            m_time = now;
        }
        *static_cast<Vector2*>(value) = m_value;
    }

    CachingPolicy caching_policy() const override {
        return CachingPolicy::EvaluateOnEveryRead;
    }

    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (!equals_ignore_case(name, "halfLife"))
            return false;
        halfLife = std::max(0.0f, value.as_float(halfLife));
        return true;
    }

private:
    float halfLife = 0.1f;
    mutable bool m_initialized = false;
    mutable double m_time = 0.0;
    mutable Vector2 m_value{};
};

class SmoothVector3Processor final : public IProcessor {
public:
    ValueType value_type() const override { return ValueType::Vector3; }

    void process(void* value, std::size_t size, const Control*) const override {
        if (size < sizeof(Vector3))
            return;
        m_value = *static_cast<Vector3*>(value);
        m_initialized = false;
    }

    void process(void* value, std::size_t size, const Control*,
                 const ProcessorContext& context) const override {
        if (size < sizeof(Vector3))
            return;
        const Vector3 input = *static_cast<Vector3*>(value);
        if (context.runtime == nullptr || halfLife <= 0.0f) {
            m_initialized = false;
            *static_cast<Vector3*>(value) = input;
            return;
        }

        const double now = context.runtime->now();
        if (!m_initialized) {
            m_value = input;
            m_time = now;
            m_initialized = true;
            *static_cast<Vector3*>(value) = m_value;
            return;
        }

        const double deltaTime = now - m_time;
        if (deltaTime > 0.0) {
            const float alpha = smoothing_alpha(halfLife, deltaTime);
            m_value.x += (input.x - m_value.x) * alpha;
            m_value.y += (input.y - m_value.y) * alpha;
            m_value.z += (input.z - m_value.z) * alpha;
            m_time = now;
        }
        *static_cast<Vector3*>(value) = m_value;
    }

    CachingPolicy caching_policy() const override {
        return CachingPolicy::EvaluateOnEveryRead;
    }

    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (!equals_ignore_case(name, "halfLife"))
            return false;
        halfLife = std::max(0.0f, value.as_float(halfLife));
        return true;
    }

private:
    float halfLife = 0.1f;
    mutable bool m_initialized = false;
    mutable double m_time = 0.0;
    mutable Vector3 m_value{};
};

} // namespace

void apply_parameters(IProcessor& processor, const std::vector<NamedValue>& parameters) {
    for (const NamedValue& parameter : parameters)
        processor.set_parameter(parameter.name, parameter.value);
}

std::unique_ptr<IProcessor> make_processor(std::string_view name,
                                           const std::vector<NamedValue>& parameters) {
    std::unique_ptr<IProcessor> processor;
    if (equals_ignore_case(name, "Invert"))
        processor = std::make_unique<InvertProcessor>();
    else if (equals_ignore_case(name, "InvertVector2"))
        processor = std::make_unique<InvertVector2Processor>();
    else if (equals_ignore_case(name, "InvertVector3"))
        processor = std::make_unique<InvertVector3Processor>();
    else if (equals_ignore_case(name, "Scale"))
        processor = std::make_unique<ScaleProcessor>();
    else if (equals_ignore_case(name, "ScaleVector2"))
        processor = std::make_unique<ScaleVector2Processor>();
    else if (equals_ignore_case(name, "ScaleVector3"))
        processor = std::make_unique<ScaleVector3Processor>();
    else if (equals_ignore_case(name, "Normalize"))
        processor = std::make_unique<NormalizeProcessor>();
    else if (equals_ignore_case(name, "NormalizeVector2"))
        processor = std::make_unique<NormalizeVector2Processor>();
    else if (equals_ignore_case(name, "NormalizeVector3"))
        processor = std::make_unique<NormalizeVector3Processor>();
    else if (equals_ignore_case(name, "Clamp"))
        processor = std::make_unique<ClampProcessor>();
    else if (equals_ignore_case(name, "AxisDeadzone") ||
             equals_ignore_case(name, "AxisDeadzoneCurve") ||
             equals_ignore_case(name, "DeadzoneCurve"))
        processor = std::make_unique<AxisDeadzoneProcessor>();
    else if (equals_ignore_case(name, "StickDeadzone") ||
             equals_ignore_case(name, "StickDeadzoneCurve"))
        processor = std::make_unique<StickDeadzoneProcessor>();
    else if (equals_ignore_case(name, "CompensateDirection"))
        processor = std::make_unique<CompensateDirectionProcessor>();
    else if (equals_ignore_case(name, "CompensateRotation"))
        processor = std::make_unique<CompensateRotationProcessor>();
    else if (equals_ignore_case(name, "AndroidCompensateDirection"))
        processor = std::make_unique<AndroidCompensateDirectionProcessor>();
    else if (equals_ignore_case(name, "AndroidCompensateRotation"))
        processor = std::make_unique<AndroidCompensateRotationProcessor>();
    else if (equals_ignore_case(name, "Smooth"))
        processor = std::make_unique<SmoothProcessor>();
    else if (equals_ignore_case(name, "SmoothVector2"))
        processor = std::make_unique<SmoothVector2Processor>();
    else if (equals_ignore_case(name, "SmoothVector3"))
        processor = std::make_unique<SmoothVector3Processor>();

    if (processor)
        apply_parameters(*processor, parameters);
    return processor;
}

} // namespace insys

