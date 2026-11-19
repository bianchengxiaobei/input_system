#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace insys::hid {

enum class UsagePage : uint16_t {
    Undefined = 0x00,
    GenericDesktop = 0x01,
    Simulation = 0x02,
    VRControls = 0x03,
    SportControls = 0x04,
    GameControls = 0x05,
    GenericDeviceControls = 0x06,
    Keyboard = 0x07,
    LEDs = 0x08,
    Button = 0x09,
    Ordinal = 0x0A,
    Telephony = 0x0B,
    Consumer = 0x0C,
    Digitizer = 0x0D,
    PID = 0x0F,
    Unicode = 0x10,
    AlphanumericDisplay = 0x14,
    MedicalInstruments = 0x40,
    Monitor = 0x80,
    Power = 0x84,
    BarCodeScanner = 0x8C,
    MagneticStripeReader = 0x8E,
    Camera = 0x90,
    Arcade = 0x91,
    VendorDefined = 0xFF00,
};

enum class ReportType : uint8_t {
    Input,
    Output,
    Feature,
};

enum class CollectionType : uint8_t {
    Physical = 0x00,
    Application = 0x01,
    Logical = 0x02,
    Report = 0x03,
    NamedArray = 0x04,
    UsageSwitch = 0x05,
    UsageModifier = 0x06,
};

enum ElementFlag : uint32_t {
    kElementConstant = 1u << 0,
    kElementVariable = 1u << 1,
    kElementRelative = 1u << 2,
    kElementWrap = 1u << 3,
    kElementNonLinear = 1u << 4,
    kElementNoPreferred = 1u << 5,
    kElementNullState = 1u << 6,
    kElementVolatile = 1u << 7,
    kElementBufferedBytes = 1u << 8,
};

struct Element {
    uint32_t usage = 0;
    UsagePage usagePage = UsagePage::Undefined;
    int32_t logicalMin = 0;
    int32_t logicalMax = 0;
    int32_t physicalMin = 0;
    int32_t physicalMax = 0;
    int32_t unit = 0;
    int32_t unitExponent = 0;
    ReportType reportType = ReportType::Input;
    int collectionIndex = -1;
    uint8_t reportId = 1;
    uint16_t reportSizeInBits = 0;
    uint32_t reportOffsetInBits = 0;
    uint32_t flags = 0;

    bool is_constant() const { return (flags & kElementConstant) != 0; }
    bool is_variable() const { return (flags & kElementVariable) != 0; }
    bool is_array() const { return !is_variable(); }
    bool is_relative() const { return (flags & kElementRelative) != 0; }
    bool has_null_state() const { return (flags & kElementNullState) != 0; }
    bool is_signed() const { return logicalMin < 0; }
};

struct Collection {
    CollectionType type = CollectionType::Physical;
    uint32_t usage = 0;
    UsagePage usagePage = UsagePage::Undefined;
    int parent = -1;
    int childCount = 0;
    int firstChild = 0;
};

struct ReportInfo {
    uint8_t id = 1;
    bool hasId = false;
    ReportType type = ReportType::Input;
    uint32_t sizeInBits = 0;

    uint32_t size_in_bytes() const {
        return (sizeInBits + 7u) / 8u;
    }
};

struct DeviceDescriptor {
    uint32_t vendorId = 0;
    uint32_t productId = 0;
    uint32_t usage = 0;
    UsagePage usagePage = UsagePage::Undefined;
    uint32_t inputReportSize = 0;
    uint32_t outputReportSize = 0;
    uint32_t featureReportSize = 0;
    std::vector<Element> elements;
    std::vector<Collection> collections;
    std::vector<ReportInfo> reports;

    const ReportInfo* primary_input_report() const;
};

bool parse_report_descriptor(const uint8_t* data, std::size_t size,
                             DeviceDescriptor& out);

} // namespace insys::hid
