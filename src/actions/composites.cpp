#include "insys/actions/composite.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "insys/actions/action_state.h"
#include "insys/string_utils.h"

namespace insys {
namespace {

Vector2 make_dpad_vector(bool up, bool down, bool left, bool right, bool normalize) {
    Vector2 value{(right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f),
                  (up ? 1.0f : 0.0f) - (down ? 1.0f : 0.0f)};
    if (normalize) {
        const float length = value.length();
        if (length > 1e-6f)
            value = value / length;
    }
    return value;
}

Vector2 make_dpad_vector(float up, float down, float left, float right) {
    return {right - left, up - down};
}

Vector3 make_dpad_vector(bool up, bool down, bool left, bool right, bool forward, bool backward,
                         bool normalize) {
    Vector3 value{(right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f),
                  (up ? 1.0f : 0.0f) - (down ? 1.0f : 0.0f),
                  (forward ? 1.0f : 0.0f) - (backward ? 1.0f : 0.0f)};
    if (normalize) {
        const float length = value.length();
        if (length > 1e-6f)
            value = value * (1.0f / length);
    }
    return value;
}

class AxisComposite final : public IComposite {
public:
    int negative = 0;
    int positive = 1;
    float minValue = -1.0f;
    float maxValue = 1.0f;
    int whichSideWins = 0;

    float mid_point() const { return (maxValue + minValue) * 0.5f; }

    ValueType value_type() const override { return ValueType::Float; }

    void read_value(CompositeContext& context, void* out) const override {
        const float negativeValue = std::fabs(context.read_float(negative));
        const float positiveValue = std::fabs(context.read_float(positive));
        bool negativeActuated = negativeValue > 1e-6f;
        bool positiveActuated = positiveValue > 1e-6f;

        if (negativeActuated == positiveActuated) {
            if (whichSideWins == 1)
                positiveActuated = false;
            else if (whichSideWins == 2)
                negativeActuated = false;
            else {
                const float mid = mid_point();
                std::memcpy(out, &mid, sizeof(float));
                return;
            }
        }

        const float mid = mid_point();
        const float result = negativeActuated
                                 ? mid - (mid - minValue) * negativeValue
                                 : mid + (maxValue - mid) * positiveValue;
        std::memcpy(out, &result, sizeof(float));
    }

    float evaluate_magnitude(CompositeContext& context) const override {
        float value = 0.0f;
        read_value(context, &value);
        const float mid = mid_point();
        const float distance = std::fabs(value - mid);
        if (value < mid) {
            const float range = std::fabs(minValue);
            return range > 0.0f ? std::min(distance / range, 1.0f) : 0.0f;
        }
        const float range = std::fabs(maxValue);
        return range > 0.0f ? std::min(distance / range, 1.0f) : 0.0f;
    }

    int assign_part(std::string_view name, int& currentPartIndex) override {
        static const char* const names[] = {"negative", "positive"};
        return assign_named_part(name, currentPartIndex, names, 2);
    }

    bool set_part_parameter(std::string_view name, const PrimitiveValue& value) {
        if (equals_ignore_case(name, "minValue")) {
            minValue = value.as_float(minValue);
            return true;
        }
        if (equals_ignore_case(name, "maxValue")) {
            maxValue = value.as_float(maxValue);
            return true;
        }
        if (equals_ignore_case(name, "whichSideWins")) {
            whichSideWins = value.as_int(whichSideWins);
            return true;
        }
        return false;
    }
};

class Vector2Composite final : public IComposite {
public:
    enum class Mode { DigitalNormalized = 0, Digital = 1, Analog = 2 };

    int up = 0;
    int down = 1;
    int left = 2;
    int right = 3;
    Mode mode = Mode::DigitalNormalized;

    ValueType value_type() const override { return ValueType::Vector2; }

    void read_value(CompositeContext& context, void* out) const override {
        if (mode == Mode::Analog) {
            const Vector2 value = make_dpad_vector(context.read_float(up), context.read_float(down),
                                                   context.read_float(left),
                                                   context.read_float(right));
            std::memcpy(out, &value, sizeof(Vector2));
            return;
        }

        const Vector2 value =
            make_dpad_vector(context.read_value_as_button(up), context.read_value_as_button(down),
                             context.read_value_as_button(left), context.read_value_as_button(right),
                             mode == Mode::DigitalNormalized);
        std::memcpy(out, &value, sizeof(Vector2));
    }

