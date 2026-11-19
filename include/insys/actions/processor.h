#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "insys/control_layout.h"
#include "insys/processor.h"

namespace insys {

void apply_parameters(IProcessor& processor, const std::vector<NamedValue>& parameters);
std::unique_ptr<IProcessor> make_processor(std::string_view name,
                                           const std::vector<NamedValue>& parameters);

} // namespace insys
