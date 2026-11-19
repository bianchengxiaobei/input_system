#include "insys/actions/input_user.h"

#include <algorithm>
#include <utility>

#include "insys/device.h"
#include "insys/manager.h"
#include "insys/string_utils.h"

namespace insys {
namespace {

struct UserRegistry {
    std::vector<std::unique_ptr<InputUser>> owned;
    std::vector<InputUser*> users;
    int nextId = 1;
};

UserRegistry& registry() {
    static UserRegistry value;
    return value;
}

} // namespace

InputUser::InputUser(Manager& manager) : m_id(registry().nextId++), m_manager(&manager) {}

InputUser::~InputUser() {
    shutdown();
}

InputUser* InputUser::join(Manager& manager, const ActionAsset& actions,
                           bool enableActions) {
    return register_user(std::unique_ptr<InputUser>(new InputUser(manager)), manager, actions,
                         enableActions, true, {});
}

bool InputUser::leave(InputUser* user) {
    if (user == nullptr)
        return false;

    UserRegistry& state = registry();
    auto owned = std::find_if(state.owned.begin(), state.owned.end(),
                              [user](const std::unique_ptr<InputUser>& entry) {
                                  return entry.get() == user;
                              });
    if (owned == state.owned.end())
        return false;

    user->shutdown();
    state.users.erase(std::remove(state.users.begin(), state.users.end(), user),
                      state.users.end());
    state.owned.erase(owned);
    return true;
}

void InputUser::leave_all(Manager* manager) {
    UserRegistry& state = registry();
    std::vector<InputUser*> users;
    users.reserve(state.owned.size());
    for (const std::unique_ptr<InputUser>& user : state.owned) {
        if (user != nullptr && (manager == nullptr || user->m_manager == manager))
            users.push_back(user.get());
    }
    for (InputUser* user : users)
        leave(user);
}

const std::vector<InputUser*>& InputUser::all() {
    return registry().users;
}

InputUser* InputUser::find(int userId) {
    for (InputUser* user : registry().users) {
        if (user != nullptr && user->m_id == userId)
            return user;
    }
    return nullptr;
}

InputUser* InputUser::register_user(std::unique_ptr<InputUser> user, Manager& manager,
                                    const ActionAsset& source, bool enableActions,
                                    bool enableAllMapsIfNoDefault,
                                    std::string_view defaultActionMap) {
    if (!user || user->m_manager != &manager)
        return nullptr;

    if (!user->initialize(std::make_unique<ActionAsset>(source.clone()), enableActions,
                          enableAllMapsIfNoDefault, defaultActionMap))
        return nullptr;

    InputUser* raw = user.get();
    UserRegistry& state = registry();
    state.owned.push_back(std::move(user));
    state.users.push_back(raw);
    raw->on_initialized();
    return raw;
}

bool InputUser::initialize(std::unique_ptr<ActionAsset> actions, bool enableActions,
                           bool enableAllMapsIfNoDefault,
                           std::string_view defaultActionMap) {
    if (!actions || m_manager == nullptr)
        return false;

    m_actions = std::move(actions);
    m_defaultActionMap.assign(defaultActionMap);
    m_currentActionMap.clear();
    m_defaultBindingMask = m_actions->bindingMask;
    m_actions->devices = std::vector<Device*>{};
    m_autoPairing = true;

    m_deviceListenerId = m_manager->add_device_change_listener(
        [this](Device* device, DeviceChange change) { handle_device_change(device, change); });

    auto_pair_devices();

    if (enableActions) {
        enable();
        if (!m_defaultActionMap.empty()) {
            disable_all_action_maps();
            enable_action_map(m_defaultActionMap);
        } else if (!enableAllMapsIfNoDefault) {
            disable_all_action_maps();
        }
    }

    m_initialized = true;
    return true;
}

void InputUser::shutdown() {
    if (m_actions) {
        m_actions->disable();
        m_actionsEnabled = false;
    }
    if (m_manager != nullptr && m_deviceListenerId != 0) {
        m_manager->remove_device_change_listener(m_deviceListenerId);
        m_deviceListenerId = 0;
    }
    m_initialized = false;
    m_manager = nullptr;
    m_pairedDevices.clear();
    m_schemeMatch = {};
    m_controlScheme.reset();
    m_currentActionMap.clear();
}

bool InputUser::has_device(Device* device) const {
    return std::find(m_pairedDevices.begin(), m_pairedDevices.end(), device) !=
           m_pairedDevices.end();
}

bool InputUser::pair_device(Device* device) {
    if (device == nullptr || m_manager == nullptr)
        return false;
    if (!has_device(device))
        m_pairedDevices.push_back(device);
    update_scheme_match();
    refresh_action_devices();
    notify_devices_changed();
    return true;
}

bool InputUser::unpair_device(Device* device) {
    const auto it = std::find(m_pairedDevices.begin(), m_pairedDevices.end(), device);
    if (it == m_pairedDevices.end())
        return false;
    m_pairedDevices.erase(it);
    update_scheme_match();
    refresh_action_devices();
    notify_devices_changed();
    return true;
}

void InputUser::unpair_all_devices() {
    if (m_pairedDevices.empty())
        return;
    m_pairedDevices.clear();
    update_scheme_match();
    refresh_action_devices();
    notify_devices_changed();
}

bool InputUser::pair_devices(const std::vector<Device*>& devices) {
    std::vector<Device*> paired;
    paired.reserve(devices.size());
    for (Device* device : devices) {
        if (device != nullptr && std::find(paired.begin(), paired.end(), device) == paired.end())
            paired.push_back(device);
    }
    if (paired == m_pairedDevices)
        return true;
    m_pairedDevices = std::move(paired);
    update_scheme_match();
    refresh_action_devices();
    notify_devices_changed();
    return true;
}

bool InputUser::auto_pair_devices() {
    if (!m_actions || m_actions->controlSchemes.empty())
        return false;

    const std::vector<Device*> available = available_devices_for_user();
    const std::optional<ControlSchemeMatch> match =
        find_control_scheme(m_actions->controlSchemes, available);
    if (!match.has_value()) {
        update_scheme_match();
        return false;
    }

    m_pairedDevices = match->devices;
    m_controlScheme = match->scheme;
    m_schemeMatch = *match;
    apply_control_scheme_mask();
    refresh_action_devices();
    notify_devices_changed();
    notify_control_scheme_changed();
    return true;
}

bool InputUser::set_control_scheme(std::string_view nameOrId) {
    if (!m_actions)
        return false;
    for (const ControlScheme& scheme : m_actions->controlSchemes) {
        if (equals_ignore_case(scheme.name, nameOrId) ||
            scheme.id.to_string() == nameOrId) {
            m_controlScheme = scheme;
            apply_control_scheme_mask();
            update_scheme_match();
            refresh_action_devices();
            notify_control_scheme_changed();
            return true;
        }
    }
    return false;
}

void InputUser::clear_control_scheme() {
    if (!m_controlScheme.has_value())
        return;
    m_controlScheme.reset();
    if (m_actions)
        m_actions->bindingMask = m_defaultBindingMask;
    update_scheme_match();
    refresh_action_devices();
    notify_control_scheme_changed();
}

const ControlScheme* InputUser::control_scheme() const {
    return m_controlScheme.has_value() ? &m_controlScheme.value() : nullptr;
}

bool InputUser::has_missing_required_devices() const {
    return m_controlScheme.has_value() && !m_schemeMatch.successful();
}

ControlSchemeMatch InputUser::match_control_scheme() const {
    if (!m_actions)
        return {};
    const std::optional<ControlSchemeMatch> match =
        find_control_scheme(m_actions->controlSchemes, available_devices_for_user());
    return match.has_value() ? *match : ControlSchemeMatch{};
}

void InputUser::enable() {
    if (!m_actions || m_manager == nullptr || m_actionsEnabled)
        return;
    m_actions->enable(*m_manager);
    m_actionsEnabled = true;
    notify_action_maps_changed();
}

void InputUser::disable() {
    if (!m_actions || !m_actionsEnabled)
        return;
    m_actions->disable();
    m_actionsEnabled = false;
    notify_action_maps_changed();
}

bool InputUser::enable_action_map(std::string_view nameOrId) {
    if (!m_actions)
        return false;
    ActionMap* map = m_actions->find_map(nameOrId);
    if (map == nullptr)
        return false;
    ensure_actions_enabled();
    map->enable(*m_manager);
    m_currentActionMap = map->name;
    notify_action_maps_changed();
    return true;
}

bool InputUser::disable_action_map(std::string_view nameOrId) {
    if (!m_actions)
        return false;
    ActionMap* map = m_actions->find_map(nameOrId);
    if (map == nullptr)
        return false;
    map->disable();
    if (m_currentActionMap == map->name)
        m_currentActionMap.clear();
    notify_action_maps_changed();
    return true;
}

bool InputUser::action_map_enabled(std::string_view nameOrId) const {
    if (!m_actions)
        return false;
    ActionMap* map = m_actions->find_map(nameOrId);
    return map != nullptr && map->enabled();
}

void InputUser::enable_all_action_maps() {
    if (!m_actions)
        return;
    ensure_actions_enabled();
    for (ActionMap& map : m_actions->maps)
        map.enable(*m_manager);
    m_currentActionMap.clear();
    notify_action_maps_changed();
}

void InputUser::disable_all_action_maps() {
    if (!m_actions)
        return;
    for (ActionMap& map : m_actions->maps)
        map.disable();
    m_currentActionMap.clear();
    notify_action_maps_changed();
}

bool InputUser::switch_action_map(std::string_view nameOrId) {
    if (!m_actions)
        return false;
    ActionMap* target = m_actions->find_map(nameOrId);
    if (target == nullptr)
        return false;
    ensure_actions_enabled();
    for (ActionMap& map : m_actions->maps)
        map.disable();
    target->enable(*m_manager);
    m_currentActionMap = target->name;
    notify_action_maps_changed();
    return true;
}

bool InputUser::re_resolve() {
    return m_actions != nullptr && m_actions->state() != nullptr && m_actions->re_resolve();
}

std::vector<Device*> InputUser::available_devices_for_user() const {
    std::vector<Device*> available;
    if (m_manager == nullptr)
        return available;
    for (Device* device : m_manager->devices()) {
        if (device != nullptr && !device_paired_to_other_user(device))
            available.push_back(device);
    }
    return available;
}

bool InputUser::device_paired_to_other_user(Device* device) const {
    if (device == nullptr)
        return false;
    for (InputUser* user : registry().users) {
        if (user != nullptr && user != this && user->m_manager == m_manager &&
            user->has_device(device))
            return true;
    }
    return false;
}

void InputUser::update_scheme_match() {
    m_schemeMatch = {};
    if (!m_actions || !m_controlScheme.has_value())
        return;
    m_schemeMatch = insys::match_control_scheme(*m_controlScheme, m_pairedDevices);
}

void InputUser::apply_control_scheme_mask() {
    if (!m_actions)
        return;
    if (!m_controlScheme.has_value() || m_controlScheme->bindingGroup.empty()) {
        m_actions->bindingMask = m_defaultBindingMask;
        return;
    }
    Binding mask;
    mask.groups = m_controlScheme->bindingGroup;
    m_actions->bindingMask = std::move(mask);
}

void InputUser::refresh_action_devices() {
    if (!m_actions)
        return;
    m_actions->devices = m_pairedDevices;
    if (m_actions->state() != nullptr)
        m_actions->re_resolve();
}

void InputUser::notify_devices_changed() {
    if (on_devices_changed)
        on_devices_changed(*this);
}

void InputUser::notify_control_scheme_changed() {
    if (on_control_scheme_changed)
        on_control_scheme_changed(*this);
}

void InputUser::notify_action_maps_changed() {
    if (on_action_maps_changed)
        on_action_maps_changed(*this);
}

void InputUser::handle_device_change(Device* device, DeviceChange change) {
    if (!m_initialized || m_manager == nullptr || device == nullptr)
        return;

    if (change == DeviceChange::Removed && has_device(device)) {
        m_pairedDevices.erase(
            std::remove(m_pairedDevices.begin(), m_pairedDevices.end(), device),
            m_pairedDevices.end());
        update_scheme_match();
        if (m_autoPairing && should_auto_pair() && auto_pair_devices())
            return;
        refresh_action_devices();
        notify_devices_changed();
        return;
    }

    if ((change == DeviceChange::Added || change == DeviceChange::Reconnected) &&
        m_autoPairing && should_auto_pair()) {
        auto_pair_devices();
    }
}

void InputUser::ensure_actions_enabled() {
    if (!m_actionsEnabled)
        enable();
}

bool InputUser::should_auto_pair() const {
    if (m_pairedDevices.empty())
        return true;
    return m_controlScheme.has_value() && !m_schemeMatch.successful();
}

} // namespace insys




