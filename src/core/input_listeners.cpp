#include "insys/input_listeners.h"

namespace insys {

const char* device_change_name(DeviceChange change) {
    switch (change) {
    case DeviceChange::Added: return "Added";
    case DeviceChange::Removed: return "Removed";
    case DeviceChange::Disconnected: return "Disconnected";
    case DeviceChange::Reconnected: return "Reconnected";
    case DeviceChange::Enabled: return "Enabled";
    case DeviceChange::Disabled: return "Disabled";
    case DeviceChange::UsageChanged: return "UsageChanged";
    case DeviceChange::ConfigurationChanged: return "ConfigurationChanged";
    case DeviceChange::SoftReset: return "SoftReset";
    case DeviceChange::HardReset: return "HardReset";
    }
    return "Unknown";
}

const char* action_change_name(ActionChange change) {
    switch (change) {
    case ActionChange::ActionEnabled: return "ActionEnabled";
    case ActionChange::ActionDisabled: return "ActionDisabled";
    case ActionChange::ActionMapEnabled: return "ActionMapEnabled";
    case ActionChange::ActionMapDisabled: return "ActionMapDisabled";
    case ActionChange::ActionStarted: return "ActionStarted";
    case ActionChange::ActionPerformed: return "ActionPerformed";
    case ActionChange::ActionCanceled: return "ActionCanceled";
    case ActionChange::BoundControlsAboutToChange: return "BoundControlsAboutToChange";
    case ActionChange::BoundControlsChanged: return "BoundControlsChanged";
    }
    return "Unknown";
}

} // namespace insys