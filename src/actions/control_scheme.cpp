#include "insys/actions/control_scheme.h"

#include <algorithm>

#include "insys/device.h"
#include "insys/string_utils.h"

namespace insys {

ControlSchemeMatch match_control_scheme(const ControlScheme& scheme,
                                        const std::vector<Device*>& available) {
    ControlSchemeMatch result;
    result.scheme = scheme;
    result.matched = true;
    result.devices.reserve(scheme.devices.size());

    for (const std::string& layout : scheme.devices) {
        Device* matched = nullptr;
        for (Device* device : available) {
            if (device == nullptr)
                continue;
            if (std::find(result.devices.begin(), result.devices.end(), device) !=
                result.devices.end())
                continue;
            bool layoutMatches = equals_ignore_case(device->layout.str(), layout);
            if (!layoutMatches) {
                for (const std::string& candidate : device->layoutLineage) {
                    if (equals_ignore_case(candidate, layout)) {
                        layoutMatches = true;
                        break;
                    }
                }
            }
            if (layoutMatches) {
                matched = device;
                break;
            }
        }
        if (matched != nullptr) {
            result.devices.push_back(matched);
            ++result.score;
        } else {
            result.missingDevices.push_back(layout);
        }
    }
    return result;
}

bool scheme_matches(const ControlScheme& scheme, const std::vector<Device*>& available) {
    return match_control_scheme(scheme, available).successful();
}

std::optional<ControlSchemeMatch> find_control_scheme(
    const std::vector<ControlScheme>& schemes, const std::vector<Device*>& available) {
    std::optional<ControlSchemeMatch> best;
    for (int pass = 0; pass < 2; ++pass) {
        for (const ControlScheme& scheme : schemes) {
            if (scheme.isOptional != (pass == 1))
                continue;

            ControlSchemeMatch match = match_control_scheme(scheme, available);
            if (!match.successful())
                continue;
            if (!best.has_value() || match.score > best->score)
                best = std::move(match);
        }
        if (best.has_value())
            return best;
    }
    return std::nullopt;
}

} // namespace insys


