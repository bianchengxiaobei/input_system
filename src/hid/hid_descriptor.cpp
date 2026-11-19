#include "insys/hid/hid_descriptor.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace insys::hid {
namespace {

constexpr uint8_t kInput = 0x80;
constexpr uint8_t kOutput = 0x90;
constexpr uint8_t kFeature = 0xB0;
constexpr uint8_t kCollection = 0xA0;
constexpr uint8_t kEndCollection = 0xC0;
constexpr uint8_t kUsagePage = 0x04;
constexpr uint8_t kLogicalMinimum = 0x14;
constexpr uint8_t kLogicalMaximum = 0x24;
constexpr uint8_t kPhysicalMinimum = 0x34;
constexpr uint8_t kPhysicalMaximum = 0x44;
constexpr uint8_t kUnitExponent = 0x54;
constexpr uint8_t kUnit = 0x64;
constexpr uint8_t kReportSize = 0x74;
constexpr uint8_t kReportId = 0x84;
constexpr uint8_t kReportCount = 0x94;
constexpr uint8_t kPush = 0xA4;
constexpr uint8_t kPop = 0xB4;
constexpr uint8_t kUsage = 0x08;
constexpr uint8_t kUsageMinimum = 0x18;
constexpr uint8_t kUsageMaximum = 0x28;

struct LocalState {
    std::optional<uint32_t> usage;
    std::optional<uint32_t> usageMinimum;
    std::optional<uint32_t> usageMaximum;
    std::vector<uint32_t> usageList;

    void set_usage(uint32_t value) {
        if (usage)
            usageList.push_back(*usage);
        usage = value;
    }

    uint32_t get_usage(int index) const {
        if (usageMinimum && usageMaximum) {
            const uint32_t minimum = *usageMinimum;
            const uint32_t maximum = *usageMaximum;
            const uint32_t range = maximum >= minimum ? maximum - minimum : 0u;
            if (index < 0 || static_cast<uint32_t>(index) >= range)
                return maximum;
            return minimum + static_cast<uint32_t>(index);
        }

        if (!usageList.empty()) {
            if (index >= 0 && static_cast<std::size_t>(index) < usageList.size())
                return usageList[static_cast<std::size_t>(index)];
            return usage.value_or(0u);
        }

        return usage.value_or(0u);
    }

    void reset() {
        usage.reset();
        usageMinimum.reset();
        usageMaximum.reset();
        usageList.clear();
    }
};

struct GlobalState {
    std::optional<uint32_t> usagePage;
    std::optional<int32_t> logicalMinimum;
    std::optional<int32_t> logicalMaximum;
    std::optional<int32_t> physicalMinimum;
    std::optional<int32_t> physicalMaximum;
    std::optional<int32_t> unitExponent;
    std::optional<uint32_t> unit;
    std::optional<uint32_t> reportSize;
    std::optional<uint32_t> reportCount;
    std::optional<uint32_t> reportId;

