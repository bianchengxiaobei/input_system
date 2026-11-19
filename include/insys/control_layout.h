#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "insys/control.h"
#include "insys/low/fourcc.h"
#include "insys/low/primitive_value.h"
#include "insys/state_block.h"

namespace insys {

struct NamedValue {
    std::string name;
    PrimitiveValue value;
};

struct NameAndParameters {
    std::string name;
    std::vector<NamedValue> parameters;
};

struct ControlItem {
    std::string name;
    std::string layout;
    std::string variants;
    std::string useStateFrom;
    std::string displayName;
    std::string shortDisplayName;

    std::vector<std::string> usages;
    std::vector<std::string> aliases;
    std::vector<NamedValue> parameters;
    std::vector<NameAndParameters> processors;

    uint32_t offset = StateBlock::AutomaticOffset;
    uint32_t bit = 0;
    uint32_t sizeInBits = 0;
    FourCC format{};
    int arraySize = 0;
    int keyCode = -1;

    std::optional<PrimitiveValue> defaultState;
    std::optional<PrimitiveValue> minValue;
    std::optional<PrimitiveValue> maxValue;

    bool isModifyingExistingControl = false;
    bool isNoisy = false;
    bool isSynthetic = false;
    bool dontReset = false;
    bool isFirstDefinedInThisLayout = false;

    bool normalize = false;
    float normalizeMin = 0.0f;
    float normalizeMax = 0.0f;
    float normalizeZero = 0.0f;
    bool invert = false;

    ControlItem merge(const ControlItem& base) const;
};

class Layout {
public:
    std::string name;
    std::string variants;
    std::string extends;
    FourCC stateFormat{};
    uint32_t stateSizeInBits = 0;
    std::string displayName;

    std::vector<ControlItem> controls;
    Layout* baseLayout = nullptr;
    std::function<std::unique_ptr<Control>()> factory;

    void merge_from(const Layout& base);
};

} // namespace insys

