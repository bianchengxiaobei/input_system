#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "insys/interned_string.h"
#include "insys/low/key.h"
#include "insys/processor.h"
#include "insys/state_block.h"
#include "insys/types.h"

namespace insys {

class Device;

enum ControlFlags : uint32_t {
    kControlIsButton = 1u << 0,
    kControlIsAxis = 1u << 1,
    kControlIsNoisy = 1u << 2,
    kControlIsSynthetic = 1u << 3,
};

class Control {
public:
    virtual ~Control() = default;

    InternedString name;
    InternedString layout;
    std::string displayName;
    std::string shortDisplayName;
    Device* device = nullptr;
    Control* parent = nullptr;

    std::vector<Control*> children;
    std::vector<InternedString> usages;
    std::vector<InternedString> aliases;
    uint32_t childStart = 0;
    uint32_t childCount = 0;

    StateBlock stateBlock;
    uint32_t flags = 0;
    bool dontReset = false;

    std::vector<std::unique_ptr<IProcessor>> processors;

    virtual uint32_t value_size_bytes() const = 0;
    virtual void read_value_from_state(const uint8_t* state, void* out) const = 0;
    virtual void write_value_to_state(uint8_t* state, const void* in) const = 0;
    virtual float evaluate_magnitude(const uint8_t* state) const;

    virtual float read_float(const uint8_t* state) const;
    virtual int32_t read_integer(const uint8_t* state) const;
    virtual double read_double(const uint8_t* state) const;
    Vector2 read_vector2(const uint8_t* state) const;
    Vector3 read_vector3(const uint8_t* state) const;
    Quaternion read_quaternion(const uint8_t* state) const;
    bool read_bool(const uint8_t* state, float threshold = 0.5f) const;

    std::string path() const;
    bool is_button() const { return (flags & kControlIsButton) != 0; }
    bool is_axis() const { return (flags & kControlIsAxis) != 0; }
};

class AxisControl : public Control {
public:
    bool normalize = false;
    float normalizeMin = 0.0f;
    float normalizeMax = 0.0f;
    float normalizeZero = 0.0f;
    bool invert = false;

    AxisControl() { flags |= kControlIsAxis; }
    uint32_t value_size_bytes() const override { return sizeof(float); }
    void read_value_from_state(const uint8_t* state, void* out) const override;
    void write_value_to_state(uint8_t* state, const void* in) const override;
};

class IntegerControl : public Control {
public:
    IntegerControl() { flags |= kControlIsAxis; }
    uint32_t value_size_bytes() const override { return sizeof(int32_t); }
    void read_value_from_state(const uint8_t* state, void* out) const override;
    void write_value_to_state(uint8_t* state, const void* in) const override;
    float read_float(const uint8_t* state) const override {
        return static_cast<float>(read_integer(state));
    }
    int32_t read_integer(const uint8_t* state) const override;
};

class DoubleControl : public Control {
public:
    DoubleControl() { flags |= kControlIsAxis; }
    uint32_t value_size_bytes() const override { return sizeof(double); }
    void read_value_from_state(const uint8_t* state, void* out) const override;
    void write_value_to_state(uint8_t* state, const void* in) const override;
    float read_float(const uint8_t* state) const override {
        return static_cast<float>(read_double(state));
    }
    double read_double(const uint8_t* state) const override;
};

class ButtonControl : public Control {
public:
    float pressPoint = 0.5f;

    ButtonControl() { flags |= kControlIsButton | kControlIsAxis; }
    uint32_t value_size_bytes() const override { return sizeof(float); }
    void read_value_from_state(const uint8_t* state, void* out) const override;
    void write_value_to_state(uint8_t* state, const void* in) const override;
};

class Vector2Control : public Control {
public:
    Vector2Control() { flags |= kControlIsAxis; }
    uint32_t value_size_bytes() const override { return sizeof(Vector2); }
    void read_value_from_state(const uint8_t* state, void* out) const override;
    void write_value_to_state(uint8_t* state, const void* in) const override;
    float evaluate_magnitude(const uint8_t* state) const override;
};

class DeltaControl : public Vector2Control {
public:
    DeltaControl() = default;
};

class Vector3Control : public Control {
public:
    Vector3Control() { flags |= kControlIsAxis; }
    uint32_t value_size_bytes() const override { return sizeof(Vector3); }
    void read_value_from_state(const uint8_t* state, void* out) const override;
    void write_value_to_state(uint8_t* state, const void* in) const override;
    float evaluate_magnitude(const uint8_t* state) const override;
};

class QuaternionControl : public Control {
public:
    QuaternionControl() { flags |= kControlIsAxis; }
    uint32_t value_size_bytes() const override { return sizeof(Quaternion); }
    void read_value_from_state(const uint8_t* state, void* out) const override;
    void write_value_to_state(uint8_t* state, const void* in) const override;
};

class TouchControl : public Control {
public:
    uint32_t value_size_bytes() const override { return 0; }
    void read_value_from_state(const uint8_t*, void*) const override {}
    void write_value_to_state(uint8_t*, const void*) const override {}
};

class KeyControl : public Control {
public:
    Key key = Key::A;

    KeyControl() { flags |= kControlIsButton; }
    uint32_t value_size_bytes() const override { return sizeof(float); }
    void read_value_from_state(const uint8_t* state, void* out) const override;
    void write_value_to_state(uint8_t* state, const void* in) const override;
};

class BitButtonControl : public Control {
public:
    BitButtonControl() { flags |= kControlIsButton; }
    uint32_t value_size_bytes() const override { return sizeof(float); }
    void read_value_from_state(const uint8_t* state, void* out) const override;
    void write_value_to_state(uint8_t* state, const void* in) const override;
};

class DiscreteButtonControl : public ButtonControl {
public:
    int32_t minValue = 0;
    int32_t maxValue = 0;
    int32_t wrapAtValue = 0;
    int32_t nullValue = 0;
    bool writeNullAndMaxValue = false;

    DiscreteButtonControl() { flags |= kControlIsButton; }
    void read_value_from_state(const uint8_t* state, void* out) const override;
    void write_value_to_state(uint8_t* state, const void* in) const override;
};

class AnyKeyControl : public Control {
public:
    AnyKeyControl() { flags |= kControlIsButton | kControlIsSynthetic; }
    uint32_t value_size_bytes() const override { return sizeof(float); }
    void read_value_from_state(const uint8_t* state, void* out) const override;
    void write_value_to_state(uint8_t* state, const void* in) const override;
};

class DpadControl : public Control {
public:
    DpadControl() { flags |= kControlIsAxis | kControlIsSynthetic; }
    uint32_t value_size_bytes() const override { return sizeof(Vector2); }
    void read_value_from_state(const uint8_t* state, void* out) const override;
    void write_value_to_state(uint8_t* state, const void* in) const override;
    float evaluate_magnitude(const uint8_t* state) const override;
};

} // namespace insys





