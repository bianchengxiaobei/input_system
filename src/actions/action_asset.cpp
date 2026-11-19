#include "insys/actions/action_asset.h"

#include <stdexcept>
#include <utility>

#include "insys/actions/action_state.h"
#include "insys/manager.h"
#include "insys/string_utils.h"

namespace insys {

ActionAsset::ActionAsset() = default;

ActionAsset::ActionAsset(std::string assetName) : name(std::move(assetName)) {}

ActionAsset::ActionAsset(ActionAsset&& other) noexcept
    : m_state(std::move(other.m_state)),
      name(std::move(other.name)),
      maps(std::move(other.maps)),
      controlSchemes(std::move(other.controlSchemes)),
      devices(std::move(other.devices)),
      bindingMask(std::move(other.bindingMask)) {}

ActionAsset& ActionAsset::operator=(ActionAsset&& other) noexcept {
    if (this == &other)
        return *this;

    disable();
    for (ActionMap& map : maps) {
        map.m_state = nullptr;
        map.m_ownedState.reset();
    }
    m_state.reset();

    m_state = std::move(other.m_state);
    name = std::move(other.name);
    maps = std::move(other.maps);
    controlSchemes = std::move(other.controlSchemes);
    devices = std::move(other.devices);
    bindingMask = std::move(other.bindingMask);
    return *this;
}

ActionAsset::~ActionAsset() {
    disable();
    for (ActionMap& map : maps) {
        map.m_state = nullptr;
        map.m_ownedState.reset();
    }
    m_state.reset();
}

void ActionAsset::enable(Manager& manager) {
    if (m_state == nullptr) {
        for (ActionMap& map : maps) {
            if (map.m_enabledActions > 0)
                map.disable();
            map.m_ownedState.reset();
            map.m_state = nullptr;
        }

        std::vector<ActionMap*> mapPointers;
        mapPointers.reserve(maps.size());
        for (ActionMap& map : maps)
            mapPointers.push_back(&map);

        m_state = ActionState::create(
            manager, mapPointers,
            devices.has_value() ? &devices.value() : nullptr,
            bindingMask.has_value() ? &bindingMask.value() : nullptr);
        for (ActionMap& map : maps)
            map.set_state(m_state.get());
    }

    for (ActionMap& map : maps)
        m_state->enable_all(map);
}

void ActionAsset::disable() {
    if (m_state == nullptr)
        return;
    for (ActionMap& map : maps)
        m_state->disable_all(map);
}

bool ActionAsset::re_resolve() {
    return m_state != nullptr &&
           m_state->re_resolve(devices.has_value() ? &devices.value() : nullptr,
                               bindingMask.has_value() ? &bindingMask.value() : nullptr);
}

ActionAsset ActionAsset::clone() const {
    ActionAsset result(name);
    result.controlSchemes = controlSchemes;
    result.devices = devices;
    result.bindingMask = bindingMask;
    result.maps.reserve(maps.size());

    for (const ActionMap& source : maps) {
        ActionMap& target = result.add_map(source.name);
        target.id = source.id;
        target.actions = source.actions;
        for (ActionDef& action : target.actions) {
            action.state = nullptr;
            action.actionIndex = -1;
            action.mapIndex = -1;
        }
        target.bindings = source.bindings;
        target.mapIndex = -1;
    }
    return result;
}

ActionMap& ActionAsset::add_map(std::string mapName) {
    maps.emplace_back(std::move(mapName));
    return maps.back();
}

ActionMap* ActionAsset::find_map(std::string_view nameOrId, bool throwIfNotFound) {
    for (ActionMap& map : maps) {
        if (equals_ignore_case(map.name, nameOrId) || map.id.to_string() == nameOrId)
            return &map;
    }
    if (throwIfNotFound)
        throw std::runtime_error("Action map not found: " + std::string(nameOrId));
    return nullptr;
}

ActionDef* ActionAsset::find_action(std::string_view nameOrId, bool throwIfNotFound) {
    for (ActionMap& map : maps) {
        if (ActionDef* action = map.find_action(nameOrId))
            return action;
    }
    if (throwIfNotFound)
        throw std::runtime_error("Action not found: " + std::string(nameOrId));
    return nullptr;
}

} // namespace insys