    float evaluate_magnitude(CompositeContext& context) const override {
        Vector2 value;
        read_value(context, &value);
        return value.length();
    }

    int assign_part(std::string_view name, int& currentPartIndex) override {
        static const char* const names[] = {"up", "down", "left", "right"};
        return assign_named_part(name, currentPartIndex, names, 4);
    }

    bool set_part_parameter(std::string_view name, const PrimitiveValue& value) {
        if (equals_ignore_case(name, "mode")) {
            mode = static_cast<Mode>(value.as_int(static_cast<int32_t>(mode)));
            return true;
        }
        if (equals_ignore_case(name, "normalize")) {
            if (!value.as_bool(true))
                mode = Mode::Digital;
            return true;
        }
        return false;
    }
};

class Vector3Composite final : public IComposite {
public:
    enum class Mode { DigitalNormalized = 0, Digital = 1, Analog = 2 };

    int up = 0;
    int down = 1;
    int left = 2;
    int right = 3;
    int forward = 4;
    int backward = 5;
    Mode mode = Mode::DigitalNormalized;

    ValueType value_type() const override { return ValueType::Vector3; }

    void read_value(CompositeContext& context, void* out) const override {
        Vector3 value;
        if (mode == Mode::Analog) {
            value = {context.read_float(right) - context.read_float(left),
                     context.read_float(up) - context.read_float(down),
                     context.read_float(forward) - context.read_float(backward)};
        } else {
            value = make_dpad_vector(context.read_value_as_button(up),
                                     context.read_value_as_button(down),
                                     context.read_value_as_button(left),
                                     context.read_value_as_button(right),
                                     context.read_value_as_button(forward),
                                     context.read_value_as_button(backward),
                                     mode == Mode::DigitalNormalized);
        }
        std::memcpy(out, &value, sizeof(Vector3));
    }

    float evaluate_magnitude(CompositeContext& context) const override {
        Vector3 value;
        read_value(context, &value);
        return value.length();
    }

    int assign_part(std::string_view name, int& currentPartIndex) override {
        static const char* const names[] = {"up", "down", "left", "right", "forward", "backward"};
        return assign_named_part(name, currentPartIndex, names, 6);
    }

    bool set_part_parameter(std::string_view name, const PrimitiveValue& value) {
        if (equals_ignore_case(name, "mode")) {
            mode = static_cast<Mode>(value.as_int(static_cast<int32_t>(mode)));
            return true;
        }
        return false;
    }
};

class ModifierComposite final : public IComposite {
public:
    enum class ModifiersOrder : int32_t {
        Default = 0,
        Ordered = 1,
        Unordered = 2,
    };

    explicit ModifierComposite(int modifierCount, bool fixedFloat)
        : m_modifierCount(modifierCount), m_fixedFloat(fixedFloat),
          m_bindingPart(modifierCount) {}

    ValueType value_type() const override {
        if (m_fixedFloat || m_bindingType == ValueType::None)
            return ValueType::Float;
        return m_bindingType;
    }

    std::size_t value_size_bytes() const override {
        if (m_fixedFloat || m_bindingSize == 0)
            return sizeof(float);
        return m_bindingSize;
    }

    void read_value(CompositeContext& context, void* out) const override {
        const std::size_t outSize = value_size_bytes();
        if (!modifiers_are_pressed(context)) {
            std::memset(out, 0, outSize);
            return;
        }
        context.read_value(m_bindingPart, out, outSize);
    }

    float evaluate_magnitude(CompositeContext& context) const override {
        if (!modifiers_are_pressed(context))
            return 0.0f;
        return context.read_part_magnitude(m_bindingPart);
    }

