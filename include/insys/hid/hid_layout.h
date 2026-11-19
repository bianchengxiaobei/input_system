#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "insys/control_layout.h"
#include "insys/hid/hid_descriptor.h"

namespace insys::hid {

struct GenericReportLayout {
    const ReportInfo* report = nullptr;
    uint32_t stateByteOffset = 0;
};

std::unique_ptr<Layout> build_generic_layout(const DeviceDescriptor& descriptor,
                                             const ReportInfo& report,
                                             std::string_view layoutName);

std::unique_ptr<Layout> build_generic_layout(
    const DeviceDescriptor& descriptor,
    std::span<const GenericReportLayout> reports, std::string_view layoutName,
    uint32_t stateSizeBytes);

} // namespace insys::hid
