#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "insys/actions/action_asset.h"

namespace insys {

class BindingOverrideError : public std::runtime_error {
public:
    explicit BindingOverrideError(std::string message) : std::runtime_error(std::move(message)) {}
};

std::string save_binding_overrides(const ActionAsset& asset);
void load_binding_overrides(ActionAsset& asset, std::string_view json,
                            std::vector<std::string>* warnings = nullptr);

} // namespace insys
