#include "insys/device_builder.h"

#include <algorithm>
#include <cstdint>

#include "insys/low/state_format.h"
#include "insys/string_utils.h"

namespace insys {
namespace {

uint32_t primitive_format_bits(FourCC format) {
    if (format == kFormatBit)
        return 1;
    if (format == kFormatByte)
        return 8;
    if (format == kFormatInt || format == kFormatFloat)
        return 32;
    if (format == kFormatUShort)
        return 16;
    if (format == kFormatUInt)
        return 32;
    if (format == kFormatDouble)
        return 64;
    if (format == kFormatVector2)
        return 64;
    if (format == kFormatVector3)
        return 96;
    if (format == kFormatQuaternion)
        return 128;
    return 0;
}

uint32_t following_byte_offset(uint32_t byteOffset, uint32_t bitOffsetPlusSize) {
    return byteOffset + ((bitOffsetPlusSize + 7u) >> 3);
}

uint32_t align_natural(uint32_t offset, uint32_t size) {
    const uint32_t alignment = size >= 8 ? 8 : (size >= 4 ? 4 : (size >= 2 ? 2 : 1));
    return (offset + alignment - 1) & ~(alignment - 1);
}

std::unique_ptr<Control> make_control_for_item(const ControlItem& item) {
    const std::string layout = to_lower(item.layout.empty() ? "axis" : item.layout);

    if (layout == "key")
        return std::make_unique<KeyControl>();
    if (layout == "integer")
        return std::make_unique<IntegerControl>();
    if (layout == "double")
        return std::make_unique<DoubleControl>();
    if (layout == "delta")
        return std::make_unique<DeltaControl>();
    if (layout == "discretebutton")
        return std::make_unique<DiscreteButtonControl>();
    if (layout == "anykey")
        return std::make_unique<AnyKeyControl>();
    if (layout == "dpad")
        return std::make_unique<DpadControl>();
    if (layout == "stick" || layout == "vector2")
        return std::make_unique<Vector2Control>();
    if (layout == "vector3")
        return std::make_unique<Vector3Control>();
    if (layout == "quaternion")
        return std::make_unique<QuaternionControl>();
    if (layout == "touch")
        return std::make_unique<TouchControl>();
    if (layout == "button") {
        if (item.format == kFormatBit || item.sizeInBits == 1)
            return std::make_unique<BitButtonControl>();
        return std::make_unique<ButtonControl>();
    }
    if (layout == "axis")
        return std::make_unique<AxisControl>();

    if (item.format == kFormatVector2)
        return std::make_unique<Vector2Control>();
    if (item.format == kFormatVector3)
        return std::make_unique<Vector3Control>();
    if (item.format == kFormatQuaternion)
        return std::make_unique<QuaternionControl>();
    if (item.format == kFormatBit || item.sizeInBits == 1)
        return std::make_unique<BitButtonControl>();
    return std::make_unique<AxisControl>();
}

} // namespace

DeviceBuilder::DeviceBuilder(LayoutRegistry& registry) : m_registry(&registry) {}

std::unique_ptr<Device> DeviceBuilder::build(std::string_view layoutName,
                                             std::string_view variants,
                                             int deviceId,
                                             const DeviceDescription& description) {
    Layout* layout = m_registry != nullptr ? m_registry->find(layoutName) : nullptr;
    if (layout == nullptr)
        return nullptr;

    std::unique_ptr<Control> rootControl =
        layout->factory ? layout->factory() : std::make_unique<Device>();
    auto* device = dynamic_cast<Device*>(rootControl.get());
    if (device == nullptr)
        return nullptr;

    std::unique_ptr<Device> deviceOwner(static_cast<Device*>(rootControl.release()));

    m_device = deviceOwner.get();
    m_useStateFromControls.clear();
    m_useStateFromRequests.clear();

    device->deviceId = deviceId;
    device->name = InternedString(layout->name);
    device->layout = InternedString(layout->name);
    device->layoutLineage.clear();
    for (Layout* current = layout; current != nullptr; current = current->baseLayout)
        device->layoutLineage.push_back(current->name);
    device->displayName = layout->displayName.empty() ? layout->name : layout->displayName;
    device->description = description;
    device->stateBlock.format = layout->stateFormat;
    device->stateBlock.byteOffset = 0;
    device->stateBlock.bitOffset = 0;
    device->stateBlock.sizeInBits = 0;

    if (!variants.empty())
        device->description.version = std::string(variants);

    add_child_controls(*layout, device);
    compute_state_layout(device);
    if (layout->stateSizeInBits != 0)
        device->stateBlock.sizeInBits = layout->stateSizeInBits;
    device->finalize_hierarchy();
    if (auto* keyboard = dynamic_cast<Keyboard*>(device))
        keyboard->rebuild_key_cache();
    apply_use_state_from(device);

    m_device = nullptr;
    return deviceOwner;
}

void DeviceBuilder::add_child_controls(Layout& layout, Control* parent) {
    for (const ControlItem& item : layout.controls) {
        if (item.name.empty())
            continue;

        std::string name = item.name;
        Control* targetParent = parent;

        const std::size_t slash = name.find_last_of('/');
        if (slash != std::string::npos) {
            const std::string parentName = name.substr(0, slash);
            name = name.substr(slash + 1);

            Control* existing = find_child(parent, parentName);
            if (existing == nullptr)
                continue;
            targetParent = existing;
        }

        if (name.empty())
            continue;

        add_control(item, targetParent, name);
    }
}

Control* DeviceBuilder::add_control(const ControlItem& item, Control* parent,
                                    const std::string& name) {
    if (m_device == nullptr)
        return nullptr;

    std::unique_ptr<Control> control = make_control_for_item(item);
    if (!control)
        return nullptr;

    control->name = InternedString(to_lower(name));
    control->layout = InternedString(to_lower(item.layout.empty() ? "Axis" : item.layout));
    control->displayName = item.displayName.empty() ? name : item.displayName;
    control->shortDisplayName = item.shortDisplayName;
    control->parent = parent;
    control->device = m_device;
    control->dontReset = item.dontReset;

    for (const std::string& usage : item.usages)
        control->usages.emplace_back(usage);
    for (const std::string& alias : item.aliases)
        control->aliases.emplace_back(alias);

    control->stateBlock.format = item.format;
    control->stateBlock.byteOffset = item.offset;
    control->stateBlock.bitOffset = item.bit;
    control->stateBlock.sizeInBits = item.sizeInBits;

    if (control->stateBlock.sizeInBits == 0 && control->stateBlock.format != FourCC{})
        control->stateBlock.sizeInBits = primitive_format_bits(control->stateBlock.format);

    if (auto* keyControl = dynamic_cast<KeyControl*>(control.get())) {
        if (item.keyCode >= 0)
            keyControl->key = static_cast<Key>(item.keyCode);
    }

    if (auto* axis = dynamic_cast<AxisControl*>(control.get())) {
        axis->normalize = item.normalize;
        axis->normalizeMin = item.normalizeMin;
        axis->normalizeMax = item.normalizeMax;
        axis->normalizeZero = item.normalizeZero;
        axis->invert = item.invert;
    }

    if (auto* discrete = dynamic_cast<DiscreteButtonControl*>(control.get())) {
        for (const NamedValue& parameter : item.parameters) {
            const std::string parameterName = to_lower(parameter.name);
            if (parameterName == "minvalue")
                discrete->minValue = parameter.value.as_int(discrete->minValue);
            else if (parameterName == "maxvalue")
                discrete->maxValue = parameter.value.as_int(discrete->maxValue);
            else if (parameterName == "wrapatvalue")
                discrete->wrapAtValue = parameter.value.as_int(discrete->wrapAtValue);
            else if (parameterName == "nullvalue")
                discrete->nullValue = parameter.value.as_int(discrete->nullValue);
            else if (parameterName == "writemode")
                discrete->writeNullAndMaxValue =
                    parameter.value.as_int(0) == 1;
        }
    }

    if (item.isNoisy)
        control->flags |= kControlIsNoisy;
    if (item.isSynthetic)
        control->flags |= kControlIsSynthetic;

    if (!item.useStateFrom.empty()) {
        m_useStateFromControls.insert(control.get());
        m_useStateFromRequests.emplace_back(control.get(), item.useStateFrom);
    }

    Control* raw = control.get();
    m_device->ownedControls.push_back(std::move(control));
    parent->children.push_back(raw);
    return raw;
}

void DeviceBuilder::compute_state_layout(Control* control) {
    if (control->stateBlock.sizeInBits == 0 && control->stateBlock.format != FourCC{})
        control->stateBlock.sizeInBits = primitive_format_bits(control->stateBlock.format);

    if (m_useStateFromControls.count(control) != 0) {
        control->stateBlock.sizeInBits = 0;
        return;
    }

    const bool hasChildren = !control->children.empty();
    if (control->stateBlock.sizeInBits == 0 && !hasChildren) {
        control->stateBlock.sizeInBits = 1;
        return;
    }

    if (!hasChildren)
        return;

    uint32_t firstUnfixedByteOffset = 0;

    for (Control* child : control->children) {
        if (m_useStateFromControls.count(child) != 0)
            continue;
        const StateBlock& block = child->stateBlock;
        if (block.byteOffset == StateBlock::InvalidOffset ||
            block.byteOffset == StateBlock::AutomaticOffset)
            continue;
        const uint32_t bitOffset = block.bitOffset == StateBlock::InvalidOffset ? 0 : block.bitOffset;
        const uint32_t end = following_byte_offset(block.byteOffset, bitOffset + block.sizeInBits);
        firstUnfixedByteOffset = std::max(firstUnfixedByteOffset, end);
    }

    uint32_t runningByteOffset = firstUnfixedByteOffset;
    Control* firstBitAddressingChild = nullptr;
    uint32_t bitfieldSizeInBits = 0;

    for (Control* child : control->children) {
        if (m_useStateFromControls.count(child) != 0)
            continue;

        StateBlock& block = child->stateBlock;
        if (block.byteOffset != StateBlock::InvalidOffset &&
            block.byteOffset != StateBlock::AutomaticOffset)
            continue;

        const bool isBitAddressing = (block.sizeInBits % 8) != 0;
        if (isBitAddressing) {
            if (firstBitAddressingChild == nullptr)
                firstBitAddressingChild = child;
            if (block.bitOffset == StateBlock::InvalidOffset ||
                block.bitOffset == StateBlock::AutomaticOffset) {
                block.bitOffset = bitfieldSizeInBits;
                bitfieldSizeInBits += block.sizeInBits;
            } else {
                bitfieldSizeInBits = std::max(bitfieldSizeInBits,
                                              block.bitOffset + block.sizeInBits);
            }
        } else {
            if (firstBitAddressingChild != nullptr) {
                runningByteOffset = following_byte_offset(runningByteOffset, bitfieldSizeInBits);
                firstBitAddressingChild = nullptr;
            }
            if (block.bitOffset == StateBlock::InvalidOffset)
                block.bitOffset = 0;
            runningByteOffset = align_natural(runningByteOffset, block.aligned_size_in_bytes());
        }

        block.byteOffset = runningByteOffset;

        if (!isBitAddressing)
            runningByteOffset = following_byte_offset(runningByteOffset, block.sizeInBits);
    }

    if (firstBitAddressingChild != nullptr)
        runningByteOffset = following_byte_offset(runningByteOffset, bitfieldSizeInBits);

    control->stateBlock.sizeInBits = runningByteOffset * 8u;
}

void DeviceBuilder::apply_use_state_from(Device* device) {
    for (const auto& [control, targetName] : m_useStateFromRequests) {
        Control* target = find_child(device, targetName);
        if (target == nullptr) {
            control->stateBlock.sizeInBits = 0;
            continue;
        }
        control->stateBlock = target->stateBlock;
    }
}

Control* DeviceBuilder::find_child(Control* parent, std::string_view name) const {
    for (Control* child : parent->children) {
        if (equals_ignore_case(child->name.str(), name))
            return child;
    }
    return nullptr;
}

} // namespace insys





