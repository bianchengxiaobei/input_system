#pragma once

#include <string_view>
#include <vector>

#include "insys/control_layout.h"

namespace insys {

NameAndParameters parse_item(std::string_view text);
std::vector<NameAndParameters> parse_list(std::string_view text);

} // namespace insys
