#pragma once

#include <vector>

#include "insys/actions/resolved_bindings.h"

namespace insys {

class ActionMap;
class Device;
class Manager;
struct Binding;

class BindingResolver {
public:
    explicit BindingResolver(Manager& manager) : m_manager(&manager) {}

    BindingResolver(Manager& manager, const std::vector<Device*>* devices,
                    const Binding* bindingMask = nullptr)
        : m_manager(&manager), m_devices(devices), m_bindingMask(bindingMask) {}

    ResolvedBindings resolve(const std::vector<ActionMap*>& maps);

private:
    Manager* m_manager = nullptr;
    const std::vector<Device*>* m_devices = nullptr;
    const Binding* m_bindingMask = nullptr;
};

} // namespace insys
