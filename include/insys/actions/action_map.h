#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "insys/actions/action.h"
#include "insys/actions/binding.h"
#include "insys/guid.h"

namespace insys {

class ActionState;
class Manager;

class ActionMap {
public:
    ActionMap();
    explicit ActionMap(std::string mapName);
    ~ActionMap();

    ActionMap(const ActionMap&) = delete;
    ActionMap& operator=(const ActionMap&) = delete;
    ActionMap(ActionMap&& other) noexcept;
    ActionMap& operator=(ActionMap&& other) noexcept;

    std::string name;
    Guid id = Guid::create();
    std::vector<ActionDef> actions;
    std::vector<Binding> bindings;
    int mapIndex = -1;

    void enable(Manager& manager);
    void disable();
    bool enabled() const { return m_enabledActions > 0; }

    ActionDef* find_action(std::string_view nameOrId, bool throwIfNotFound = false) const;
    ActionDef* operator[](std::string_view actionName) { return find_action(actionName, true); }

    ActionDef& add_action(std::string actionName, ActionType type = ActionType::Button);
    Binding& add_binding(std::string actionName, std::string path, std::string bindingName = {});
    Binding& add_composite_binding(std::string actionName, std::string compositeName,
                                   std::string bindingName = {});
    Binding& add_composite_part(std::string actionName, std::string partName, std::string path);

    ActionState* state() const { return m_state; }

private:
    ActionState* m_state = nullptr;
    std::unique_ptr<ActionState> m_ownedState;
    int m_enabledActions = 0;

    void set_state(ActionState* state) { m_state = state; }

    friend class ActionState;
    friend class ActionAsset;
};

} // namespace insys
