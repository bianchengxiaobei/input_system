#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include "insys/actions/value_type.h"
#include "insys/control_layout.h"
#include "insys/types.h"

namespace insys {

class ActionState;
class IProcessor;

class CompositeContext {
public:
    CompositeContext(ActionState& state, int compositeBindingIndex);

    float read_float(int partIndex) const;
    Vector2 read_vector2(int partIndex) const;
    Vector3 read_vector3(int partIndex) const;
    bool read_value_as_button(int partIndex) const;
    void read_value(int partIndex, void* out, std::size_t size) const;
    float read_part_magnitude(int partIndex) const;
    double read_part_press_time(int partIndex) const;
    bool shortcut_keys_consume_input() const;
    float magnitude() const;

private:
    ActionState* m_state = nullptr;
    int m_compositeBindingIndex = -1;
};

class IComposite {
public:
    virtual ~IComposite() = default;

    virtual ValueType value_type() const = 0;
    virtual std::size_t value_size_bytes() const { return value_type_size(value_type()); }
    virtual void read_value(CompositeContext& context, void* out) const = 0;
    virtual float evaluate_magnitude(CompositeContext& context) const = 0;
    virtual int assign_part(std::string_view name, int& currentPartIndex) = 0;
    virtual void configure_part(int partIndex, ValueType valueType, std::size_t valueSize,
                                bool isButton) {
        (void)partIndex;
        (void)valueType;
        (void)valueSize;
        (void)isButton;
    }

    void set_part_binding_index(int partIndex, int bindingIndex);
    int part_binding_index(int partIndex) const;

    std::vector<int> partBindingIndices;

protected:
    int assign_named_part(std::string_view name, int& currentPartIndex,
                          const char* const* names, int nameCount);
};

std::unique_ptr<IComposite> make_composite(std::string_view name,
                                           const std::vector<NamedValue>& parameters);

} // namespace insys
