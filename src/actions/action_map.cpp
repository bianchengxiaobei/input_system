#include "insys/actions/action_map.h"

#include <stdexcept>
#include <utility>

#include "insys/actions/action_state.h"
#include "insys/manager.h"
#include "insys/string_utils.h"

namespace insys {

ActionMap::ActionMap() = default;

ActionMap::ActionMap(std::string mapName) : name(std::move(mapName)) {}

ActionMap::ActionMap(ActionMap&& other) noexcept
    : name(std::move(other.name)),
      id(other.id),
      actions(std::move(other.actions)),
      bindings(std::move(other.bindings)),
      mapIndex(other.mapIndex),
      m_state(other.m_ownedState.get()),
      m_ownedState(std::move(other.m_ownedState)),
      m_enabledActions(other.m_enabledActions) {
    other.m_state = nullptr;
    other.m_enabledActions = 0;
    other.mapIndex = -1;
}

ActionMap& ActionMap::operator=(ActionMap&& other) noexcept {
    if (this == &other)
        return *this;

    name = std::move(other.name);
    id = other.id;
    actions = std::move(other.actions);
    bindings = std::move(other.bindings);
    mapIndex = other.mapIndex;
    m_ownedState = std::move(other.m_ownedState);
    m_state = m_ownedState ? m_ownedState.get() : other.m_state;
    m_enabledActions = other.m_enabledActions;

    other.m_state = nullptr;
    other.m_enabledActions = 0;
    other.mapIndex = -1;
    return *this;
}

ActionMap::~ActionMap() {
    if (m_state != nullptr)
        disable();
}

void ActionMap::enable(Manager& manager) {
    if (m_state == nullptr) {
        m_ownedState = ActionState::create(manager, {this});
        m_state = m_ownedState.get();
    }
    if (m_state != nullptr)
        m_state->enable_all(*this);
}

void ActionMap::disable() {
    if (m_state != nullptr)
        m_state->disable_all(*this);
}

ActionDef* ActionMap::find_action(std::string_view nameOrId, bool throwIfNotFound) const {
    for (const ActionDef& action : actions) {
        if (equals_ignore_case(action.name, nameOrId) ||
            action.id.to_string() == nameOrId) {
            return const_cast<ActionDef*>(&action);
        }
    }
    if (throwIfNotFound)
        throw std::runtime_error("Action not found: " + std::string(nameOrId));
    return nullptr;
}

ActionDef& ActionMap::add_action(std::string actionName, ActionType type) {
    ActionDef action;
    action.name = std::move(actionName);
    action.type = type;
    actions.push_back(std::move(action));
    return actions.back();
}

Binding& ActionMap::add_binding(std::string actionName, std::string path,
                                std::string bindingName) {
    Binding binding;
    binding.action = std::move(actionName);
    binding.path = std::move(path);
    binding.name = std::move(bindingName);
    bindings.push_back(std::move(binding));
    return bindings.back();
}

Binding& ActionMap::add_composite_binding(std::string actionName, std::string compositeName,
                                          std::string bindingName) {
    Binding binding;
    binding.action = std::move(actionName);
    binding.path = std::move(compositeName);
    binding.name = std::move(bindingName);
    binding.isComposite = true;
    bindings.push_back(std::move(binding));
    return bindings.back();
}

Binding& ActionMap::add_composite_part(std::string actionName, std::string partName,
                                       std::string path) {
    if (bindings.empty() || (!bindings.back().isComposite &&
                             !bindings.back().isPartOfComposite))
        throw std::runtime_error("Composite part added without a preceding composite binding");

    Binding binding;
    binding.action = std::move(actionName);
    binding.path = std::move(path);
    binding.name = std::move(partName);
    binding.isPartOfComposite = true;
    bindings.push_back(std::move(binding));
    return bindings.back();
}

} // namespace insys
