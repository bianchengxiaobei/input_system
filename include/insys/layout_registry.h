#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "insys/control_layout.h"
#include "insys/device_description.h"

namespace insys {

class LayoutRegistry {
public:
    Layout* add(std::unique_ptr<Layout> layout);
    Layout* find(std::string_view name);
    const Layout* find(std::string_view name) const;

    void add_matcher(std::string_view layoutName, const DeviceMatcher& matcher);
    std::string find_matching_layout(const DeviceDescription& description) const;

    std::vector<std::string> list() const;

private:
    Layout* resolve(std::string_view name);
    static std::string normalize(std::string_view name);

    std::unordered_map<std::string, std::unique_ptr<Layout>> m_layouts;
    std::vector<std::pair<std::string, DeviceMatcher>> m_matchers;
};

void register_default_layouts(LayoutRegistry& registry);
LayoutRegistry& default_layout_registry();

} // namespace insys
