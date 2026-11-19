#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "insys/control_layout.h"
#include "insys/device.h"
#include "insys/layout_registry.h"

namespace insys {

class DeviceBuilder {
public:
    explicit DeviceBuilder(LayoutRegistry& registry);

    std::unique_ptr<Device> build(std::string_view layoutName, std::string_view variants,
                                  int deviceId, const DeviceDescription& description);

private:
    void add_child_controls(Layout& layout, Control* parent);
    Control* add_control(const ControlItem& item, Control* parent, const std::string& name);
    void compute_state_layout(Control* control);
    void apply_use_state_from(Device* device);
    Control* find_child(Control* parent, std::string_view name) const;

    LayoutRegistry* m_registry = nullptr;
    Device* m_device = nullptr;
    std::unordered_set<Control*> m_useStateFromControls;
    std::vector<std::pair<Control*, std::string>> m_useStateFromRequests;
};

} // namespace insys