    UsagePage get_usage_page(int index, const LocalState& local) const {
        if (usagePage)
            return static_cast<UsagePage>(static_cast<uint16_t>(*usagePage));
        return static_cast<UsagePage>(static_cast<uint16_t>(local.get_usage(index) >> 16));
    }
};

struct InternalReport {
    uint8_t id = 1;
    bool hasId = false;
    ReportType type = ReportType::Input;
    uint32_t currentBitOffset = 0;
};

uint32_t read_raw(const uint8_t* data, int itemSize) {
    uint32_t value = 0;
    if (itemSize == 1) {
        value = data[0];
    } else if (itemSize == 2) {
        value = static_cast<uint32_t>(data[0]) |
                (static_cast<uint32_t>(data[1]) << 8u);
    } else if (itemSize == 4) {
        value = static_cast<uint32_t>(data[0]) |
                (static_cast<uint32_t>(data[1]) << 8u) |
                (static_cast<uint32_t>(data[2]) << 16u) |
                (static_cast<uint32_t>(data[3]) << 24u);
    }
    return value;
}

int32_t read_signed(const uint8_t* data, int itemSize) {
    const uint32_t raw = read_raw(data, itemSize);
    if (itemSize == 1)
        return static_cast<int8_t>(raw);
    if (itemSize == 2)
        return static_cast<int16_t>(raw);
    if (itemSize == 4)
        return static_cast<int32_t>(raw);
    return 0;
}

std::size_t find_or_add_report(std::vector<InternalReport>& reports,
                               std::optional<uint32_t> reportId, ReportType type) {
    const uint8_t id = static_cast<uint8_t>(reportId.value_or(1u));
    const bool hasId = reportId.has_value();
    for (std::size_t i = 0; i < reports.size(); ++i) {
        if (reports[i].id == id && reports[i].type == type) {
            if (hasId)
                reports[i].hasId = true;
            return i;
        }
    }
    InternalReport report;
    report.id = id;
    report.hasId = hasId;
    report.type = type;
    report.currentBitOffset = hasId ? 8u : 0u;
    reports.push_back(report);
    return reports.size() - 1;
}

} // namespace

const ReportInfo* DeviceDescriptor::primary_input_report() const {
    for (const ReportInfo& report : reports) {
        if (report.type == ReportType::Input)
            return &report;
    }
    return nullptr;
}

