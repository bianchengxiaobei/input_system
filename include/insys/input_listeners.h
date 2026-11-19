#pragma once

#include <functional>

namespace insys {

struct ActionDef;
class Device;

namespace low {
struct Event;
}

// Mirrors the subset of Unity's InputDeviceChange used by the runtime.
enum class DeviceChange {
    Added,
    Removed,
    Disconnected,
    Reconnected,
    Enabled,
    Disabled,
    UsageChanged,
    ConfigurationChanged,
    SoftReset,
    HardReset,
};

// Mirrors InputActionChange. Map notifications use the first action of the map
// as the callback object (or nullptr for an empty map).
enum class ActionChange {
    ActionEnabled,
    ActionDisabled,
    ActionMapEnabled,
    ActionMapDisabled,
    ActionStarted,
    ActionPerformed,
    ActionCanceled,
    BoundControlsAboutToChange,
    BoundControlsChanged,
};

using EventListener = std::function<void(low::Event&, Device*)>;
using DeviceChangeListener = std::function<void(Device*, DeviceChange)>;
using ActionChangeListener = std::function<void(ActionDef*, ActionChange)>;

const char* device_change_name(DeviceChange change);
const char* action_change_name(ActionChange change);

} // namespace insys
