#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "insys/control.h"
#include "insys/interned_string.h"

namespace insys {

struct PathComponent {
    InternedString name;
    InternedString layout;
    std::vector<InternedString> usages;
    bool wildcard = false;
    bool isRoot = false;
};

std::vector<PathComponent> parse_path(std::string_view path);
bool path_matches(std::string_view path, const Control& control);
Control* find_control(Control& root, std::string_view path);
std::string combine_path(const Control* parent, std::string_view name);
std::string control_path(const Control& control);
std::string to_display_string(std::string_view path);

} // namespace insys
