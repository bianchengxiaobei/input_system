#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "insys/actions/action_map.h"
#include "insys/actions/binding.h"
#include "insys/actions/control_scheme.h"

namespace insys {

class ActionState;
class Device;
class Manager;

class ActionAsset {
private:
    std::unique_ptr<ActionState> m_state;

public:
    ActionAsset();
    explicit ActionAsset(std::string assetName);
    ~ActionAsset();

    ActionAsset(const ActionAsset&) = delete;
    ActionAsset& operator=(const ActionAsset&) = delete;
    ActionAsset(ActionAsset&& other) noexcept;
    ActionAsset& operator=(ActionAsset&& other) noexcept;

    std::string name;
    std::vector<ActionMap> maps;
    std::vector<ControlScheme> controlSchemes;
    std::optional<std::vector<Device*>> devices;
    std::optional<Binding> bindingMask;

    void enable(Manager& manager);
    void disable();
    bool re_resolve();
    ActionAsset clone() const;

    ActionMap& add_map(std::string mapName);
    ActionMap* find_map(std::string_view nameOrId, bool throwIfNotFound = false);
    ActionDef* find_action(std::string_view nameOrId, bool throwIfNotFound = false);

    ActionState* state() const { return m_state.get(); }
};

} // namespace insys

