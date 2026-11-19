#include "insys/debugger_model.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "insys/actions/action_asset.h"
#include "insys/actions/action_map.h"
#include "insys/actions/action_state.h"
#include "insys/control.h"
#include "insys/control_path.h"
#include "insys/device.h"
#include "insys/manager.h"

namespace insys {
namespace {

std::string format_float(float value) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(2);
    stream << value;
    return stream.str();
}

} // namespace

std::string debugger_format_control_value(const Control& control, const uint8_t* state) {
    if (state == nullptr)
        return "-";

    if (const auto* vector = dynamic_cast<const Vector2Control*>(&control)) {
        (void)vector;
        const Vector2 value = control.read_vector2(state);
        return "(" + format_float(value.x) + ", " + format_float(value.y) + ")";
    }
    if (dynamic_cast<const DpadControl*>(&control) != nullptr) {
        const Vector2 value = control.read_vector2(state);
        return "(" + format_float(value.x) + ", " + format_float(value.y) + ")";
    }
    return format_float(control.read_float(state));
}

std::string debugger_format_action_value(const ActionState& state, const ActionDef& action) {
    if (action.state != &state)
        return "-";
    switch (state.value_type(action)) {
    case ValueType::Vector2: {
        const Vector2 value = state.read_value<Vector2>(action);
        return "(" + format_float(value.x) + ", " + format_float(value.y) + ")";
    }
    case ValueType::Vector3: {
        const Vector3 value = state.read_value<Vector3>(action);
        return "(" + format_float(value.x) + ", " + format_float(value.y) + ", " +
               format_float(value.z) + ")";
    }
    case ValueType::Float:
        return format_float(state.read_value<float>(action));
    case ValueType::Int:
        return std::to_string(state.read_value<int32_t>(action));
    case ValueType::Double:
        return std::to_string(state.read_value<double>(action));
    case ValueType::None:
    case ValueType::Quaternion:
    default:
        return "-";
    }
}

void DebuggerModel::add_asset(const ActionAsset& asset) {
    m_assets.push_back(&asset);
}

void DebuggerModel::clear_assets() {
    m_assets.clear();
    m_actions.clear();
    m_bindings.clear();
    m_history.clear();
}

void DebuggerModel::record_event(const low::Event& event, const Device* device) {
    DebuggerEventInfo info;
    info.time = event.time;
    info.type = to_string(FourCC(event.type));
    info.deviceId = device != nullptr ? device->deviceId : event.deviceId;
    info.size = event.size;
    m_recentEvents.push_back(std::move(info));
    while (m_recentEvents.size() > m_options.maxRecentEvents)
        m_recentEvents.pop_front();
}

void DebuggerModel::record_action_change(ActionDef* action, ActionChange change) {
    if (action == nullptr)
        return;
    ActionHistory& history = m_history[action];
    const ActionState* state = action->state;
    const TriggerState* trigger =
        state != nullptr ? state->trigger_state(action->actionIndex) : nullptr;
    const double time = trigger != nullptr ? trigger->time : 0.0;
    switch (change) {
    case ActionChange::ActionStarted:
        history.startedTime = time;
        history.phase = ActionPhase::Started;
        break;
    case ActionChange::ActionPerformed:
        history.performedTime = time;
        history.performedFrame = trigger != nullptr ? trigger->lastPerformedInUpdate : 0;
        history.phase = ActionPhase::Performed;
        break;
    case ActionChange::ActionCanceled:
        history.canceledTime = time;
        history.canceledFrame = trigger != nullptr ? trigger->lastCanceledInUpdate : 0;
        history.phase = ActionPhase::Canceled;
        break;
    case ActionChange::ActionDisabled:
        history.phase = ActionPhase::Disabled;
        break;
    default:
        break;
    }
}

void DebuggerModel::clear_events() {
    m_recentEvents.clear();
}

bool DebuggerModel::refresh(Manager& manager, bool force) {
    if (m_paused && !force)
        return false;
    refresh_devices(manager);
    refresh_actions(manager);
    refresh_bindings();
    return true;
}

void DebuggerModel::refresh(Manager& manager) {
    (void)refresh(manager, false);
}