    int assign_part(std::string_view name, int& currentPartIndex) override {
        auto assign = [&](int index) {
            currentPartIndex = std::max(currentPartIndex, index + 1);
            return index;
        };

        if (m_modifierCount == 1) {
            if (equals_ignore_case(name, "modifier"))
                return assign(0);
        } else {
            if (equals_ignore_case(name, "modifier1") || equals_ignore_case(name, "modifier"))
                return assign(0);
            if (equals_ignore_case(name, "modifier2"))
                return assign(1);
        }
        if (equals_ignore_case(name, "binding") || equals_ignore_case(name, "button"))
            return assign(m_bindingPart);
        return -1;
    }

    void configure_part(int partIndex, ValueType valueType, std::size_t valueSize,
                        bool isButton) override {
        if (partIndex != m_bindingPart)
            return;

        if (valueType != ValueType::None) {
            if (!m_bindingTypeConfigured) {
                m_bindingType = valueType;
                m_bindingTypeConfigured = true;
            } else if (m_bindingType != valueType) {
                m_bindingType = ValueType::None;
            }
        }
        m_bindingSize = std::max(m_bindingSize, valueSize);
        m_bindingIsButton = m_bindingIsButton && isButton;
    }

    bool set_parameter(std::string_view name, const PrimitiveValue& value) {
        if (equals_ignore_case(name, "modifiersOrder")) {
            const int32_t order = value.as_int(static_cast<int32_t>(m_modifiersOrder));
            if (order >= static_cast<int32_t>(ModifiersOrder::Default) &&
                order <= static_cast<int32_t>(ModifiersOrder::Unordered)) {
                m_modifiersOrder = static_cast<ModifiersOrder>(order);
            }
            return true;
        }
        if (equals_ignore_case(name, "overrideModifiersNeedToBePressedFirst")) {
            m_overrideModifiersNeedToBePressedFirst = value.as_bool(
                m_overrideModifiersNeedToBePressedFirst);
            return true;
        }
        return false;
    }

private:
    ModifiersOrder effective_order(CompositeContext& context) const {
        if (m_modifiersOrder != ModifiersOrder::Default)
            return m_modifiersOrder;
        if (m_overrideModifiersNeedToBePressedFirst)
            return ModifiersOrder::Unordered;
        return context.shortcut_keys_consume_input() ? ModifiersOrder::Ordered
                                                     : ModifiersOrder::Unordered;
    }

    bool modifiers_are_pressed(CompositeContext& context) const {
        for (int i = 0; i < m_modifierCount; ++i) {
            if (!context.read_value_as_button(i))
                return false;
        }

        if (!m_bindingIsButton || effective_order(context) != ModifiersOrder::Ordered)
            return true;

        const double bindingPressTime = context.read_part_press_time(m_bindingPart);
        if (bindingPressTime < 0.0)
            return true;
        for (int i = 0; i < m_modifierCount; ++i) {
            const double modifierPressTime = context.read_part_press_time(i);
            if (modifierPressTime < 0.0 || modifierPressTime > bindingPressTime)
                return false;
        }
        return true;
    }

