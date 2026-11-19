#include "insys/actions/action_state.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "insys/actions/action_map.h"
#include "insys/actions/binding_resolver.h"
#include "insys/actions/processor.h"
#include "insys/device.h"

namespace insys {
namespace {

constexpr float kDefaultButtonPressPoint = 0.5f;
constexpr float kButtonReleaseThreshold = 0.75f;
constexpr float kEpsilon = 1e-5f;

int64_t pack_monitor_index(int mapIndex, int controlIndex, int bindingIndex) {
    return (static_cast<int64_t>(mapIndex) << 40) |
           (static_cast<int64_t>(controlIndex) << 16) |
           static_cast<int64_t>(bindingIndex);
}

void unpack_monitor_index(int64_t packed, int& mapIndex, int& controlIndex, int& bindingIndex) {
    mapIndex = static_cast<int>((packed >> 40) & 0xFFFFFF);
    controlIndex = static_cast<int>((packed >> 16) & 0xFFFFFF);
    bindingIndex = static_cast<int>(packed & 0xFFFF);
}

ValueType value_type_from_control(const Control* control) {
    if (control == nullptr)
        return ValueType::None;
    if (dynamic_cast<const Vector2Control*>(control) != nullptr ||
        dynamic_cast<const DpadControl*>(control) != nullptr)
        return ValueType::Vector2;
    switch (control->value_size_bytes()) {
    case sizeof(Vector2):
        return ValueType::Vector2;
    case sizeof(Vector3):
        return ValueType::Vector3;
    case sizeof(Quaternion):
        return ValueType::Quaternion;
    default:
        return ValueType::Float;
    }
}

bool approximately_zero(float value) {
    return std::fabs(value) <= kEpsilon;
}

} // namespace

ProcessorContext ActionState::processor_context() const {
    ProcessorContext context;
    if (manager != nullptr) {
        context.settings = &manager->settings();
        context.runtime = &manager->runtime();
    }
    return context;
}

ActionState::ActionState() = default;

ActionState::~ActionState() {
    dispose();
}

std::unique_ptr<ActionState> ActionState::create(
    Manager& manager, const std::vector<ActionMap*>& maps,
    const std::vector<Device*>* devices, const Binding* bindingMask) {
    auto state = std::make_unique<ActionState>();
    if (!state->resolve_and_initialize(manager, maps, devices, bindingMask))
        return nullptr;
    return state;
}

bool ActionState::resolve_and_initialize(Manager& managerValue,
                                         const std::vector<ActionMap*>& maps,
                                         const std::vector<Device*>* devices,
                                         const Binding* bindingMask) {
    BindingResolver resolver(managerValue, devices, bindingMask);
    if (!initialize(managerValue, resolver.resolve(maps)))
        return false;

    m_hasDeviceFilter = devices != nullptr;
    m_deviceFilterIds.clear();
    if (devices != nullptr) {
        m_deviceFilterIds.reserve(devices->size());
        for (Device* device : *devices) {
            if (device != nullptr)
                m_deviceFilterIds.push_back(device->deviceId);
        }
    }
    if (bindingMask != nullptr)
        m_bindingMask = *bindingMask;
    else
        m_bindingMask.reset();
    return true;
}

bool ActionState::initialize(Manager& managerValue, ResolvedBindings&& resolved) {
    dispose();

    manager = &managerValue;
    m_maps = std::move(resolved.maps);
    m_actions = std::move(resolved.actions);
    m_controls = std::move(resolved.controls);
    m_interactions = std::move(resolved.interactions);
    m_processors = std::move(resolved.processors);
    m_composites = std::move(resolved.composites);
    m_interactionStorage = std::move(resolved.interactionStorage);
    m_processorStorage = std::move(resolved.processorStorage);
    m_compositeStorage = std::move(resolved.compositeStorage);
    m_memory = std::move(resolved.memory);
    m_controlEnableCounts.assign(static_cast<std::size_t>(m_memory.controlCount), 0);
    m_controlDeviceIds.assign(m_controls.size(), 0);
    m_controlDevices.assign(m_controls.size(), nullptr);
    for (std::size_t i = 0; i < m_controls.size(); ++i) {
        if (m_controls[i] != nullptr && m_controls[i]->device != nullptr) {
            m_controlDeviceIds[i] = m_controls[i]->device->deviceId;
            m_controlDevices[i] = m_controls[i]->device;
        }
    }
    m_enabledActionCount = 0;

    for (ActionDef* action : m_actions) {
        if (action != nullptr)
            action->state = this;
    }
    return true;
}

void ActionState::dispose() {
    if (manager != nullptr) {
        for (ActionDef* action : m_actions) {
            if (action != nullptr && action->state == this) {
                if (action->actionIndex >= 0 &&
                    m_memory.actionStates[action->actionIndex].phase_value() != ActionPhase::Disabled)
                    disable_single(*action);
                action->state = nullptr;
            }
        }
    }
    unhook_initial_state_check();
    m_enabledActionCount = 0;
    for (ActionMap* map : m_maps) {
        if (map != nullptr)
            map->m_enabledActions = 0;
    }
    manager = nullptr;
}

void ActionState::enable_all(ActionMap& map) {
    for (ActionDef& action : map.actions)
        enable_single(action);
    if (manager != nullptr)
        manager->notify_action_change(map.actions.empty() ? nullptr : &map.actions.front(),
                                       ActionChange::ActionMapEnabled);
}

void ActionState::disable_all(ActionMap& map) {
    for (ActionDef& action : map.actions)
        disable_single(action);
    if (manager != nullptr)
        manager->notify_action_change(map.actions.empty() ? nullptr : &map.actions.front(),
                                       ActionChange::ActionMapDisabled);
}

void ActionState::enable_single(ActionDef& action) {
    if (manager == nullptr || action.actionIndex < 0 ||
        action.actionIndex >= m_memory.actionCount)
        return;

    TriggerState& state = m_memory.actionStates[action.actionIndex];
    if (state.phase_value() != ActionPhase::Disabled)
        return;

    state = TriggerState{};
    state.set_phase(ActionPhase::Waiting);
    state.mapIndex = static_cast<uint8_t>(action.mapIndex);
    state.controlIndex = kInvalidIndex16;
    state.bindingIndex = kInvalidIndex16;
    state.interactionIndex = kInvalidIndex16;

    const std::size_t countIndex = static_cast<std::size_t>(action.actionIndex) * 2;
    const int bindingStart = m_memory.actionBindingIndicesAndCounts[countIndex];
    const int bindingCount = m_memory.actionBindingIndicesAndCounts[countIndex + 1];

    bool wantsCheck = false;
    for (int i = 0; i < bindingCount; ++i) {
        const int bindingIndex = m_memory.actionBindingIndices[bindingStart + i];
        BindingState& binding = m_memory.bindingStates[bindingIndex];
        if (binding.is_disabled() || binding.is_part_of_composite())
            continue;

        const int controlStart = binding.controlStartIndex;
        const int controlCount = binding.controlCount;
        for (int c = 0; c < controlCount; ++c) {
            const int controlIndex = controlStart + c;
            if (controlIndex < 0 || controlIndex >= static_cast<int>(m_controls.size()))
                continue;
            Control* control = m_controls[static_cast<std::size_t>(controlIndex)];
            if (control == nullptr)
                continue;
            if (m_controlEnableCounts[static_cast<std::size_t>(controlIndex)]++ == 0)
                m_memory.set_control_enabled(controlIndex, true);
            manager->add_state_change_monitor(
                control, this,
                pack_monitor_index(binding.mapIndex, controlIndex, bindingIndex));
        }

        if (binding.wants_initial_state_check()) {
            binding.set_initial_state_check_pending(true);
            wantsCheck = true;
        }
    }

    ++m_enabledActionCount;
    if (action.mapIndex >= 0 && action.mapIndex < static_cast<int>(m_maps.size()) &&
        m_maps[static_cast<std::size_t>(action.mapIndex)] != nullptr)
        ++m_maps[static_cast<std::size_t>(action.mapIndex)]->m_enabledActions;

    if (wantsCheck)
        hook_initial_state_check();

    if (manager != nullptr)
        manager->notify_action_change(&action, ActionChange::ActionEnabled);
}

void ActionState::disable_single(ActionDef& action) {
    if (action.actionIndex < 0 || action.actionIndex >= m_memory.actionCount)
        return;

    TriggerState& state = m_memory.actionStates[action.actionIndex];
    if (state.phase_value() == ActionPhase::Disabled) {
        reset_action_interactions(action.actionIndex);
        return;
    }

    if (state.phase_value() == ActionPhase::Started || state.phase_value() == ActionPhase::Performed) {
        TriggerState trigger;
        trigger.mapIndex = state.mapIndex;
        trigger.controlIndex = state.controlIndex;
        trigger.bindingIndex = state.bindingIndex;
        trigger.interactionIndex = state.interactionIndex;
        trigger.time = manager != nullptr ? manager->runtime().now() : state.time;
        trigger.startTime = state.startTime;
        trigger.magnitude = state.magnitude;
        trigger.flags = state.flags;
        change_phase_of_action(ActionPhase::Canceled, trigger, ActionPhase::Disabled);
    }

    const std::size_t countIndex = static_cast<std::size_t>(action.actionIndex) * 2;
    const int bindingStart = m_memory.actionBindingIndicesAndCounts[countIndex];
    const int bindingCount = m_memory.actionBindingIndicesAndCounts[countIndex + 1];
    for (int i = 0; i < bindingCount; ++i) {
        const int bindingIndex = m_memory.actionBindingIndices[bindingStart + i];
        BindingState& binding = m_memory.bindingStates[bindingIndex];
        if (binding.is_part_of_composite()) {
            binding.set_initial_state_check_pending(false);
            continue;
        }

        for (int c = 0; c < binding.controlCount; ++c) {
            const int controlIndex = binding.controlStartIndex + c;
            if (controlIndex < 0 || controlIndex >= static_cast<int>(m_controls.size()))
                continue;
            Control* control = m_controls[static_cast<std::size_t>(controlIndex)];
            if (control != nullptr && manager != nullptr) {
                manager->remove_state_change_monitor(
                    control, pack_monitor_index(binding.mapIndex, controlIndex, bindingIndex));
            }
            if (m_controlEnableCounts[static_cast<std::size_t>(controlIndex)] > 0)
                --m_controlEnableCounts[static_cast<std::size_t>(controlIndex)];
            if (m_controlEnableCounts[static_cast<std::size_t>(controlIndex)] == 0)
                m_memory.set_control_enabled(controlIndex, false);
        }
        binding.pressTime = 0.0;
        binding.set_initial_state_check_pending(false);
    }

    reset_action_interactions(action.actionIndex);

    state = TriggerState{};
    state.set_phase(ActionPhase::Disabled);
    state.controlIndex = kInvalidIndex16;
    state.bindingIndex = kInvalidIndex16;
    state.interactionIndex = kInvalidIndex16;

    if (m_enabledActionCount > 0)
        --m_enabledActionCount;
    if (action.mapIndex >= 0 && action.mapIndex < static_cast<int>(m_maps.size()) &&
        m_maps[static_cast<std::size_t>(action.mapIndex)] != nullptr &&
        m_maps[static_cast<std::size_t>(action.mapIndex)]->m_enabledActions > 0)
        --m_maps[static_cast<std::size_t>(action.mapIndex)]->m_enabledActions;

    if (m_enabledActionCount == 0)
        unhook_initial_state_check();

    if (manager != nullptr)
        manager->notify_action_change(&action, ActionChange::ActionDisabled);
}

ActionPhase ActionState::phase(const ActionDef& action) const {
    if (action.actionIndex < 0 || action.actionIndex >= m_memory.actionCount)
        return ActionPhase::Disabled;
    return m_memory.actionStates[action.actionIndex].phase_value();
}

bool ActionState::was_performed_this_frame(const ActionDef& action) const {
    if (manager == nullptr || action.actionIndex < 0)
        return false;
    return m_memory.actionStates[action.actionIndex].lastPerformedInUpdate ==
           manager->update_step_count();
}

bool ActionState::was_pressed_this_frame(const ActionDef& action) const {
    if (manager == nullptr || action.actionIndex < 0)
        return false;
    const TriggerState& state = m_memory.actionStates[action.actionIndex];
    return state.pressedInUpdate == manager->update_step_count() &&
           (state.flags & kTriggerPressed) != 0;
}

bool ActionState::was_released_this_frame(const ActionDef& action) const {
    if (manager == nullptr || action.actionIndex < 0)
        return false;
    const TriggerState& state = m_memory.actionStates[action.actionIndex];
    return state.releasedInUpdate == manager->update_step_count() &&
           (state.flags & kTriggerPressed) == 0;
}

int ActionState::active_binding(const ActionDef& action) const {
    if (action.actionIndex < 0 || action.actionIndex >= m_memory.actionCount)
        return -1;
    const TriggerState& state = m_memory.actionStates[action.actionIndex];
    if (state.bindingIndex != kInvalidIndex16)
        return state.bindingIndex;

    const std::size_t countIndex = static_cast<std::size_t>(action.actionIndex) * 2;
    const int bindingStart = m_memory.actionBindingIndicesAndCounts[countIndex];
    const int bindingCount = m_memory.actionBindingIndicesAndCounts[countIndex + 1];
    for (int i = 0; i < bindingCount; ++i) {
        const int bindingIndex = m_memory.actionBindingIndices[bindingStart + i];
        if (!m_memory.bindingStates[bindingIndex].is_disabled())
            return bindingIndex;
    }
    return -1;
}

bool ActionState::control_alive(int controlIndex) const {
    if (manager == nullptr || controlIndex < 0 ||
        controlIndex >= static_cast<int>(m_controls.size()))
        return false;
    if (m_controls[static_cast<std::size_t>(controlIndex)] == nullptr)
        return false;
    const int deviceId = m_controlDeviceIds[static_cast<std::size_t>(controlIndex)];
    return deviceId > 0 && manager->device_by_id(deviceId) != nullptr;
}

Control* ActionState::live_control(int controlIndex) const {
    return control_alive(controlIndex) ? m_controls[static_cast<std::size_t>(controlIndex)]
                                       : nullptr;
}

Device* ActionState::live_device(int controlIndex) const {
    return control_alive(controlIndex) ? m_controlDevices[static_cast<std::size_t>(controlIndex)]
                                       : nullptr;
}

Control* ActionState::active_control(const ActionDef& action) const {
    if (action.actionIndex < 0 || action.actionIndex >= m_memory.actionCount)
        return nullptr;
    const TriggerState& state = m_memory.actionStates[action.actionIndex];
    if (state.controlIndex != kInvalidIndex16 && control_alive(state.controlIndex))
        return m_controls[state.controlIndex];

    const int bindingIndex = active_binding(action);
    if (bindingIndex < 0)
        return nullptr;
    const BindingState& binding = m_memory.bindingStates[bindingIndex];
    if (binding.controlCount > 0 && control_alive(binding.controlStartIndex))
        return m_controls[binding.controlStartIndex];
    return nullptr;
}

ValueType ActionState::value_type(const ActionDef& action) const {
    const int bindingIndex = active_binding(action);
    if (bindingIndex >= 0) {
        const BindingState& binding = m_memory.bindingStates[bindingIndex];
        if (binding.is_composite() || binding.is_part_of_composite()) {
            const int compositeBindingIndex = binding.is_composite()
                                                  ? bindingIndex
                                                  : binding.compositeOrCompositeBindingIndex;
            if (compositeBindingIndex >= 0) {
                const BindingState& compositeBinding =
                    m_memory.bindingStates[compositeBindingIndex];
                const int compositeIndex = compositeBinding.compositeOrCompositeBindingIndex;
                if (compositeIndex >= 0 && compositeIndex < static_cast<int>(m_composites.size()))
                    return m_composites[static_cast<std::size_t>(compositeIndex)]->value_type();
            }
        }
        if (binding.controlCount > 0 && control_alive(binding.controlStartIndex))
            return value_type_from_control(m_controls[binding.controlStartIndex]);
    }

    if (action.expectedControlType == "Vector2" || action.expectedControlType == "Stick")
        return ValueType::Vector2;
    if (action.expectedControlType == "Vector3")
        return ValueType::Vector3;
    return ValueType::Float;
}

IComposite* ActionState::composite(int compositeIndex) const {
    if (compositeIndex < 0 || compositeIndex >= static_cast<int>(m_composites.size()))
        return nullptr;
    return m_composites[static_cast<std::size_t>(compositeIndex)];
}

void ActionState::read_binding_value(int bindingIndex, void* out, std::size_t size) const {
    if (out == nullptr || size == 0)
        return;
    std::memset(out, 0, size);

    if (bindingIndex < 0 || bindingIndex >= m_memory.bindingCount)
        return;
    const BindingState& binding = m_memory.bindingStates[bindingIndex];
    if (binding.is_disabled())
        return;

    const ProcessorContext context = processor_context();
    if (binding.is_composite()) {
        const int compositeIndex = binding.compositeOrCompositeBindingIndex;
        IComposite* compositeValue = composite(compositeIndex);
        if (compositeValue == nullptr)
            return;
        alignas(16) uint8_t buffer[64] = {};
        CompositeContext compositeContext(const_cast<ActionState&>(*this), bindingIndex);
        compositeValue->read_value(compositeContext, buffer);
        for (int i = 0; i < binding.processorCount; ++i) {
            const int processorIndex = binding.processorStartIndex + i;
            if (processorIndex >= 0 && processorIndex < static_cast<int>(m_processors.size()))
                m_processors[static_cast<std::size_t>(processorIndex)]->process(
                    buffer, sizeof(buffer), nullptr, context);
        }
        std::memcpy(out, buffer, std::min(size, sizeof(buffer)));
        return;
    }

    Control* control = nullptr;
    Device* controlDevice = nullptr;
    float bestMagnitude = -1.0f;
    for (int c = 0; c < binding.controlCount; ++c) {
        const int controlIndex = binding.controlStartIndex + c;
        if (!control_alive(controlIndex))
            continue;
        Control* candidate = m_controls[static_cast<std::size_t>(controlIndex)];
        Device* candidateDevice = live_device(controlIndex);
        if (candidateDevice == nullptr || candidateDevice->current_state() == nullptr)
            continue;
        const float magnitude = candidate->evaluate_magnitude(candidateDevice->current_state());
        if (magnitude > bestMagnitude) {
            bestMagnitude = magnitude;
            control = candidate;
            controlDevice = candidateDevice;
        }
    }
    if (control == nullptr || controlDevice == nullptr || controlDevice->current_state() == nullptr)
        return;

    alignas(16) uint8_t buffer[64] = {};
    control->read_value_from_state(controlDevice->current_state(), buffer);
    const std::size_t valueSize = std::min<std::size_t>(control->value_size_bytes(), sizeof(buffer));
    for (int i = 0; i < binding.processorCount; ++i) {
        const int processorIndex = binding.processorStartIndex + i;
        if (processorIndex >= 0 && processorIndex < static_cast<int>(m_processors.size()))
            m_processors[static_cast<std::size_t>(processorIndex)]->process(
                buffer, valueSize, control, context);
    }
    std::memcpy(out, buffer, std::min(size, valueSize));
}

void ActionState::read_value(const ActionDef& action, void* out, std::size_t size) const {
    if (out == nullptr || size == 0)
        return;
    const int bindingIndex = active_binding(action);
    if (bindingIndex < 0) {
        std::memset(out, 0, size);
        return;
    }
    read_binding_value(bindingIndex, out, size);
}

void ActionState::read_composite_part(int compositeBindingIndex, int partIndex, void* out,
                                      std::size_t size) const {
    if (out == nullptr || size == 0)
        return;
    std::memset(out, 0, size);

    if (compositeBindingIndex < 0 || compositeBindingIndex >= m_memory.bindingCount)
        return;
    const BindingState& binding = m_memory.bindingStates[compositeBindingIndex];
    IComposite* compositeValue = composite(binding.compositeOrCompositeBindingIndex);
    if (compositeValue == nullptr)
        return;
    const int partBinding = compositeValue->part_binding_index(partIndex);
    if (partBinding >= 0)
        read_binding_value(partBinding, out, size);
}

bool ActionState::read_composite_part_as_button(int compositeBindingIndex, int partIndex) const {
    if (compositeBindingIndex < 0 || compositeBindingIndex >= m_memory.bindingCount)
        return false;
    const BindingState& binding = m_memory.bindingStates[compositeBindingIndex];
    IComposite* compositeValue = composite(binding.compositeOrCompositeBindingIndex);
    if (compositeValue == nullptr)
        return false;
    const int partBinding = compositeValue->part_binding_index(partIndex);
    if (partBinding < 0)
        return false;

    float value = 0.0f;
    read_binding_value(partBinding, &value, sizeof(float));
    const BindingState& part = m_memory.bindingStates[partBinding];
    float threshold = kDefaultButtonPressPoint;
    if (part.controlCount > 0 && control_alive(part.controlStartIndex)) {
        if (auto* button = dynamic_cast<ButtonControl*>(m_controls[part.controlStartIndex]))
            threshold = button->pressPoint;
    }
    return value >= threshold;
}

float ActionState::read_composite_part_magnitude(int compositeBindingIndex,
                                                  int partIndex) const {
    if (compositeBindingIndex < 0 || compositeBindingIndex >= m_memory.bindingCount)
        return 0.0f;
    const BindingState& binding = m_memory.bindingStates[compositeBindingIndex];
    IComposite* compositeValue = composite(binding.compositeOrCompositeBindingIndex);
    if (compositeValue == nullptr)
        return 0.0f;
    const int partBinding = compositeValue->part_binding_index(partIndex);
    if (partBinding < 0)
        return 0.0f;

    const BindingState& part = m_memory.bindingStates[partBinding];
    float magnitude = 0.0f;
    for (int c = 0; c < part.controlCount; ++c) {
        const int controlIndex = part.controlStartIndex + c;
        Control* control = live_control(controlIndex);
        Device* device = live_device(controlIndex);
        if (control == nullptr || device == nullptr || device->current_state() == nullptr)
            continue;
        magnitude = std::max(magnitude, control->evaluate_magnitude(device->current_state()));
    }
    return magnitude;
}

double ActionState::read_composite_part_press_time(int compositeBindingIndex,
                                                    int partIndex) const {
    if (compositeBindingIndex < 0 || compositeBindingIndex >= m_memory.bindingCount)
        return -1.0;
    const BindingState& binding = m_memory.bindingStates[compositeBindingIndex];
    IComposite* compositeValue = composite(binding.compositeOrCompositeBindingIndex);
    if (compositeValue == nullptr)
        return -1.0;
    const int partBinding = compositeValue->part_binding_index(partIndex);
    if (partBinding < 0)
        return -1.0;
    const double pressTime = m_memory.bindingStates[partBinding].pressTime;
    return pressTime > 0.0 ? pressTime : -1.0;
}

float ActionState::composite_magnitude(int compositeIndex) const {
    if (compositeIndex < 0 || compositeIndex >= m_memory.compositeCount)
        return 0.0f;
    return m_memory.compositeMagnitudes[compositeIndex];
}

ActionDef* ActionState::action_def(int actionIndex) const {
    if (actionIndex < 0 || actionIndex >= static_cast<int>(m_actions.size()))
        return nullptr;
    return m_actions[static_cast<std::size_t>(actionIndex)];
}

BindingState* ActionState::binding_state(int bindingIndex) {
    if (bindingIndex < 0 || bindingIndex >= m_memory.bindingCount)
        return nullptr;
    return &m_memory.bindingStates[bindingIndex];
}

const BindingState* ActionState::binding_state(int bindingIndex) const {
    if (bindingIndex < 0 || bindingIndex >= m_memory.bindingCount)
        return nullptr;
    return &m_memory.bindingStates[bindingIndex];
}

TriggerState* ActionState::trigger_state(int actionIndex) {
    if (actionIndex < 0 || actionIndex >= m_memory.actionCount)
        return nullptr;
    return &m_memory.actionStates[actionIndex];
}

const TriggerState* ActionState::trigger_state(int actionIndex) const {
    if (actionIndex < 0 || actionIndex >= m_memory.actionCount)
        return nullptr;
    return &m_memory.actionStates[actionIndex];
}

void ActionState::notify_state_changed(Control* control, double time, low::Event* event,
                                       int64_t monitorIndex) {
    (void)control;
    int mapIndex = 0;
    int controlIndex = 0;
    int bindingIndex = 0;
    unpack_monitor_index(monitorIndex, mapIndex, controlIndex, bindingIndex);
    process_control_state_change(mapIndex, controlIndex, bindingIndex, time, event);
}

void ActionState::notify_timer_expired(Control* control, double time, int64_t monitorIndex,
                                       int timerIndex) {
    if (manager == nullptr || timerIndex < 0 ||
        timerIndex >= static_cast<int>(m_interactions.size()))
        return;
    if (timerIndex >= m_memory.interactionCount)
        return;

    InteractionState& interactionState = m_memory.interactionStates[timerIndex];
    if (interactionState.phase == static_cast<uint8_t>(ActionPhase::Waiting))
        return;

    int mapIndex = 0;
    int controlIndex = 0;
    int bindingIndex = 0;
    unpack_monitor_index(monitorIndex, mapIndex, controlIndex, bindingIndex);
    (void)mapIndex;
    if (bindingIndex < 0 || bindingIndex >= m_memory.bindingCount)
        return;
    const BindingState& binding = m_memory.bindingStates[bindingIndex];
    if (binding.is_disabled() || binding.actionIndex == kInvalidIndex16)
        return;
    ActionDef* action = action_def(binding.actionIndex);
    if (action == nullptr || action->state != this)
        return;
    if (m_memory.actionStates[binding.actionIndex].phase_value() == ActionPhase::Disabled)
        return;

    interactionState.flags &= static_cast<uint8_t>(~kInteractionTimerRunning);

    InteractionContext context{*this,
                               static_cast<ActionPhase>(interactionState.phase),
                               ActionPhase::Waiting,
                               ActionPhase::Waiting,
                               binding.actionIndex,
                               bindingIndex,
                               interactionState.triggerControlIndex != kInvalidIndex16
                                   ? interactionState.triggerControlIndex
                                   : static_cast<int>(controlIndex),
                               timerIndex,
                               time,
                               interactionState.startTime,
                               true};
    context.magnitude = m_memory.actionStates[binding.actionIndex].magnitude;
    (void)control;
    m_interactions[static_cast<std::size_t>(timerIndex)]->process(context);
}

void ActionState::process_control_state_change(int mapIndex, int controlIndex, int bindingIndex,
                                               double time, low::Event* event) {
    if (m_inProcess || manager == nullptr)
        return;
    if (bindingIndex < 0 || bindingIndex >= m_memory.bindingCount)
        return;

    BindingState& binding = m_memory.bindingStates[bindingIndex];
    if (binding.is_disabled() || binding.actionIndex == kInvalidIndex16)
        return;

    BindingState* partBinding = binding.is_part_of_composite() ? &binding : nullptr;
    if (binding.is_part_of_composite()) {
        bindingIndex = binding.compositeOrCompositeBindingIndex;
        if (bindingIndex < 0 || bindingIndex >= m_memory.bindingCount)
            return;
    }

    const int actionIndex = m_memory.bindingStates[bindingIndex].actionIndex;
    ActionDef* action = action_def(actionIndex);
    if (action == nullptr || action->state != this)
        return;
    TriggerState& actionState = m_memory.actionStates[actionIndex];
    if (actionState.phase_value() == ActionPhase::Disabled)
        return;

    m_inProcess = true;

    BindingState& resolvedBinding = m_memory.bindingStates[bindingIndex];
    resolvedBinding.set_initial_state_check_pending(false);

    if (partBinding == nullptr && resolvedBinding.is_composite()) {
        IComposite* compositeValue =
            composite(resolvedBinding.compositeOrCompositeBindingIndex);
        if (compositeValue != nullptr) {
            for (std::size_t part = 0; part < compositeValue->partBindingIndices.size(); ++part) {
                const int candidateIndex = compositeValue->part_binding_index(
                    static_cast<int>(part));
                if (candidateIndex < 0 || candidateIndex >= m_memory.bindingCount)
                    continue;
                BindingState& candidate = m_memory.bindingStates[candidateIndex];
                if (controlIndex >= candidate.controlStartIndex &&
                    controlIndex < candidate.controlStartIndex + candidate.controlCount) {
                    partBinding = &candidate;
                    break;
                }
            }
        }
    }

    TriggerState trigger;
    trigger.mapIndex = static_cast<uint8_t>(mapIndex);
    trigger.controlIndex = static_cast<uint16_t>(controlIndex);
    trigger.bindingIndex = static_cast<uint16_t>(bindingIndex);
    trigger.interactionIndex = kInvalidIndex16;
    trigger.time = time;
    trigger.startTime = time;
    trigger.flags = 0;
    if (action->is_button())
        trigger.flags |= kTriggerButton;
    if (action->is_pass_through())
        trigger.flags |= kTriggerPassThrough;

    Control* control = live_control(controlIndex);
    Device* controlDevice = live_device(controlIndex);
    if (control == nullptr || controlDevice == nullptr ||
        controlDevice->current_state() == nullptr) {
        m_inProcess = false;
        return;
    }

    trigger.magnitude = control->evaluate_magnitude(controlDevice->current_state());
    m_memory.controlMagnitudes[controlIndex] = trigger.magnitude;

    const float threshold = press_threshold(control, resolvedBinding);
    if (trigger.magnitude >= threshold) {
        if (resolvedBinding.pressTime == 0.0 || resolvedBinding.pressTime > time)
            resolvedBinding.pressTime = time;
    }

    if (partBinding != nullptr) {
        const float partThreshold = press_threshold(control, *partBinding);
        if (trigger.magnitude >= partThreshold) {
            if (partBinding->pressTime == 0.0 || partBinding->pressTime > time)
                partBinding->pressTime = time;
        } else if (trigger.magnitude <= partThreshold * kButtonReleaseThreshold) {
            partBinding->pressTime = 0.0;
        }
    }

    bool haveInteractionsOnComposite = false;
    if (resolvedBinding.is_composite()) {
        if (event != nullptr && event->eventId != 0 &&
            resolvedBinding.triggerEventIdForComposite == event->eventId) {
            m_inProcess = false;
            return;
        }
        if (event != nullptr)
            resolvedBinding.triggerEventIdForComposite = event->eventId;

        CompositeContext context(*this, bindingIndex);
        IComposite* compositeValue = composite(resolvedBinding.compositeOrCompositeBindingIndex);
        if (compositeValue != nullptr) {
            trigger.magnitude = compositeValue->evaluate_magnitude(context);
            m_memory.compositeMagnitudes[resolvedBinding.compositeOrCompositeBindingIndex] =
                trigger.magnitude;
        }
        haveInteractionsOnComposite = resolvedBinding.interactionCount > 0;
    }

    const bool conflicting = is_conflicting_input(trigger, actionIndex);
    if (!conflicting)
        process_button_state(trigger, actionIndex);

    if (resolvedBinding.interactionCount > 0 && !resolvedBinding.is_part_of_composite())
        process_interactions(trigger, resolvedBinding.interactionStartIndex,
                             resolvedBinding.interactionCount);
    else if (!haveInteractionsOnComposite && !conflicting)
        process_default_interaction(trigger, actionIndex);

    m_inProcess = false;
}

float ActionState::press_threshold(const Control* control, const BindingState& binding) const {
    (void)binding;
    if (auto* button = dynamic_cast<const ButtonControl*>(control))
        return button->pressPoint > 0.0f ? button->pressPoint : kDefaultButtonPressPoint;
    return kDefaultButtonPressPoint;
}

bool ActionState::is_actuated(const TriggerState& trigger) const {
    return trigger.magnitude > 0.0f;
}

int ActionState::trigger_control_index(const TriggerState& trigger) const {
    if (trigger.bindingIndex == kInvalidIndex16)
        return -1;
    const BindingState& binding = m_memory.bindingStates[trigger.bindingIndex];
    if (binding.is_composite())
        return binding.controlStartIndex;
    return trigger.controlIndex;
}

int ActionState::action_control_index(const TriggerState& trigger,
                                      const TriggerState& actionState) const {
    (void)trigger;
    if (actionState.controlIndex == kInvalidIndex16)
        return -1;
    if (actionState.bindingIndex == kInvalidIndex16)
        return actionState.controlIndex;
    const BindingState& binding = m_memory.bindingStates[actionState.bindingIndex];
    if (binding.is_composite())
        return binding.controlStartIndex;
    return actionState.controlIndex;
}

bool ActionState::is_conflicting_input(TriggerState& trigger, int actionIndex) {
    ActionDef* action = action_def(actionIndex);
    if (action == nullptr || action->is_pass_through())
        return false;

    const std::size_t countIndex = static_cast<std::size_t>(actionIndex) * 2;
    const int bindingStart = m_memory.actionBindingIndicesAndCounts[countIndex];
    const int bindingCount = m_memory.actionBindingIndicesAndCounts[countIndex + 1];

    int actuationCandidates = 0;
    for (int i = 0; i < bindingCount; ++i) {
        const int bindingIndex = m_memory.actionBindingIndices[bindingStart + i];
        const BindingState& binding = m_memory.bindingStates[bindingIndex];
        if (binding.is_disabled() || binding.is_part_of_composite())
            continue;
        if (binding.is_composite())
            ++actuationCandidates;
        else
            actuationCandidates += std::max<int>(1, binding.controlCount);
    }
    if (actuationCandidates <= 1)
        return false;

    TriggerState& actionState = m_memory.actionStates[actionIndex];
    const int currentControl = action_control_index(trigger, actionState);
    const int triggeredControl = trigger_control_index(trigger);
    if (currentControl < 0) {
        actionState.magnitude = trigger.magnitude;
        return false;
    }

    const bool driving = triggeredControl == currentControl;
    if (trigger.magnitude > actionState.magnitude) {
        if (trigger.magnitude > 0.0f && !driving && actionState.magnitude > 0.0f)
            actionState.flags |= kTriggerMultipleConcurrentActuations;
        actionState.magnitude = trigger.magnitude;
        return false;
    }

    if (trigger.magnitude < actionState.magnitude) {
        if (!driving) {
            if (trigger.magnitude > 0.0f)
                actionState.flags |= kTriggerMultipleConcurrentActuations;
            return true;
        }
        if ((actionState.flags & kTriggerMultipleConcurrentActuations) == 0) {
            actionState.magnitude = trigger.magnitude;
            return false;
        }

        float highest = trigger.magnitude;
        int bestControl = -1;
        int bestBinding = -1;
        for (int i = 0; i < bindingCount; ++i) {
            const int bindingIndex = m_memory.actionBindingIndices[bindingStart + i];
            const BindingState& binding = m_memory.bindingStates[bindingIndex];
            if (binding.is_composite()) {
                const float magnitude =
                    m_memory.compositeMagnitudes[binding.compositeOrCompositeBindingIndex];
                if (magnitude > highest) {
                    highest = magnitude;
                    bestControl = binding.controlStartIndex;
                    bestBinding = bindingIndex;
                }
            } else if (!binding.is_part_of_composite()) {
                for (int c = 0; c < binding.controlCount; ++c) {
                    const int controlIndex = binding.controlStartIndex + c;
                    const float magnitude = m_memory.controlMagnitudes[controlIndex];
                    if (magnitude > highest) {
                        highest = magnitude;
                        bestControl = controlIndex;
                        bestBinding = bindingIndex;
                    }
                }
            }
        }

        if (bestControl >= 0) {
            trigger.controlIndex = static_cast<uint16_t>(bestControl);
            trigger.bindingIndex = static_cast<uint16_t>(bestBinding);
            trigger.magnitude = highest;
            actionState.controlIndex = static_cast<uint16_t>(bestControl);
            actionState.bindingIndex = static_cast<uint16_t>(bestBinding);
            actionState.magnitude = highest;
            return false;
        }
        return false;
    }

    if (!driving && std::fabs(trigger.magnitude - actionState.magnitude) <= kEpsilon) {
        if (trigger.magnitude > 0.0f)
            actionState.flags |= kTriggerMultipleConcurrentActuations;
        return true;
    }
    return false;
}

void ActionState::process_button_state(TriggerState& trigger, int actionIndex) {
    (void)actionIndex;
    if (trigger.bindingIndex == kInvalidIndex16 || trigger.controlIndex == kInvalidIndex16)
        return;
    BindingState& binding = m_memory.bindingStates[trigger.bindingIndex];
    Control* control = live_control(trigger.controlIndex);
    const float threshold = press_threshold(control, binding);
    const float controlActuation = m_memory.controlMagnitudes[trigger.controlIndex];
    if (controlActuation <= threshold * kButtonReleaseThreshold)
        binding.pressTime = 0.0;

    TriggerState& actionState = m_memory.actionStates[actionIndex];
    const bool pressed = (actionState.flags & kTriggerPressed) != 0;
    if (!pressed && trigger.magnitude >= threshold) {
        actionState.flags |= kTriggerPressed;
        actionState.framePressed = static_cast<int32_t>(manager->update_step_count());
        actionState.pressedInUpdate = manager->update_step_count();
    } else if (pressed && trigger.magnitude <= threshold * kButtonReleaseThreshold) {
        actionState.flags &= static_cast<uint8_t>(~kTriggerPressed);
        actionState.frameReleased = static_cast<int32_t>(manager->update_step_count());
        actionState.releasedInUpdate = manager->update_step_count();
    }
}

void ActionState::process_default_interaction(TriggerState& trigger, int actionIndex) {
    ActionDef* action = action_def(actionIndex);
    if (action == nullptr)
        return;

    TriggerState& actionState = m_memory.actionStates[actionIndex];
    const ActionPhase phase = actionState.phase_value();

    if (phase == ActionPhase::Waiting) {
        if (action->is_pass_through()) {
            change_phase_of_action(ActionPhase::Performed, trigger, ActionPhase::Waiting);
            return;
        }
        if (action->is_button()) {
            if (trigger.magnitude > 0.0f)
                change_phase_of_action(ActionPhase::Started, trigger);
            const BindingState& binding = m_memory.bindingStates[trigger.bindingIndex];
            Control* control = live_control(trigger.controlIndex);
            if (trigger.magnitude >= press_threshold(control, binding))
                change_phase_of_action(ActionPhase::Performed, trigger, ActionPhase::Performed);
        } else {
            if (is_actuated(trigger)) {
                change_phase_of_action(ActionPhase::Started, trigger);
                change_phase_of_action(ActionPhase::Performed, trigger, ActionPhase::Started);
            }
        }
    } else if (phase == ActionPhase::Started) {
        if (action->is_button()) {
            const BindingState& binding = m_memory.bindingStates[trigger.bindingIndex];
            Control* control = live_control(trigger.controlIndex);
            if (trigger.magnitude >= press_threshold(control, binding))
                change_phase_of_action(ActionPhase::Performed, trigger, ActionPhase::Performed);
            else if (approximately_zero(trigger.magnitude))
                change_phase_of_action(ActionPhase::Canceled, trigger);
        } else {
            if (!is_actuated(trigger))
                change_phase_of_action(ActionPhase::Canceled, trigger);
            else
                change_phase_of_action(ActionPhase::Performed, trigger, ActionPhase::Started);
        }
    } else if (phase == ActionPhase::Performed) {
        if (action->is_button()) {
            const BindingState& binding = m_memory.bindingStates[trigger.bindingIndex];
            Control* control = live_control(trigger.controlIndex);
            const float pressPoint = press_threshold(control, binding);
            if (approximately_zero(trigger.magnitude))
                change_phase_of_action(ActionPhase::Canceled, trigger);
            else if (trigger.magnitude <= pressPoint * kButtonReleaseThreshold)
                change_phase_of_action(ActionPhase::Started, trigger);
        } else if (action->is_pass_through()) {
            change_phase_of_action(ActionPhase::Performed, trigger, ActionPhase::Performed);
        }
    }
}

void ActionState::change_phase_of_action(ActionPhase newPhase, TriggerState& trigger,
                                         ActionPhase phaseAfterPerformedOrCanceled) {
    if (newPhase == ActionPhase::Disabled || trigger.bindingIndex == kInvalidIndex16)
        return;
    if (trigger.bindingIndex >= m_memory.bindingCount)
        return;
    const int actionIndex = m_memory.bindingStates[trigger.bindingIndex].actionIndex;
    if (actionIndex == kInvalidIndex16 || actionIndex >= m_memory.actionCount)
        return;

    TriggerState& actionState = m_memory.actionStates[actionIndex];
    if (actionState.phase_value() == ActionPhase::Disabled)
        return;
    if ((actionState.flags & kTriggerInProcessing) != 0)
        return;

    actionState.flags |= kTriggerInProcessing;

    if ((trigger.flags & kTriggerPassThrough) != 0 && trigger.interactionIndex == kInvalidIndex16) {
        change_phase_of_action_internal(actionIndex, trigger, newPhase, phaseAfterPerformedOrCanceled);
        if ((m_memory.actionStates[actionIndex].flags & kTriggerInProcessing) != 0)
            m_memory.actionStates[actionIndex].phase =
                static_cast<uint8_t>(phaseAfterPerformedOrCanceled);
    } else if (newPhase == ActionPhase::Performed &&
               actionState.phase_value() == ActionPhase::Waiting) {
        change_phase_of_action_internal(actionIndex, trigger, ActionPhase::Started,
                                        phaseAfterPerformedOrCanceled);
        if (m_memory.actionStates[actionIndex].phase_value() != ActionPhase::Disabled &&
            (m_memory.actionStates[actionIndex].flags & kTriggerInProcessing) != 0) {
            change_phase_of_action_internal(actionIndex, trigger, ActionPhase::Performed,
                                            phaseAfterPerformedOrCanceled);
            if (phaseAfterPerformedOrCanceled == ActionPhase::Waiting &&
                m_memory.actionStates[actionIndex].phase_value() != ActionPhase::Disabled &&
                (m_memory.actionStates[actionIndex].flags & kTriggerInProcessing) != 0) {
                change_phase_of_action_internal(actionIndex, trigger, ActionPhase::Canceled,
                                                phaseAfterPerformedOrCanceled);
            }
            if (m_memory.actionStates[actionIndex].phase_value() != ActionPhase::Disabled)
                m_memory.actionStates[actionIndex].phase =
                    static_cast<uint8_t>(phaseAfterPerformedOrCanceled);
        }
    } else if (actionState.phase_value() != newPhase || newPhase == ActionPhase::Performed) {
        change_phase_of_action_internal(actionIndex, trigger, newPhase,
                                        phaseAfterPerformedOrCanceled);
        if (m_memory.actionStates[actionIndex].phase_value() != ActionPhase::Disabled &&
            (newPhase == ActionPhase::Performed || newPhase == ActionPhase::Canceled))
            m_memory.actionStates[actionIndex].phase =
                static_cast<uint8_t>(phaseAfterPerformedOrCanceled);
    }

    TriggerState& finalState = m_memory.actionStates[actionIndex];
    finalState.flags &= static_cast<uint8_t>(~kTriggerInProcessing);
    if (finalState.phase_value() == ActionPhase::Waiting) {
        finalState.controlIndex = kInvalidIndex16;
        finalState.interactionIndex = kInvalidIndex16;
        finalState.magnitude = 0.0f;
        finalState.flags &= static_cast<uint8_t>(~kTriggerHaveMagnitude);
    }
}

void ActionState::change_phase_of_interaction(ActionPhase newPhase,
                                               InteractionContext& context,
                                               ActionPhase phaseAfterPerformed,
                                               ActionPhase phaseAfterCanceled) {
    const int interactionIndex = context.interactionIndex;
    if (interactionIndex < 0 || interactionIndex >= m_memory.interactionCount)
        return;

    InteractionState& interactionState = m_memory.interactionStates[interactionIndex];
    if ((interactionState.flags & kInteractionTimerRunning) != 0)
        stop_timeout(interactionIndex);

    context.interactionPhase = newPhase;
    if (newPhase == ActionPhase::Started)
        context.startedAt = context.currentTime;

    interactionState.phase = static_cast<uint8_t>(newPhase);
    if (context.controlIndex >= 0)
        interactionState.triggerControlIndex = static_cast<uint16_t>(context.controlIndex);
    interactionState.startTime = context.startTime();
    if (newPhase == ActionPhase::Performed)
        interactionState.performedTime = context.time();

    int actionIndex = context.actionIndex;
    if (actionIndex < 0 || actionIndex >= m_memory.actionCount) {
        if (newPhase == ActionPhase::Performed || newPhase == ActionPhase::Canceled)
            reset_interaction_state(interactionIndex);
        return;
    }

    ActionDef* action = action_def(actionIndex);
    TriggerState& actionState = m_memory.actionStates[actionIndex];
    if (action == nullptr || actionState.phase_value() == ActionPhase::Disabled) {
        reset_interaction_state(interactionIndex);
        return;
    }

    TriggerState trigger;
    trigger.set_phase(newPhase);
    trigger.controlIndex = context.controlIndex >= 0
                               ? static_cast<uint16_t>(context.controlIndex)
                               : kInvalidIndex16;
    trigger.bindingIndex = context.bindingIndex >= 0
                               ? static_cast<uint16_t>(context.bindingIndex)
                               : kInvalidIndex16;
    trigger.interactionIndex = static_cast<uint16_t>(interactionIndex);
    trigger.time = context.time();
    trigger.startTime = context.startTime();
    trigger.magnitude =
        interactionState.triggerControlIndex != kInvalidIndex16 &&
                interactionState.triggerControlIndex < m_memory.controlCount
            ? m_memory.controlMagnitudes[interactionState.triggerControlIndex]
            : 0.0f;
    if (trigger.magnitude == 0.0f) {
        const TriggerState* actionTrigger = trigger_state(actionIndex);
        if (actionTrigger != nullptr)
            trigger.magnitude = actionTrigger->magnitude;
    }

    const ActionPhase actionPhase = actionState.phase_value();
    const bool driving =
        actionState.interactionIndex == static_cast<uint16_t>(interactionIndex);

    if (newPhase == ActionPhase::Waiting) {
        if (driving)
            change_phase_of_action(ActionPhase::Canceled, trigger, phaseAfterCanceled);
    } else if (actionPhase == ActionPhase::Waiting) {
        change_phase_of_action(newPhase, trigger,
                               newPhase == ActionPhase::Performed ? phaseAfterPerformed
                                                                  : phaseAfterCanceled);
    } else if (driving) {
        change_phase_of_action(newPhase, trigger,
                               newPhase == ActionPhase::Performed ? phaseAfterPerformed
                                                                  : phaseAfterCanceled);
    }

    if (newPhase == ActionPhase::Performed && phaseAfterPerformed != ActionPhase::Waiting) {
        interactionState.phase = static_cast<uint8_t>(phaseAfterPerformed);
    } else if (newPhase == ActionPhase::Performed || newPhase == ActionPhase::Canceled ||
               newPhase == ActionPhase::Waiting) {
        reset_interaction_state(interactionIndex);
    }
}

void ActionState::reset_interaction_state(int interactionIndex) {
    if (interactionIndex < 0 || interactionIndex >= m_memory.interactionCount)
        return;
    stop_timeout(interactionIndex);

    InteractionState& interactionState = m_memory.interactionStates[interactionIndex];
    interactionState = InteractionState{};
    if (interactionIndex < static_cast<int>(m_interactions.size()) &&
        m_interactions[static_cast<std::size_t>(interactionIndex)] != nullptr) {
        m_interactions[static_cast<std::size_t>(interactionIndex)]->reset();
    }
}

void ActionState::reset_action_interactions(int actionIndex) {
    if (actionIndex < 0 || actionIndex >= m_memory.actionCount)
        return;
    const std::size_t countIndex = static_cast<std::size_t>(actionIndex) * 2;
    const int bindingStart = m_memory.actionBindingIndicesAndCounts[countIndex];
    const int bindingCount = m_memory.actionBindingIndicesAndCounts[countIndex + 1];
    std::vector<bool> reset(static_cast<std::size_t>(m_memory.interactionCount), false);
    for (int i = 0; i < bindingCount; ++i) {
        const int bindingIndex = m_memory.actionBindingIndices[bindingStart + i];
        if (bindingIndex < 0 || bindingIndex >= m_memory.bindingCount)
            continue;
        const BindingState& binding = m_memory.bindingStates[bindingIndex];
        if (binding.interactionStartIndex == kInvalidIndex16)
            continue;
        for (int j = 0; j < binding.interactionCount; ++j) {
            const int interactionIndex = binding.interactionStartIndex + j;
            if (interactionIndex < 0 || interactionIndex >= m_memory.interactionCount ||
                reset[static_cast<std::size_t>(interactionIndex)])
                continue;
            reset[static_cast<std::size_t>(interactionIndex)] = true;
            reset_interaction_state(interactionIndex);
        }
    }
}

void ActionState::change_phase_of_action_internal(int actionIndex, TriggerState& trigger,
                                                  ActionPhase newPhase,
                                                  ActionPhase phaseAfterPerformedOrCanceled) {
    (void)phaseAfterPerformedOrCanceled;
    TriggerState& actionState = m_memory.actionStates[actionIndex];
    ActionDef* action = action_def(actionIndex);
    if (action == nullptr)
        return;

    actionState.controlIndex = trigger.controlIndex;
    actionState.bindingIndex = trigger.bindingIndex;
    actionState.interactionIndex = trigger.interactionIndex;
    actionState.mapIndex = trigger.mapIndex;
    actionState.time = trigger.time;
    actionState.flags = static_cast<uint8_t>(
        (actionState.flags & (kTriggerPassThrough | kTriggerButton |
                              kTriggerMultipleConcurrentActuations | kTriggerPressed)) |
        kTriggerInProcessing);
    actionState.magnitude = newPhase == ActionPhase::Canceled ? 0.0f : trigger.magnitude;
    if (newPhase == ActionPhase::Started)
        actionState.startTime = trigger.time;

    const uint32_t step = manager != nullptr ? manager->update_step_count() : 0;
    switch (newPhase) {
    case ActionPhase::Started:
        actionState.set_phase(newPhase);
        break;
    case ActionPhase::Performed:
        actionState.set_phase(newPhase);
        actionState.lastPerformedInUpdate = step;
        actionState.framePerformed = static_cast<int32_t>(step);
        if (actionState.phase_value() != ActionPhase::Performed)
            actionState.lastCompletedInUpdate = step;
        break;
    case ActionPhase::Canceled:
        actionState.set_phase(newPhase);
        actionState.lastCanceledInUpdate = step;
        actionState.frameCompleted = static_cast<int32_t>(step);
        actionState.lastCompletedInUpdate = step;
        break;
    case ActionPhase::Waiting:
        actionState.set_phase(newPhase);
        break;
    default:
        break;
    }

    CallbackContext context;
    context.state = this;
    context.action = action;
    context.phase = newPhase;
    context.control = live_control(trigger.controlIndex);
    context.time = trigger.time;
    context.startTime = actionState.startTime;

    if (newPhase == ActionPhase::Started && action->started)
        action->started(context);
    else if (newPhase == ActionPhase::Performed && action->performed)
        action->performed(context);
    else if (newPhase == ActionPhase::Canceled && action->canceled)
        action->canceled(context);

    if (manager != nullptr) {
        switch (newPhase) {
        case ActionPhase::Started:
            manager->notify_action_change(action, ActionChange::ActionStarted);
            break;
        case ActionPhase::Performed:
            manager->notify_action_change(action, ActionChange::ActionPerformed);
            break;
        case ActionPhase::Canceled:
            manager->notify_action_change(action, ActionChange::ActionCanceled);
            break;
        default:
            break;
        }
    }
}

void ActionState::process_interactions(TriggerState& trigger, int interactionStartIndex,
                                       int interactionCount) {
    for (int i = 0; i < interactionCount; ++i) {
        const int interactionIndex = interactionStartIndex + i;
        if (interactionIndex < 0 ||
            interactionIndex >= static_cast<int>(m_interactions.size()))
            continue;

        InteractionContext context{
            *this,
            static_cast<ActionPhase>(m_memory.interactionStates[interactionIndex].phase),
            ActionPhase::Waiting,
            ActionPhase::Waiting,
            m_memory.bindingStates[trigger.bindingIndex].actionIndex,
            trigger.bindingIndex,
            trigger.controlIndex,
            interactionIndex,
            trigger.time,
            m_memory.interactionStates[interactionIndex].startTime,
            false};
        context.magnitude = trigger.magnitude;
        m_interactions[static_cast<std::size_t>(interactionIndex)]->process(context);
    }
}

void ActionState::set_composite_part_press_times(int compositeBindingIndex, double time) {
    if (compositeBindingIndex < 0 || compositeBindingIndex >= m_memory.bindingCount)
        return;
    const BindingState& binding = m_memory.bindingStates[compositeBindingIndex];
    IComposite* compositeValue = composite(binding.compositeOrCompositeBindingIndex);
    if (compositeValue == nullptr)
        return;

    for (std::size_t part = 0; part < compositeValue->partBindingIndices.size(); ++part) {
        const int partBindingIndex = compositeValue->part_binding_index(static_cast<int>(part));
        if (partBindingIndex < 0 || partBindingIndex >= m_memory.bindingCount)
            continue;
        BindingState& partBinding = m_memory.bindingStates[partBindingIndex];
        if (partBinding.is_disabled())
            continue;

        for (int c = 0; c < partBinding.controlCount; ++c) {
            const int controlIndex = partBinding.controlStartIndex + c;
            Control* control = live_control(controlIndex);
            Device* device = live_device(controlIndex);
            if (control == nullptr || device == nullptr || device->current_state() == nullptr)
                continue;
            const float magnitude = control->evaluate_magnitude(device->current_state());
            const float threshold = press_threshold(control, partBinding);
            if (magnitude >= threshold) {
                if (partBinding.pressTime == 0.0 || partBinding.pressTime > time)
                    partBinding.pressTime = time;
            } else if (magnitude <= threshold * kButtonReleaseThreshold) {
                partBinding.pressTime = 0.0;
            }
        }
    }
}

void ActionState::on_before_update() {
    if (manager == nullptr || !m_initialStateCheckHooked)
        return;

    unhook_initial_state_check();
    const double time = manager->runtime().now();

    for (int bindingIndex = 0; bindingIndex < m_memory.bindingCount; ++bindingIndex) {
        BindingState& binding = m_memory.bindingStates[bindingIndex];
        if (!binding.initial_state_check_pending())
            continue;
        binding.set_initial_state_check_pending(false);
        if (binding.is_disabled())
            continue;

        if (binding.is_composite())
            set_composite_part_press_times(bindingIndex, time);

        for (int c = 0; c < binding.controlCount; ++c) {
            const int controlIndex = binding.controlStartIndex + c;
            if (controlIndex < 0 || controlIndex >= static_cast<int>(m_controls.size()))
                continue;
            Control* control = live_control(controlIndex);
            Device* controlDevice = live_device(controlIndex);
            if (control == nullptr || controlDevice == nullptr ||
                controlDevice->current_state() == nullptr)
                continue;
            if (control->evaluate_magnitude(controlDevice->current_state()) <= 0.0f)
                continue;

            const float threshold = press_threshold(control, binding);
            if (control->evaluate_magnitude(controlDevice->current_state()) >= threshold) {
                if (binding.pressTime == 0.0 || binding.pressTime > time)
                    binding.pressTime = time;
            }

            manager->signal_state_change_monitor(
                control, this, pack_monitor_index(binding.mapIndex, controlIndex, bindingIndex),
                time);
            if (binding.is_composite())
                break;
        }
    }
}

void ActionState::hook_initial_state_check() {
    if (manager == nullptr || m_initialStateCheckHooked)
        return;
    manager->add_before_update_listener(this);
    m_initialStateCheckHooked = true;
}

void ActionState::unhook_initial_state_check() {
    if (manager == nullptr || !m_initialStateCheckHooked)
        return;
    manager->remove_before_update_listener(this);
    m_initialStateCheckHooked = false;
}

bool ActionState::re_resolve() {
    if (manager == nullptr)
        return false;

    std::vector<Device*> devices;
    const std::vector<Device*>* deviceFilter = nullptr;
    if (m_hasDeviceFilter) {
        devices.reserve(m_deviceFilterIds.size());
        for (int deviceId : m_deviceFilterIds) {
            if (Device* device = manager->device_by_id(deviceId))
                devices.push_back(device);
        }
        deviceFilter = &devices;
    }
    Binding bindingMaskCopy;
    const Binding* bindingMask = nullptr;
    if (m_bindingMask.has_value()) {
        bindingMaskCopy = m_bindingMask.value();
        bindingMask = &bindingMaskCopy;
    }
    return re_resolve(deviceFilter, bindingMask);
}

bool ActionState::re_resolve(const std::vector<Device*>* devices,
                             const Binding* bindingMask) {
    if (manager == nullptr)
        return false;

    Manager* currentManager = manager;
    manager->notify_action_change(m_actions.empty() ? nullptr : m_actions.front(),
                                  ActionChange::BoundControlsAboutToChange);
    std::vector<ActionMap*> currentMaps = m_maps;
    std::vector<bool> wasEnabled(currentMaps.size(), false);
    for (std::size_t i = 0; i < currentMaps.size(); ++i) {
        if (currentMaps[i] != nullptr) {
            wasEnabled[i] = currentMaps[i]->enabled();
            disable_all(*currentMaps[i]);
        }
    }

    if (!resolve_and_initialize(*currentManager, currentMaps, devices, bindingMask))
        return false;

    for (std::size_t i = 0; i < currentMaps.size() && i < wasEnabled.size(); ++i) {
        if (wasEnabled[i] && currentMaps[i] != nullptr)
            enable_all(*currentMaps[i]);
    }

    manager->notify_action_change(m_actions.empty() ? nullptr : m_actions.front(),
                                  ActionChange::BoundControlsChanged);
    return true;
}

int ActionState::binding_global_index(int mapIndex, int localBindingIndex) const {
    if (mapIndex < 0 || mapIndex >= m_memory.mapCount || localBindingIndex < 0)
        return -1;
    const ActionMapIndices& indices = m_memory.mapIndices[mapIndex];
    if (localBindingIndex >= indices.bindingCount)
        return -1;
    return indices.bindingStart + localBindingIndex;
}

void ActionState::update_enabled_count(int delta) {
    m_enabledActionCount += delta;
    if (m_enabledActionCount < 0)
        m_enabledActionCount = 0;
}

void ActionState::start_timeout(float seconds, TriggerState& trigger) {
    if (manager == nullptr || trigger.interactionIndex == kInvalidIndex16 ||
        trigger.interactionIndex >= m_memory.interactionCount)
        return;

    const int interactionIndex = trigger.interactionIndex;
    InteractionState& interactionState = m_memory.interactionStates[interactionIndex];
    if ((interactionState.flags & kInteractionTimerRunning) != 0)
        stop_timeout(interactionIndex);

    if (trigger.controlIndex == kInvalidIndex16 ||
        trigger.controlIndex >= static_cast<uint16_t>(m_controls.size()))
        return;
    Control* control = m_controls[trigger.controlIndex];
    if (control == nullptr)
        return;

    const int64_t monitorIndex =
        pack_monitor_index(trigger.mapIndex, trigger.controlIndex, trigger.bindingIndex);
    manager->add_state_change_monitor_timeout(control, this, trigger.time + seconds,
                                               monitorIndex, interactionIndex);

    interactionState.flags |= kInteractionTimerRunning;
    interactionState.timerStartTime = trigger.time;
    interactionState.timerDuration = seconds;
    interactionState.timerMonitorIndex = monitorIndex;
}

void ActionState::stop_timeout(int interactionIndex) {
    if (interactionIndex < 0 || interactionIndex >= m_memory.interactionCount)
        return;

    InteractionState& interactionState = m_memory.interactionStates[interactionIndex];
    if ((interactionState.flags & kInteractionTimerRunning) == 0)
        return;

    if (manager != nullptr) {
        manager->remove_state_change_monitor_timeout(
            this, interactionState.timerMonitorIndex, interactionIndex);
    }
    interactionState.flags &= static_cast<uint8_t>(~kInteractionTimerRunning);
    interactionState.totalTimeoutCompletionDone += interactionState.timerDuration;
    interactionState.totalTimeoutCompletionTimeRemaining =
        std::max(0.0f, interactionState.totalTimeoutCompletionTimeRemaining -
                           interactionState.timerDuration);
    interactionState.timerDuration = 0.0f;
    interactionState.timerStartTime = 0.0;
    interactionState.timerMonitorIndex = 0;
}

void ActionState::set_total_timeout_completion_time(float seconds, int interactionIndex) {
    if (interactionIndex < 0 || interactionIndex >= m_memory.interactionCount)
        return;
    InteractionState& interactionState = m_memory.interactionStates[interactionIndex];
    interactionState.totalTimeoutCompletionDone = 0.0f;
    interactionState.totalTimeoutCompletionTimeRemaining = std::max(0.0f, seconds);
}

void CallbackContext::read_value(void* buffer, int size) const {
    if (state != nullptr && action != nullptr && buffer != nullptr && size > 0)
        state->read_value(*action, buffer, static_cast<std::size_t>(size));
}

bool CallbackContext::read_value_as_button() const {
    if (state == nullptr || action == nullptr)
        return false;
    return state->read_value<float>(*action) >= kDefaultButtonPressPoint;
}

} // namespace insys

