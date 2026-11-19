#pragma once

#include <string>
#include <string_view>

namespace insys {

std::string to_lower(std::string_view text);
bool equals_ignore_case(std::string_view a, std::string_view b);
bool starts_with_ignore_case(std::string_view text, std::string_view prefix);

} // namespace insys
