#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "insys/actions/action.h"
#include "insys/input_listeners.h"
#include "insys/low/event.h"

namespace insys {

class ActionAsset;
struct ActionDef;
class Control;
class Device;
class Manager;

struct DebuggerControlInfo {
    std::string path;
    std::string layout;
    std::string value;
    uint32_t offset = 0;
    uint32_t sizeInBits = 0;
    bool button = false;
    bool axis = false;
};

struct DebuggerDeviceInfo {
    int deviceId = 0;
    std::string name;
    std::string layout;
    bool enabled = true;
    bool connected = true;
    std::vector<DebuggerControlInfo> controls;
};

struct DebuggerActionInfo {
    std::string map;
    std::string name;
    ActionPhase phase = ActionPhase::Disabled;
    std::string value;
    std::string drivingControl;
    double lastStartedTime = 0.0;
    double lastPerformedTime = 0.0;
    double lastCanceledTime = 0.0;
    uint32_t lastPerformedFrame = 0;
    uint32_t lastCanceledFrame = 0;
    bool enabled = false;
};

struct DebuggerBindingInfo {
    std::string map;
    std::string action;
    std::string path;
    std::string interactions;
    std::string processors;
    std::string name;
    bool overridden = false;
    bool composite = false;
    bool partOfComposite = false;
};

struct DebuggerEventInfo {
    double time = 0.0;
    std::string type;
    int deviceId = 0;
    uint32_t size = 0;
};

// Read-only snapshot for the debugger UI. refresh() is intentionally the only
// work done per frame; event recording is a separate listener callback.
class DebuggerModel {
public:
    struct Options {
        std::size_t maxRecentEvents = 128;
        std::size_t maxControlsPerDevice = 64;
        bool includeIdleControls = true;
    };

    DebuggerModel() = default;
    explicit DebuggerModel(Options options) : m_options(options) {}

    void add_asset(const ActionAsset& asset);
    void clear_assets();

    void refresh(Manager& manager);
    void record_event(const low::Event& event, const Device* device);
    void record_action_change(ActionDef* action, ActionChange change);
    void clear_events();

    void set_paused(bool paused) { m_paused = paused; }
    bool paused() const { return m_paused; }
    bool refresh(Manager& manager, bool force);

    const std::vector<DebuggerDeviceInfo>& devices() const { return m_devices; }
    const std::vector<DebuggerActionInfo>& actions() const { return m_actions; }
    const std::vector<DebuggerBindingInfo>& bindings() const { return m_bindings; }
    const std::deque<DebuggerEventInfo>& recent_events() const { return m_recentEvents; }

private:
    struct ActionHistory {
        ActionPhase phase = ActionPhase::Disabled;
        double startedTime = 0.0;
        double performedTime = 0.0;
        double canceledTime = 0.0;
        uint32_t performedFrame = 0;
        uint32_t canceledFrame = 0;
    };

    void refresh_devices(Manager& manager);
    void refresh_actions(Manager& manager);
    void refresh_bindings();

    Options m_options;
    bool m_paused = false;
    std::vector<const ActionAsset*> m_assets;
    std::vector<DebuggerDeviceInfo> m_devices;
    std::vector<DebuggerActionInfo> m_actions;
    std::vector<DebuggerBindingInfo> m_bindings;
    std::deque<DebuggerEventInfo> m_recentEvents;
    std::unordered_map<const ActionDef*, ActionHistory> m_history;
};

std::string debugger_format_control_value(const Control& control, const uint8_t* state);
std::string debugger_format_action_value(const class ActionState& state, const ActionDef& action);

} // namespace insys
