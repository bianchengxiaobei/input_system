#include "insys/hid/hid_layout.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "insys/device.h"
#include "insys/low/primitive_value.h"
#include "insys/low/state_format.h"

namespace insys::hid {
namespace {

std::string usage_name(UsagePage page, uint32_t usage) {
    if (page == UsagePage::Button)
        return usage == 1 ? "trigger" : "button" + std::to_string(usage);

    if (page == UsagePage::GenericDesktop) {
        switch (usage) {
        case 0x30: return "x";
        case 0x31: return "y";
        case 0x32: return "z";
        case 0x33: return "rx";
        case 0x34: return "ry";
        case 0x35: return "rz";
        case 0x36: return "slider";
        case 0x37: return "dial";
        case 0x38: return "wheel";
        case 0x39: return "hat";
        case 0x3E: return "select";
        case 0x3D: return "start";
        case 0x40: return "vx";
        case 0x41: return "vy";
        case 0x42: return "vz";
        case 0x43: return "vbrx";
        case 0x44: return "vbry";
        case 0x45: return "vbrz";
        case 0x90: return "dpadUp";
        case 0x91: return "dpadDown";
        case 0x92: return "dpadRight";
        case 0x93: return "dpadLeft";
        default: break;
        }
    }

    return "usage_" + std::to_string(static_cast<uint16_t>(page)) + "_" +
           std::to_string(usage);
}

bool is_axis_usage(UsagePage page, uint32_t usage) {
    if (page != UsagePage::GenericDesktop)
        return false;
    switch (usage) {
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x45:
        return true;
    default:
        return false;
    }
}

bool is_y_axis_usage(UsagePage page, uint32_t usage) {
    if (page != UsagePage::GenericDesktop)
        return false;
    return usage == 0x31 || usage == 0x34 || usage == 0x41 || usage == 0x44;
}

bool is_button_usage(UsagePage page, uint32_t usage) {
    if (page == UsagePage::Button)
        return true;
    if (page != UsagePage::GenericDesktop)
        return false;
    return usage == 0x3D || usage == 0x3E || (usage >= 0x90 && usage <= 0x93);
}

std::string unique_name(std::unordered_set<std::string>& used, std::string name) {
    if (used.insert(name).second)
        return name;
    for (int index = 2;; ++index) {
        std::string candidate = name + "_" + std::to_string(index);
        if (used.insert(candidate).second)
            return candidate;
    }
}

void set_location(ControlItem& item, const Element& element,
                  uint32_t stateByteOffset) {
    item.offset = stateByteOffset + element.reportOffsetInBits / 8u;
    item.bit = element.reportOffsetInBits % 8u;
    item.sizeInBits = element.reportSizeInBits;
}

ControlItem make_button(const Element& element, std::string name,
                        uint32_t stateByteOffset = 0) {
    ControlItem item;
    item.name = std::move(name);
    item.layout = "Button";
    item.displayName = "Button " + std::to_string(element.usage);
    set_location(item, element, stateByteOffset);

    if (element.reportSizeInBits == 1) {
        item.format = kFormatBit;
        item.sizeInBits = 1;
    } else if (element.reportSizeInBits <= 8) {
        item.format = kFormatByte;
    } else if (element.reportSizeInBits <= 16) {
        item.format = kFormatUShort;
    } else {
        item.format = kFormatUInt;
    }

    if (element.usagePage == UsagePage::Button && element.usage == 1)
        item.usages.push_back("PrimaryAction");
    return item;
}

ControlItem make_axis(const Element& element, std::string name,
                      uint32_t stateByteOffset = 0) {
    ControlItem item;
    item.name = std::move(name);
    item.layout = "Axis";
    item.displayName = "Axis";
    set_location(item, element, stateByteOffset);

    if (element.reportSizeInBits <= 8)
        item.format = element.is_signed() ? kFormatInt : kFormatByte;
    else if (element.reportSizeInBits <= 16)
        item.format = element.is_signed() ? kFormatInt : kFormatUShort;
    else
        item.format = element.is_signed() ? kFormatInt : kFormatUInt;

    item.normalize = true;
    item.normalizeMin = static_cast<float>(element.logicalMin);
    item.normalizeMax = static_cast<float>(element.logicalMax);
    if (element.logicalMin == 0 && element.logicalMax == 0) {
        item.normalizeMin = 0.0f;
        item.normalizeMax = static_cast<float>((1ull << element.reportSizeInBits) - 1ull);
    }
    if (item.normalizeMax < item.normalizeMin)
        std::swap(item.normalizeMin, item.normalizeMax);
    item.normalizeZero = element.is_signed()
                             ? 0.0f
                             : item.normalizeMin +
                                   (item.normalizeMax - item.normalizeMin) * 0.5f;
    item.invert = is_y_axis_usage(element.usagePage, element.usage);
    return item;
}

void add_hat_controls(Layout& layout, const Element& element,
                      std::unordered_set<std::string>& used,
                      std::string baseName = "hat",
                      std::string parentLayout = "Axis",
                      uint32_t stateByteOffset = 0) {
    const std::string name = unique_name(used, std::move(baseName));
    ControlItem parent;
    parent.name = name;
    parent.layout = std::move(parentLayout);
    if (parent.layout == "Dpad") {
        parent.format = kFormatBit;
        parent.sizeInBits = 1;
        parent.offset = stateByteOffset + element.reportOffsetInBits / 8u;
        parent.bit = 0;
    } else {
        parent.format = element.reportSizeInBits <= 8 ? kFormatByte : kFormatInt;
        set_location(parent, element, stateByteOffset);
    }
    layout.controls.push_back(std::move(parent));

    const int32_t minimum = element.logicalMin;
    const int32_t maximum = element.logicalMax;
    if (maximum - minimum + 1 != 8)
        return;

    const int32_t nullValue = maximum + 1;

    auto add_direction = [&](std::string direction, int32_t minValue,
                             int32_t maxValue, int32_t wrapAtValue) {
        ControlItem item;
        item.name = name + "/" + std::move(direction);
        item.layout = "DiscreteButton";
        item.format = kFormatBit;
        item.sizeInBits = element.reportSizeInBits;
        item.offset = stateByteOffset + element.reportOffsetInBits / 8u;
        item.bit = element.reportOffsetInBits % 8u;
        item.parameters.push_back({"minValue", PrimitiveValue::from_int(minValue)});
        item.parameters.push_back({"maxValue", PrimitiveValue::from_int(maxValue)});
        item.parameters.push_back({"nullValue", PrimitiveValue::from_int(nullValue)});
        item.parameters.push_back({"wrapAtValue", PrimitiveValue::from_int(wrapAtValue)});
        layout.controls.push_back(std::move(item));
    };

    add_direction("up", maximum, minimum + 1, maximum);
    add_direction("right", minimum + 1, minimum + 3, 0);
    add_direction("down", minimum + 3, minimum + 5, 0);
    add_direction("left", minimum + 5, minimum + 7, 0);
}

void add_stick_controls(Layout& layout, const Element& xElement,
                        const Element& yElement, std::string name,
                        std::unordered_set<std::string>& used,
                        uint32_t stateByteOffset = 0) {
    ControlItem parent;
    parent.name = unique_name(used, std::move(name));
    parent.layout = "Stick";
    parent.format = kFormatVector2;
    parent.sizeInBits = 64;
    parent.offset = stateByteOffset + xElement.reportOffsetInBits / 8u;
    parent.bit = xElement.reportOffsetInBits % 8u;
    parent.usages.push_back("Stick");
    layout.controls.push_back(parent);

    ControlItem x =
        make_axis(xElement, parent.name + "/x", stateByteOffset);
    x.displayName = "x";
    layout.controls.push_back(std::move(x));
    ControlItem y =
        make_axis(yElement, parent.name + "/y", stateByteOffset);
    y.displayName = "y";
    y.invert = true;
    layout.controls.push_back(std::move(y));
}

void add_semantic_axis(Layout& layout, const Element& element,
                       std::string name, std::unordered_set<std::string>& used,
                       uint32_t stateByteOffset = 0) {
    ControlItem item = make_axis(element, unique_name(used, std::move(name)),
                                 stateByteOffset);
    item.displayName = item.name;
    item.invert = false;
    if (item.normalizeMin >= 0.0f)
        item.normalizeZero = item.normalizeMin;
    layout.controls.push_back(std::move(item));
}

void add_semantic_button(Layout& layout, const Element& element,
                         std::string name, std::unordered_set<std::string>& used,
                         uint32_t stateByteOffset = 0) {
    ControlItem item = make_button(element, unique_name(used, std::move(name)),
                                   stateByteOffset);
    item.displayName = item.name;
    layout.controls.push_back(std::move(item));
}

const Element* find_input_element(const DeviceDescriptor& descriptor,
                                  const ReportInfo& report, UsagePage page,
                                  uint32_t usage) {
    for (const Element& candidate : descriptor.elements) {
        if (candidate.reportType != ReportType::Input ||
            candidate.reportId != report.id || candidate.is_constant() ||
            candidate.is_array())
            continue;
        if (candidate.usagePage == page && candidate.usage == usage)
            return &candidate;
    }
    return nullptr;
}

std::optional<uint32_t> first_button_offset(const DeviceDescriptor& descriptor,
                                            const ReportInfo& report) {
    std::optional<uint32_t> result;
    for (const Element& element : descriptor.elements) {
        if (element.reportType != ReportType::Input ||
            element.reportId != report.id || element.is_constant() ||
            element.is_array() || element.usagePage != UsagePage::Button)
            continue;
        if (!result || element.reportOffsetInBits < *result)
            result = element.reportOffsetInBits;
    }
    return result;
}

bool before_button(const Element* element,
                   const std::optional<uint32_t>& buttonOffset) {
    return element != nullptr && buttonOffset.has_value() &&
           element->reportOffsetInBits < *buttonOffset;
}

void add_report_controls(Layout& layout, const DeviceDescriptor& descriptor,
                         const ReportInfo& report, uint32_t stateByteOffset,
                         bool isGamepad, bool isJoystick,
                         std::unordered_set<std::string>& used) {
    const auto findElement = [&](UsagePage page, uint32_t usage) {
        return find_input_element(descriptor, report, page, usage);
    };

    if (isGamepad || isJoystick) {
        const Element* x = findElement(UsagePage::GenericDesktop, 0x30);
        const Element* y = findElement(UsagePage::GenericDesktop, 0x31);
        const Element* z = findElement(UsagePage::GenericDesktop, 0x32);
        const Element* rx = findElement(UsagePage::GenericDesktop, 0x33);
        const Element* ry = findElement(UsagePage::GenericDesktop, 0x34);
        const Element* rz = findElement(UsagePage::GenericDesktop, 0x35);
        const Element* hat = findElement(UsagePage::GenericDesktop, 0x39);
        const Element* slider = findElement(UsagePage::GenericDesktop, 0x36);

        if (isGamepad) {
            if (x != nullptr && y != nullptr)
                add_stick_controls(layout, *x, *y, "leftStick", used,
                                   stateByteOffset);

            const Element* rightX = nullptr;
            const Element* rightY = nullptr;
            const Element* leftTrigger = nullptr;
            const Element* rightTrigger = nullptr;
            const bool haveZRz = z != nullptr && rz != nullptr;
            const bool haveRxRy = rx != nullptr && ry != nullptr;
            const std::optional<uint32_t> firstButton =
                first_button_offset(descriptor, report);

            if (haveZRz && haveRxRy) {
                const bool zPairBefore = before_button(z, firstButton) &&
                                         before_button(rz, firstButton);
                const bool rxPairBefore = before_button(rx, firstButton) &&
                                          before_button(ry, firstButton);
                const bool zPairSigned = z->is_signed() || rz->is_signed();
                const bool rxPairSigned = rx->is_signed() || ry->is_signed();

                bool useZRzForStick = false;
                if (zPairBefore != rxPairBefore) {
                    useZRzForStick = zPairBefore;
                } else if (zPairSigned != rxPairSigned) {
                    useZRzForStick = zPairSigned;
                } else {
                    useZRzForStick =
                        std::min(z->reportOffsetInBits, rz->reportOffsetInBits) <
                        std::min(rx->reportOffsetInBits, ry->reportOffsetInBits);
                }

                if (useZRzForStick) {
                    rightX = z;
                    rightY = rz;
                    leftTrigger = rx;
                    rightTrigger = ry;
                } else {
                    rightX = rx;
                    rightY = ry;
                    leftTrigger = z;
                    rightTrigger = rz;
                }
            } else if (haveRxRy) {
                rightX = rx;
                rightY = ry;
            } else if (haveZRz) {
                if (z->is_signed() || rz->is_signed()) {
                    rightX = z;
                    rightY = rz;
                } else {
                    leftTrigger = z;
                    rightTrigger = rz;
                }
            } else {
                if (z != nullptr && !z->is_signed())
                    leftTrigger = z;
                if (rz != nullptr && !rz->is_signed())
                    rightTrigger = rz;
            }

            if (rightX != nullptr && rightY != nullptr)
                add_stick_controls(layout, *rightX, *rightY, "rightStick", used,
                                   stateByteOffset);

            if (leftTrigger == nullptr && slider != nullptr)
                leftTrigger = slider;

            if (leftTrigger != nullptr)
                add_semantic_axis(layout, *leftTrigger, "leftTrigger", used,
                                  stateByteOffset);
            if (rightTrigger != nullptr)
                add_semantic_axis(layout, *rightTrigger, "rightTrigger", used,
                                  stateByteOffset);

            if (hat != nullptr)
                add_hat_controls(layout, *hat, used, "dpad", "Dpad",
                                 stateByteOffset);

            static constexpr struct {
                uint32_t usage;
                const char* name;
            } kButtons[] = {
                {1, "buttonSouth"}, {2, "buttonEast"}, {3, "buttonWest"},
                {4, "buttonNorth"}, {5, "leftShoulder"}, {6, "rightShoulder"},
                {7, "select"},      {8, "start"},       {9, "leftStickButton"},
                {10, "rightStickButton"},
            };
            for (const auto& spec : kButtons) {
                if (const Element* element =
                        findElement(UsagePage::Button, spec.usage)) {
                    add_semantic_button(layout, *element, spec.name, used,
                                        stateByteOffset);
                }
            }
        } else {
            if (x != nullptr && y != nullptr)
                add_stick_controls(layout, *x, *y, "stick", used,
                                   stateByteOffset);
            if (z != nullptr)
                add_semantic_axis(layout, *z, "trigger", used,
                                  stateByteOffset);
            if (hat != nullptr)
                add_hat_controls(layout, *hat, used, "dpad", "Dpad",
                                 stateByteOffset);
        }
    }

    for (const Element& element : descriptor.elements) {
        if (element.reportType != ReportType::Input ||
            element.reportId != report.id || element.is_constant() ||
            element.is_array())
            continue;

        if (element.usagePage == UsagePage::GenericDesktop &&
            element.usage == 0x39) {
            add_hat_controls(layout, element, used, "hat", "Axis",
                             stateByteOffset);
            continue;
        }

        if (is_button_usage(element.usagePage, element.usage)) {
            const std::string name = unique_name(
                used, usage_name(element.usagePage, element.usage));
            layout.controls.push_back(
                make_button(element, name, stateByteOffset));
            continue;
        }

        if (is_axis_usage(element.usagePage, element.usage)) {
            const std::string name = unique_name(
                used, usage_name(element.usagePage, element.usage));
            layout.controls.push_back(make_axis(element, name, stateByteOffset));
        }
    }
}

} // namespace

std::unique_ptr<Layout> build_generic_layout(
    const DeviceDescriptor& descriptor,
    std::span<const GenericReportLayout> reports, std::string_view layoutName,
    uint32_t stateSizeBytes) {
    if (reports.empty() || stateSizeBytes == 0)
        return nullptr;

    bool hasReport = false;
    for (const GenericReportLayout& entry : reports) {
        if (entry.report != nullptr && entry.report->type == ReportType::Input &&
            entry.report->sizeInBits != 0) {
            hasReport = true;
            break;
        }
    }
    if (!hasReport)
        return nullptr;

    auto layout = std::make_unique<Layout>();
    layout->name = std::string(layoutName);
    layout->displayName = "HID";
    layout->stateFormat = kFormatHid;
    layout->stateSizeInBits = stateSizeBytes * 8u;
    layout->factory = [] { return std::make_unique<Device>(); };

    const bool isGamepad = descriptor.usagePage == UsagePage::GenericDesktop &&
                           descriptor.usage == 0x05;
    const bool isJoystick = descriptor.usagePage == UsagePage::GenericDesktop &&
                            descriptor.usage == 0x04;
    if (isGamepad || isJoystick) {
        layout->displayName = isGamepad ? "HID Gamepad" : "HID Joystick";
        layout->factory = isGamepad
                              ? [] { return std::unique_ptr<Control>(
                                         std::make_unique<Gamepad>()); }
                              : [] { return std::unique_ptr<Control>(
                                         std::make_unique<Joystick>()); };
    }

    std::unordered_set<std::string> used;
    for (const GenericReportLayout& entry : reports) {
        if (entry.report == nullptr || entry.report->type != ReportType::Input ||
            entry.report->sizeInBits == 0)
            continue;
        add_report_controls(*layout, descriptor, *entry.report,
                            entry.stateByteOffset, isGamepad, isJoystick, used);
    }

    return layout;
}

std::unique_ptr<Layout> build_generic_layout(const DeviceDescriptor& descriptor,
                                             const ReportInfo& report,
                                             std::string_view layoutName) {
    if (report.type != ReportType::Input || report.sizeInBits == 0)
        return nullptr;
    const GenericReportLayout entry{&report, 0};
    return build_generic_layout(
        descriptor, std::span<const GenericReportLayout>(&entry, 1),
        layoutName, report.size_in_bytes());
}

} // namespace insys::hid