bool parse_report_descriptor(const uint8_t* data, std::size_t size,
                             DeviceDescriptor& out) {
    if (data == nullptr || size == 0)
        return false;

    DeviceDescriptor result;
    std::vector<Element> elements;
    std::vector<Collection> collections;
    std::vector<InternalReport> internalReports;
    std::vector<GlobalState> globalStack;

    LocalState local;
    GlobalState global;
    int currentCollection = -1;

    std::size_t offset = 0;
    while (offset < size) {
        const uint8_t first = data[offset++];

        if (first == 0xFE) {
            if (offset + 2 > size)
                return false;
            const uint8_t dataSize = data[offset++];
            ++offset;
            if (offset + dataSize > size)
                return false;
            offset += dataSize;
            continue;
        }

        const int itemSizeCode = first & 0x03;
        const int itemSize = itemSizeCode == 3 ? 4 : itemSizeCode;
        const uint8_t item = first & 0xFC;

        if (offset + static_cast<std::size_t>(itemSize) > size)
            return false;
        const uint8_t* itemData = data + offset;
        offset += static_cast<std::size_t>(itemSize);

        switch (item) {
        case kUsagePage:
            global.usagePage = read_raw(itemData, itemSize);
            break;
        case kLogicalMinimum:
            global.logicalMinimum = read_signed(itemData, itemSize);
            break;
        case kLogicalMaximum:
            global.logicalMaximum = read_signed(itemData, itemSize);
            break;
        case kPhysicalMinimum:
            global.physicalMinimum = read_signed(itemData, itemSize);
            break;
        case kPhysicalMaximum:
            global.physicalMaximum = read_signed(itemData, itemSize);
            break;
        case kUnitExponent:
            global.unitExponent = read_signed(itemData, itemSize);
            break;
        case kUnit:
            global.unit = read_raw(itemData, itemSize);
            break;
        case kReportSize:
            global.reportSize = read_raw(itemData, itemSize);
            break;
        case kReportId:
            global.reportId = read_raw(itemData, itemSize);
            break;
        case kReportCount:
            global.reportCount = read_raw(itemData, itemSize);
            break;
        case kPush:
            globalStack.push_back(global);
            break;
        case kPop:
            if (!globalStack.empty()) {
                global = globalStack.back();
                globalStack.pop_back();
            }
            break;
        case kUsage:
            local.set_usage(read_raw(itemData, itemSize));
            break;
        case kUsageMinimum:
            local.usageMinimum = read_raw(itemData, itemSize);
            break;
        case kUsageMaximum:
            local.usageMaximum = read_raw(itemData, itemSize);
            break;
        case kCollection: {
            const int parent = currentCollection;
            currentCollection = static_cast<int>(collections.size());
            Collection collection;
            collection.type = static_cast<CollectionType>(read_raw(itemData, itemSize));
            collection.parent = parent;
            collection.usagePage = global.get_usage_page(0, local);
            collection.usage = local.get_usage(0) & 0xFFFFu;
            collection.firstChild = static_cast<int>(elements.size());
            collections.push_back(collection);
            local.reset();
            break;
        }
        case kEndCollection: {
            if (currentCollection < 0 || currentCollection >= static_cast<int>(collections.size()))
                return false;
            Collection& collection = collections[static_cast<std::size_t>(currentCollection)];
            collection.childCount = static_cast<int>(elements.size()) - collection.firstChild;
            currentCollection = collection.parent;
            local.reset();
            break;
        }
        case kInput:
        case kOutput:
        case kFeature: {
            ReportType reportType = ReportType::Input;
            if (item == kOutput)
                reportType = ReportType::Output;
            else if (item == kFeature)
                reportType = ReportType::Feature;

            const std::size_t reportIndex =
                find_or_add_report(internalReports, global.reportId, reportType);
            InternalReport& report = internalReports[reportIndex];

            const uint32_t reportCount = global.reportCount.value_or(1u);
            const uint32_t reportSize = global.reportSize.value_or(8u);
            const uint32_t flags = read_raw(itemData, itemSize);
            const uint8_t reportId = static_cast<uint8_t>(global.reportId.value_or(1u));

            for (uint32_t i = 0; i < reportCount; ++i) {
                Element element;
                const uint32_t usageValue = local.get_usage(static_cast<int>(i));
                element.usage = usageValue & 0xFFFFu;
                element.usagePage = global.get_usage_page(static_cast<int>(i), local);
                element.logicalMin = global.logicalMinimum.value_or(0);
                element.logicalMax = global.logicalMaximum.value_or(0);
                const bool hasPhysicalRange =
                    global.physicalMinimum.has_value() &&
                    global.physicalMaximum.has_value() &&
                    !(*global.physicalMinimum == 0 && *global.physicalMaximum == 0);
                element.physicalMin = hasPhysicalRange
                                          ? *global.physicalMinimum
                                          : element.logicalMin;
                element.physicalMax = hasPhysicalRange
                                          ? *global.physicalMaximum
                                          : element.logicalMax;
                element.unit = static_cast<int32_t>(global.unit.value_or(0));
                element.unitExponent = global.unitExponent.value_or(0);
                element.reportType = reportType;
                element.collectionIndex = currentCollection;
                element.reportId = reportId;
                element.reportSizeInBits = static_cast<uint16_t>(reportSize);
                element.reportOffsetInBits = report.currentBitOffset;
                element.flags = flags;
                report.currentBitOffset += reportSize;
                elements.push_back(element);
            }

            local.reset();
            break;
        }
        default:
            break;
        }
    }

    for (const Collection& collection : collections) {
        if (collection.parent == -1 && collection.type == CollectionType::Application) {
            result.usage = collection.usage;
            result.usagePage = collection.usagePage;
            break;
        }
    }

    for (const InternalReport& internal : internalReports) {
        ReportInfo report;
        report.id = internal.id;
        report.hasId = internal.hasId;
        report.type = internal.type;
        report.sizeInBits = internal.currentBitOffset;
        result.reports.push_back(report);

        const uint32_t bytes = (report.sizeInBits + 7u) / 8u;
        if (report.type == ReportType::Input)
            result.inputReportSize = std::max(result.inputReportSize, bytes);
        else if (report.type == ReportType::Output)
            result.outputReportSize = std::max(result.outputReportSize, bytes);
        else
            result.featureReportSize = std::max(result.featureReportSize, bytes);
    }

    result.elements = std::move(elements);
    result.collections = std::move(collections);
    out = std::move(result);
    return true;
}

} // namespace insys::hid



