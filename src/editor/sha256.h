#pragma once

#include <string>
#include <string_view>

namespace insys::editor::detail {

std::string sha256_hex(std::string_view data);

} // namespace insys::editor::detail