    int m_modifierCount = 1;
    int m_bindingPart = 1;
    bool m_fixedFloat = false;
    ValueType m_bindingType = ValueType::Float;
    std::size_t m_bindingSize = sizeof(float);
    bool m_bindingTypeConfigured = false;
    bool m_bindingIsButton = true;
    ModifiersOrder m_modifiersOrder = ModifiersOrder::Default;
    bool m_overrideModifiersNeedToBePressedFirst = false;
};

} // namespace

CompositeContext::CompositeContext(ActionState& state, int compositeBindingIndex)
    : m_state(&state), m_compositeBindingIndex(compositeBindingIndex) {}

float CompositeContext::read_float(int partIndex) const {
    float value = 0.0f;
    read_value(partIndex, &value, sizeof(float));
    return value;
}

Vector2 CompositeContext::read_vector2(int partIndex) const {
    Vector2 value{};
    read_value(partIndex, &value, sizeof(Vector2));
    return value;
}

Vector3 CompositeContext::read_vector3(int partIndex) const {
    Vector3 value{};
    read_value(partIndex, &value, sizeof(Vector3));
    return value;
}

bool CompositeContext::read_value_as_button(int partIndex) const {
    if (m_state == nullptr)
        return false;

    return m_state->read_composite_part_as_button(m_compositeBindingIndex, partIndex);
}

void CompositeContext::read_value(int partIndex, void* out, std::size_t size) const {
    if (m_state != nullptr)
        m_state->read_composite_part(m_compositeBindingIndex, partIndex, out, size);
}

float CompositeContext::read_part_magnitude(int partIndex) const {
    if (m_state == nullptr)
        return 0.0f;
    return m_state->read_composite_part_magnitude(m_compositeBindingIndex, partIndex);
}

double CompositeContext::read_part_press_time(int partIndex) const {
    if (m_state == nullptr)
        return -1.0;
    return m_state->read_composite_part_press_time(m_compositeBindingIndex, partIndex);
}

bool CompositeContext::shortcut_keys_consume_input() const {
    return m_state != nullptr && m_state->manager != nullptr &&
           m_state->manager->settings().shortcutKeysConsumeInput;
}

float CompositeContext::magnitude() const {
    if (m_state == nullptr)
        return 0.0f;
    const int compositeIndex = m_state->binding_state(m_compositeBindingIndex)
                                   ->compositeOrCompositeBindingIndex;
    return m_state->composite_magnitude(compositeIndex);
}

void IComposite::set_part_binding_index(int partIndex, int bindingIndex) {
    if (partIndex < 0)
        return;
    if (static_cast<std::size_t>(partIndex) >= partBindingIndices.size())
        partBindingIndices.resize(static_cast<std::size_t>(partIndex) + 1, -1);
    partBindingIndices[static_cast<std::size_t>(partIndex)] = bindingIndex;
}

int IComposite::part_binding_index(int partIndex) const {
    if (partIndex < 0 || static_cast<std::size_t>(partIndex) >= partBindingIndices.size())
        return -1;
    return partBindingIndices[static_cast<std::size_t>(partIndex)];
}

int IComposite::assign_named_part(std::string_view name, int& currentPartIndex,
                                  const char* const* names, int nameCount) {
    for (int i = 0; i < nameCount; ++i) {
        if (equals_ignore_case(name, names[i])) {
            currentPartIndex = std::max(currentPartIndex, i + 1);
            return i;
        }
    }
    return -1;
}

std::unique_ptr<IComposite> make_composite(std::string_view name,
                                           const std::vector<NamedValue>& parameters) {
    std::unique_ptr<IComposite> composite;
    if (equals_ignore_case(name, "Axis") || equals_ignore_case(name, "1DAxis")) {
        auto value = std::make_unique<AxisComposite>();
        for (const NamedValue& parameter : parameters)
            value->set_part_parameter(parameter.name, parameter.value);
        composite = std::move(value);
    } else if (equals_ignore_case(name, "Vector2") || equals_ignore_case(name, "2DVector")) {
        auto value = std::make_unique<Vector2Composite>();
        for (const NamedValue& parameter : parameters)
            value->set_part_parameter(parameter.name, parameter.value);
        composite = std::move(value);
    } else if (equals_ignore_case(name, "Vector3") || equals_ignore_case(name, "3DVector")) {
        auto value = std::make_unique<Vector3Composite>();
        for (const NamedValue& parameter : parameters)
            value->set_part_parameter(parameter.name, parameter.value);
        composite = std::move(value);
    } else if (equals_ignore_case(name, "OneModifier") ||
               equals_ignore_case(name, "ButtonWithOneModifier")) {
        auto value = std::make_unique<ModifierComposite>(
            1, equals_ignore_case(name, "ButtonWithOneModifier"));
        for (const NamedValue& parameter : parameters)
            value->set_parameter(parameter.name, parameter.value);
        composite = std::move(value);
    } else if (equals_ignore_case(name, "TwoModifiers") ||
               equals_ignore_case(name, "ButtonWithTwoModifiers")) {
        auto value = std::make_unique<ModifierComposite>(
            2, equals_ignore_case(name, "ButtonWithTwoModifiers"));
        for (const NamedValue& parameter : parameters)
            value->set_parameter(parameter.name, parameter.value);
        composite = std::move(value);
    }
    return composite;
}

} // namespace insys
