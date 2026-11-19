#pragma once

#include <string>
#include <string_view>

#include "insys/guid.h"

namespace insys {

struct Binding {
    std::string name;
    Guid id = Guid::create();
    std::string path;
    std::string overridePath;
    std::string action;
    std::string interactions;
    std::string processors;
    std::string groups;
    std::string overrideInteractions;
    std::string overrideProcessors;
    bool isComposite = false;
    bool isPartOfComposite = false;
    int partIndex = -1;

    std::string_view effective_path() const {
        return overridePath.empty() ? std::string_view(path) : std::string_view(overridePath);
    }
    std::string_view effective_interactions() const {
        return overrideInteractions.empty() ? std::string_view(interactions)
                                            : std::string_view(overrideInteractions);
    }
    std::string_view effective_processors() const {
        return overrideProcessors.empty() ? std::string_view(processors)
                                          : std::string_view(overrideProcessors);
    }
};

} // namespace insys
