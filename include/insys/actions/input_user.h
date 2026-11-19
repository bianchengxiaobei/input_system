#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "insys/actions/action_asset.h"
#include "insys/actions/control_scheme.h"
#include "insys/input_listeners.h"

namespace insys {

class Device;
class Manager;

class InputUser {
public:
    InputUser(const InputUser&) = delete;
    InputUser& operator=(const InputUser&) = delete;
    virtual ~InputUser();

    static InputUser* join(Manager& manager, const ActionAsset& actions,
                           bool enableActions = true);
    static bool leave(InputUser* user);
    static void leave_all(Manager* manager = nullptr);
    static const std::vector<InputUser*>& all();
    static InputUser* find(int userId);

    int id() const { return m_id; }
    Manager* manager() const { return m_manager; }
    ActionAsset& actions() { return *m_actions; }
    const ActionAsset& actions() const { return *m_actions; }

    const std::vector<Device*>& paired_devices() const { return m_pairedDevices; }
    bool has_device(Device* device) const;
    bool pair_device(Device* device);
    bool unpair_device(Device* device);
    void unpair_all_devices();
    bool pair_devices(const std::vector<Device*>& devices);

    ControlSchemeMatch match_control_scheme() const;
    bool auto_pair_devices();
    bool set_control_scheme(std::string_view nameOrId);
    void clear_control_scheme();
    const ControlScheme* control_scheme() const;
    const ControlSchemeMatch& control_scheme_match() const { return m_schemeMatch; }
    bool has_missing_required_devices() const;

    void enable();
    void disable();
    bool enabled() const { return m_actionsEnabled; }
    bool enable_action_map(std::string_view nameOrId);
    bool disable_action_map(std::string_view nameOrId);
    bool action_map_enabled(std::string_view nameOrId) const;
    void enable_all_action_maps();
    void disable_all_action_maps();
    std::string current_action_map() const { return m_currentActionMap; }
    bool switch_action_map(std::string_view nameOrId);

    void set_auto_pairing(bool enabled) { m_autoPairing = enabled; }
    bool auto_pairing() const { return m_autoPairing; }

    bool re_resolve();

    std::function<void(InputUser&)> on_devices_changed;
    std::function<void(InputUser&)> on_control_scheme_changed;
    std::function<void(InputUser&)> on_action_maps_changed;

protected:
    explicit InputUser(Manager& manager);

    static InputUser* register_user(std::unique_ptr<InputUser> user, Manager& manager,
                                    const ActionAsset& source, bool enableActions,
                                    bool enableAllMapsIfNoDefault,
                                    std::string_view defaultActionMap);

    virtual void on_initialized() {}

private:
    bool initialize(std::unique_ptr<ActionAsset> actions, bool enableActions,
                    bool enableAllMapsIfNoDefault, std::string_view defaultActionMap);
    void shutdown();

    std::vector<Device*> available_devices_for_user() const;
    bool device_paired_to_other_user(Device* device) const;
    void update_scheme_match();
    void apply_control_scheme_mask();
    void refresh_action_devices();
    void notify_devices_changed();
    void notify_control_scheme_changed();
    void notify_action_maps_changed();
    void handle_device_change(Device* device, DeviceChange change);
    void ensure_actions_enabled();
    bool should_auto_pair() const;

    int m_id = 0;
    Manager* m_manager = nullptr;
    std::unique_ptr<ActionAsset> m_actions;
    std::vector<Device*> m_pairedDevices;
    ControlSchemeMatch m_schemeMatch;
    std::optional<Binding> m_defaultBindingMask;
    std::optional<ControlScheme> m_controlScheme;
    std::string m_currentActionMap;
    std::string m_defaultActionMap;
    int m_deviceListenerId = 0;
    bool m_actionsEnabled = false;
    bool m_autoPairing = true;
    bool m_initialized = false;
};

} // namespace insys
