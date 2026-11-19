#pragma once

#include <optional>
#include <string>
#include <vector>

#include "insys/guid.h"

namespace insys {

class Device;

struct ControlScheme {
    std::string name;
    Guid id = Guid::create();
    std::string bindingGroup;
    std::vector<std::string> devices;
    bool isOptional = false;
};

struct ControlSchemeMatch {
    ControlScheme scheme;
    bool matched = false;
    std::vector<Device*> devices;
    std::vector<std::string> missingDevices;
    int score = 0;

    bool successful() const { return matched && missingDevices.empty(); }
};

bool scheme_matches(const ControlScheme& scheme, const std::vector<Device*>& devices);

ControlSchemeMatch match_control_scheme(const ControlScheme& scheme,
                                        const std::vector<Device*>& available);

std::optional<ControlSchemeMatch> find_control_scheme(
    const std::vector<ControlScheme>& schemes, const std::vector<Device*>& available);

} // namespace insys

