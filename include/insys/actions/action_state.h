#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "insys/actions/action.h"
#include "insys/actions/action_state_memory.h"
#include "insys/actions/action_state_types.h"
#include "insys/actions/binding.h"
#include "insys/actions/composite.h"
#include "insys/actions/interaction.h"
#include "insys/actions/resolved_bindings.h"
#include "insys/manager.h"
#include "insys/processor.h"
#include "insys/state_change_monitors.h"

namespace insys {

class ActionMap;
class BindingResolver;

class ActionState final : public IStateChangeMonitor, public IBeforeUpdateListener {
public:
    ActionState();
    ~ActionState() override;

    ActionState(const ActionState&) = delete;
    ActionState& operator=(const ActionState&) = delete;

    static std::unique_ptr<ActionState> create(
        Manager& manager, const std::vector<ActionMap*>& maps,
        const std::vector<Device*>* devices = nullptr,
        const Binding* bindingMask = nullptr);
    bool initialize(Manager& manager, ResolvedBindings&& resolved);
    void dispose();

    void enable_all(ActionMap& map);
    void disable_all(ActionMap& map);
    void enable_single(ActionDef& action);
    void disable_single(ActionDef& action);

    ActionPhase phase(const ActionDef& action) const;
    bool was_performed_this_frame(const ActionDef& action) const;
    bool was_pressed_this_frame(const ActionDef& action) const;
    bool was_released_this_frame(const ActionDef& action) const;

    ValueType value_type(const ActionDef& action) const;
    void read_value(const ActionDef& action, void* out, std::size_t size) const;

    template <typename T>
    T read_value(const ActionDef& action) const {
        T value{};
        read_value(action, &value, sizeof(T));
        return value;
    }

    Control* active_control(const ActionDef& action) const;
    int active_binding(const ActionDef& action) const;

    ActionDef* action_def(int actionIndex) const;
    BindingState* binding_state(int bindingIndex);
    const BindingState* binding_state(int bindingIndex) const;
    TriggerState* trigger_state(int actionIndex);
    const TriggerState* trigger_state(int actionIndex) const;
    const std::vector<Control*>& controls() const { return m_controls; }
    const std::vector<ActionMap*>& maps() const { return m_maps; }
    IComposite* composite(int compositeIndex) const;

    void read_composite_part(int compositeBindingIndex, int partIndex, void* out,
                             std::size_t size) const;
    bool read_composite_part_as_button(int compositeBindingIndex, int partIndex) const;
    float read_composite_part_magnitude(int compositeBindingIndex, int partIndex) const;
    double read_composite_part_press_time(int compositeBindingIndex, int partIndex) const;
    float composite_magnitude(int compositeIndex) const;
    void read_binding_value(int bindingIndex, void* out, std::size_t size) const;

    void change_phase_of_action(ActionPhase newPhase, TriggerState& trigger,
                                ActionPhase phaseAfterPerformedOrCanceled = ActionPhase::Waiting);
    void change_phase_of_interaction(
        ActionPhase newPhase, InteractionContext& context,
        ActionPhase phaseAfterPerformed = ActionPhase::Waiting,
        ActionPhase phaseAfterCanceled = ActionPhase::Waiting);

    void notify_state_changed(Control* control, double time, low::Event* event,
                              int64_t monitorIndex) override;
    void notify_timer_expired(Control* control, double time, int64_t monitorIndex,
                              int timerIndex) override;
    void on_before_update() override;

    void start_timeout(float seconds, TriggerState& trigger);
    void stop_timeout(int interactionIndex);
    void set_total_timeout_completion_time(float seconds, int interactionIndex);

    bool re_resolve();
    bool re_resolve(const std::vector<Device*>* devices, const Binding* bindingMask);
    int binding_global_index(int mapIndex, int localBindingIndex) const;

    Manager* manager = nullptr;

private:
    ProcessorContext processor_context() const;
    void set_composite_part_press_times(int compositeBindingIndex, double time);
    void process_control_state_change(int mapIndex, int controlIndex, int bindingIndex, double time,
                                      low::Event* event);
    void process_default_interaction(TriggerState& trigger, int actionIndex);
    void process_button_state(TriggerState& trigger, int actionIndex);
    void process_interactions(TriggerState& trigger, int interactionStartIndex,
                              int interactionCount);
    bool is_conflicting_input(TriggerState& trigger, int actionIndex);
    float press_threshold(const Control* control, const BindingState& binding) const;
    bool is_actuated(const TriggerState& trigger) const;
    int trigger_control_index(const TriggerState& trigger) const;
    int action_control_index(const TriggerState& trigger, const TriggerState& actionState) const;
    void change_phase_of_action_internal(int actionIndex, TriggerState& trigger,
                                         ActionPhase newPhase,
                                         ActionPhase phaseAfterPerformedOrCanceled);
    void reset_action_state(int actionIndex);
    void reset_interaction_state(int interactionIndex);
    void reset_action_interactions(int actionIndex);
    bool control_alive(int controlIndex) const;
    Control* live_control(int controlIndex) const;
    Device* live_device(int controlIndex) const;
    void hook_initial_state_check();
    void unhook_initial_state_check();
    void update_enabled_count(int delta);
    bool resolve_and_initialize(Manager& manager, const std::vector<ActionMap*>& maps,
                                const std::vector<Device*>* devices,
                                const Binding* bindingMask);

    std::vector<ActionMap*> m_maps;
    std::vector<ActionDef*> m_actions;
    std::vector<Control*> m_controls;
    std::vector<int> m_controlDeviceIds;
    std::vector<Device*> m_controlDevices;
    std::vector<IInteraction*> m_interactions;
    std::vector<IProcessor*> m_processors;
    std::vector<IComposite*> m_composites;

    std::vector<std::unique_ptr<IInteraction>> m_interactionStorage;
    std::vector<std::unique_ptr<IProcessor>> m_processorStorage;
    std::vector<std::unique_ptr<IComposite>> m_compositeStorage;

    ActionStateMemory m_memory;
    std::vector<int> m_controlEnableCounts;
    std::vector<int> m_deviceFilterIds;
    bool m_hasDeviceFilter = false;
    std::optional<Binding> m_bindingMask;

    int m_enabledActionCount = 0;
    bool m_initialStateCheckHooked = false;
    bool m_inProcess = false;

    friend class BindingResolver;
};

struct CallbackContext {
    ActionState* state = nullptr;
    ActionDef* action = nullptr;
    ActionPhase phase = ActionPhase::Waiting;
    Control* control = nullptr;
    double time = 0.0;
    double startTime = 0.0;

    double duration() const { return time - startTime; }

    void read_value(void* buffer, int size) const;
    bool read_value_as_button() const;

    template <typename T>
    T read_value() const {
        T value{};
        if (state != nullptr && action != nullptr)
            state->read_value(*action, &value, sizeof(T));
        return value;
    }
};

} // namespace insys