void DebuggerModel::refresh_devices(Manager& manager) {
    m_devices.clear();
    m_devices.reserve(manager.devices().size());

    for (Device* device : manager.devices()) {
        if (device == nullptr)
            continue;

        DebuggerDeviceInfo info;
        info.deviceId = device->deviceId;
        info.name = device->name.c_str();
        info.layout = device->layout.c_str();
        info.enabled = true;
        info.connected = true;

        const uint8_t* state = device->current_state();
        for (Control* control : device->allControls) {
            if (control == nullptr || control == device)
                continue;
            const std::string value = debugger_format_control_value(*control, state);
            if (!m_options.includeIdleControls &&
                (value == "0.00" || value == "(0.00, 0.00)"))
                continue;
            DebuggerControlInfo controlInfo;
            controlInfo.path = control_path(*control);
            controlInfo.layout = control->layout.c_str();
            controlInfo.value = value;
            controlInfo.offset = control->stateBlock.effective_byte_offset();
            controlInfo.sizeInBits = control->stateBlock.sizeInBits;
            controlInfo.button = control->is_button();
            controlInfo.axis = control->is_axis();
            info.controls.push_back(std::move(controlInfo));
            if (info.controls.size() >= m_options.maxControlsPerDevice)
                break;
        }
        m_devices.push_back(std::move(info));
    }
}

void DebuggerModel::refresh_actions(Manager& manager) {
    (void)manager;
    m_actions.clear();

    for (const ActionAsset* asset : m_assets) {
        if (asset == nullptr)
            continue;
        ActionState* state = asset->state();
        for (ActionMap& map : const_cast<ActionAsset*>(asset)->maps) {
            for (ActionDef& action : map.actions) {
                DebuggerActionInfo info;
                info.map = map.name;
                info.name = action.name;
                info.phase = state != nullptr ? state->phase(action) : ActionPhase::Disabled;
                info.enabled = info.phase != ActionPhase::Disabled;
                info.value = state != nullptr ? debugger_format_action_value(*state, action) : "-";
                if (state != nullptr) {
                    if (Control* control = state->active_control(action))
                        info.drivingControl = control_path(*control);
                }

                ActionHistory& history = m_history[&action];
                if (history.phase != info.phase) {
                    if (info.phase == ActionPhase::Started || info.phase == ActionPhase::Performed) {
                        const TriggerState* trigger =
                            state != nullptr ? state->trigger_state(action.actionIndex) : nullptr;
                        const double time = trigger != nullptr ? trigger->time : 0.0;
                        if (info.phase == ActionPhase::Started)
                            history.startedTime = time;
                        if (info.phase == ActionPhase::Performed) {
                            history.performedTime = time;
                            history.performedFrame =
                                trigger != nullptr ? trigger->lastPerformedInUpdate : 0;
                        }
                    } else if (info.phase == ActionPhase::Canceled) {
                        const TriggerState* trigger =
                            state != nullptr ? state->trigger_state(action.actionIndex) : nullptr;
                        history.canceledTime = trigger != nullptr ? trigger->time : 0.0;
                        history.canceledFrame =
                            trigger != nullptr ? trigger->lastCanceledInUpdate : 0;
                    }
                    history.phase = info.phase;
                }

                info.lastStartedTime = history.startedTime;
                info.lastPerformedTime = history.performedTime;
                info.lastCanceledTime = history.canceledTime;
                info.lastPerformedFrame = history.performedFrame;
                info.lastCanceledFrame = history.canceledFrame;
                m_actions.push_back(std::move(info));
            }
        }
    }
}

void DebuggerModel::refresh_bindings() {
    m_bindings.clear();
    for (const ActionAsset* asset : m_assets) {
        if (asset == nullptr)
            continue;
        const ActionAsset& value = *asset;
        for (const ActionMap& map : const_cast<ActionAsset&>(value).maps) {
            for (const Binding& binding : map.bindings) {
                DebuggerBindingInfo info;
                info.map = map.name;
                info.action = binding.action;
                info.path = std::string(binding.effective_path());
                info.interactions = std::string(binding.effective_interactions());
                info.processors = std::string(binding.effective_processors());
                info.name = binding.name;
                info.overridden = !binding.overridePath.empty() ||
                                  !binding.overrideInteractions.empty() ||
                                  !binding.overrideProcessors.empty();
                info.composite = binding.isComposite;
                info.partOfComposite = binding.isPartOfComposite;
                m_bindings.push_back(std::move(info));
            }
        }
    }
}

} // namespace insys