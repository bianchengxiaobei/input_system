#include "insys/control_layout.h"

#include <unordered_map>

namespace insys {
namespace {

template <typename T>
void append_unique(std::vector<T>& destination, const std::vector<T>& source) {
    for (const T& value : source) {
        bool exists = false;
        for (const T& current : destination) {
            if (current == value) {
                exists = true;
                break;
            }
        }
        if (!exists)
            destination.push_back(value);
    }
}

} // namespace

ControlItem ControlItem::merge(const ControlItem& base) const {
    ControlItem result = base;

    if (!name.empty())
        result.name = name;
    if (!layout.empty())
        result.layout = layout;
    if (!variants.empty())
        result.variants = variants;
    if (!useStateFrom.empty())
        result.useStateFrom = useStateFrom;
    if (!displayName.empty())
        result.displayName = displayName;
    if (!shortDisplayName.empty())
        result.shortDisplayName = shortDisplayName;

    append_unique(result.usages, usages);
    append_unique(result.aliases, aliases);

    for (const NamedValue& parameter : parameters) {
        bool replaced = false;
        for (NamedValue& existing : result.parameters) {
            if (existing.name == parameter.name) {
                existing.value = parameter.value;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            result.parameters.push_back(parameter);
    }

    for (const NameAndParameters& processor : processors) {
        bool replaced = false;
        for (NameAndParameters& existing : result.processors) {
            if (existing.name == processor.name) {
                existing.parameters = processor.parameters;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            result.processors.push_back(processor);
    }

    if (offset != StateBlock::AutomaticOffset)
        result.offset = offset;
    if (bit != 0)
        result.bit = bit;
    if (sizeInBits != 0)
        result.sizeInBits = sizeInBits;
    if (format != FourCC{})
        result.format = format;
    if (arraySize != 0)
        result.arraySize = arraySize;
    if (keyCode >= 0)
        result.keyCode = keyCode;

    if (defaultState)
        result.defaultState = defaultState;
    if (minValue)
        result.minValue = minValue;
    if (maxValue)
        result.maxValue = maxValue;

    result.isNoisy = result.isNoisy || isNoisy;
    result.isSynthetic = result.isSynthetic || isSynthetic;
    result.dontReset = result.dontReset || dontReset;
    if (normalize) {
        result.normalize = true;
        result.normalizeMin = normalizeMin;
        result.normalizeMax = normalizeMax;
        result.normalizeZero = normalizeZero;
    }
    if (invert)
        result.invert = true;
    result.isFirstDefinedInThisLayout = false;
    return result;
}

void Layout::merge_from(const Layout& base) {
    std::unordered_map<std::string, std::size_t> indexByName;
    std::vector<ControlItem> merged = base.controls;
    for (std::size_t i = 0; i < merged.size(); ++i)
        indexByName[merged[i].name] = i;

    for (const ControlItem& item : controls) {
        auto it = indexByName.find(item.name);
        if (it != indexByName.end()) {
            merged[it->second] = item.merge(merged[it->second]);
        } else {
            merged.push_back(item);
            indexByName[item.name] = merged.size() - 1;
        }
    }

    for (ControlItem& item : merged)
        item.isFirstDefinedInThisLayout = false;

    controls = std::move(merged);
    baseLayout = const_cast<Layout*>(&base);
    if (stateFormat == FourCC{})
        stateFormat = base.stateFormat;
    if (stateSizeInBits == 0)
        stateSizeInBits = base.stateSizeInBits;
    if (!factory)
        factory = base.factory;
    if (displayName.empty())
        displayName = base.displayName;
}

} // namespace insys

