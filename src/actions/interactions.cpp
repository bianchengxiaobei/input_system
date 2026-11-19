#include "insys/actions/interaction.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "insys/actions/action_state.h"
#include "insys/device.h"
#include "insys/manager.h"
#include "insys/string_utils.h"

namespace insys {
namespace {

constexpr float kEpsilon = 1e-5f;

bool approximately_zero(float value) {
    return std::fabs(value) <= kEpsilon;
}

float press_point_or_default(const InteractionContext& context, float pressPoint) {
    return pressPoint > 0.0f ? pressPoint : context.settings().defaultButtonPressPoint;
}

float release_point_or_default(const InteractionContext& context, float pressPoint) {
    return press_point_or_default(context, pressPoint) *
           context.settings().buttonReleaseThreshold;
}

TriggerState make_trigger(const InteractionContext& context) {
    TriggerState trigger;
    trigger.set_phase(context.phase());
    trigger.controlIndex = context.controlIndex >= 0
                               ? static_cast<uint16_t>(context.controlIndex)
                               : kInvalidIndex16;
    trigger.bindingIndex = context.bindingIndex >= 0
                               ? static_cast<uint16_t>(context.bindingIndex)
                               : kInvalidIndex16;
    trigger.interactionIndex = context.interactionIndex >= 0
                                   ? static_cast<uint16_t>(context.interactionIndex)
                                   : kInvalidIndex16;
    trigger.time = context.time();
    trigger.startTime = context.startTime();
    return trigger;
}

class TapInteraction final : public IInteraction {
public:
    float duration = -1.0f;
    float pressPoint = -1.0f;

    void process(InteractionContext& context) override {
        if (context.timerHasExpired()) {
            context.canceled();
            m_canceledFromTimer = true;
            return;
        }

        const float press = press_point_or_default(context, pressPoint);
        const float release = release_point_or_default(context, pressPoint);
        const float durationValue =
            duration > 0.0f ? duration : context.settings().defaultTapTime;

        if (context.phase() == ActionPhase::Waiting && context.control_is_actuated(press) &&
            !m_canceledFromTimer) {
            m_tapStartTime = context.time();
            context.started();
            context.set_timeout(durationValue + 0.00001f);
            return;
        }

        if (context.phase() == ActionPhase::Started && !context.control_is_actuated(release)) {
            if (context.time() - m_tapStartTime <= durationValue)
                context.performed();
            else
                context.canceled();
        }

        if (!context.control_is_actuated(release))
            m_canceledFromTimer = false;
    }

    void reset() override {
        m_tapStartTime = 0.0;
        m_canceledFromTimer = false;
    }

    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (equals_ignore_case(name, "duration")) {
            duration = value.as_float(duration);
            return true;
        }
        if (equals_ignore_case(name, "pressPoint")) {
            pressPoint = value.as_float(pressPoint);
            return true;
        }
        return false;
    }

private:
    double m_tapStartTime = 0.0;
    bool m_canceledFromTimer = false;
};

class HoldInteraction final : public IInteraction {
public:
    float duration = -1.0f;
    float pressPoint = -1.0f;

    void process(InteractionContext& context) override {
        if (context.timerHasExpired()) {
            context.performed_and_stay_performed();
            return;
        }

        const float press = press_point_or_default(context, pressPoint);
        const float durationValue =
            duration > 0.0f ? duration : context.settings().defaultHoldTime;

        switch (context.phase()) {
        case ActionPhase::Waiting:
            if (context.control_is_actuated(press)) {
                m_timePressed = context.time();
                context.started();
                context.set_timeout(durationValue);
            }
            break;
        case ActionPhase::Started:
            if (context.time() - m_timePressed >= durationValue)
                context.performed_and_stay_performed();
            if (!context.control_is_actuated())
                context.canceled();
            break;
        case ActionPhase::Performed:
            if (!context.control_is_actuated(press))
                context.canceled();
            break;
        default:
            break;
        }
    }

    void reset() override { m_timePressed = 0.0; }

    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (equals_ignore_case(name, "duration")) {
            duration = value.as_float(duration);
            return true;
        }
        if (equals_ignore_case(name, "pressPoint")) {
            pressPoint = value.as_float(pressPoint);
            return true;
        }
        return false;
    }

private:
    double m_timePressed = 0.0;
};

class SlowTapInteraction final : public IInteraction {
public:
    float duration = -1.0f;
    float pressPoint = -1.0f;

    void process(InteractionContext& context) override {
        const float press = press_point_or_default(context, pressPoint);
        const float durationValue =
            duration > 0.0f ? duration : context.settings().defaultSlowTapTime;

        if (context.phase() == ActionPhase::Waiting && context.control_is_actuated(press)) {
            m_slowTapStartTime = context.time();
            context.started();
            return;
        }

        if (context.phase() == ActionPhase::Started && !context.control_is_actuated(press)) {
            if (context.time() - m_slowTapStartTime >= durationValue)
                context.performed();
            else
                context.canceled();
        }
    }

    void reset() override { m_slowTapStartTime = 0.0; }

    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (equals_ignore_case(name, "duration")) {
            duration = value.as_float(duration);
            return true;
        }
        if (equals_ignore_case(name, "pressPoint")) {
            pressPoint = value.as_float(pressPoint);
            return true;
        }
        return false;
    }

private:
    double m_slowTapStartTime = 0.0;
};

class PressInteraction final : public IInteraction {
public:
    PressBehavior behavior = PressBehavior::PressOnly;
    float pressPoint = -1.0f;

    void process(InteractionContext& context) override {
        const float actuation = context.compute_magnitude();
        const float press = press_point_or_default(context, pressPoint);
        const float release = release_point_or_default(context, pressPoint);

        switch (behavior) {
        case PressBehavior::PressOnly:
            if (m_waitingForRelease) {
                if (actuation <= release) {
                    m_waitingForRelease = false;
                    if (approximately_zero(actuation))
                        context.canceled();
                    else
                        context.started();
                }
            } else if (actuation >= press) {
                m_waitingForRelease = true;
                context.performed_and_stay_performed();
            } else if (actuation > 0.0f && context.phase() != ActionPhase::Started) {
                context.started();
            } else if (approximately_zero(actuation) &&
                       context.phase() == ActionPhase::Started) {
                context.canceled();
            }
            break;

        case PressBehavior::ReleaseOnly:
            if (m_waitingForRelease) {
                if (actuation <= release) {
                    m_waitingForRelease = false;
                    context.performed();
                    context.canceled();
                }
            } else if (actuation >= press) {
                m_waitingForRelease = true;
                if (context.phase() != ActionPhase::Started)
                    context.started();
            } else if (actuation > 0.0f && context.phase() != ActionPhase::Started) {
                context.started();
            } else if (approximately_zero(actuation) &&
                       context.phase() == ActionPhase::Started) {
                context.canceled();
            }
            break;

        case PressBehavior::PressAndRelease:
            if (m_waitingForRelease) {
                if (actuation <= release) {
                    m_waitingForRelease = false;
                    context.performed();
                    if (approximately_zero(actuation))
                        context.canceled();
                }
            } else if (actuation >= press) {
                m_waitingForRelease = true;
                context.performed_and_stay_performed();
            } else if (actuation > 0.0f && context.phase() != ActionPhase::Started) {
                context.started();
            } else if (approximately_zero(actuation) &&
                       context.phase() == ActionPhase::Started) {
                context.canceled();
            }
            break;
        }
    }

    void reset() override { m_waitingForRelease = false; }

    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (equals_ignore_case(name, "behavior")) {
            behavior = static_cast<PressBehavior>(
                std::clamp(value.as_int(static_cast<int32_t>(behavior)), 0, 2));
            return true;
        }
        if (equals_ignore_case(name, "pressPoint")) {
            pressPoint = value.as_float(pressPoint);
            return true;
        }
        return false;
    }

private:
    bool m_waitingForRelease = false;
};

class MultiTapInteraction final : public IInteraction {
public:
    float tapTime = -1.0f;
    float tapDelay = -1.0f;
    int tapCount = 2;
    float pressPoint = -1.0f;

    void process(InteractionContext& context) override {
        if (context.timerHasExpired()) {
            context.canceled();
            reset();
            return;
        }

        const float press = press_point_or_default(context, pressPoint);
        const float release = release_point_or_default(context, pressPoint);
        const float tapTimeValue =
            tapTime > 0.0f ? tapTime : context.settings().defaultTapTime;
        const float tapDelayValue =
            tapDelay > 0.0f ? tapDelay : context.settings().multiTapDelayTime;
        const int requiredTaps = std::max(tapCount, 1);

        switch (m_phase) {
        case Phase::None:
            if (context.control_is_actuated(press)) {
                m_phase = Phase::WaitingForNextRelease;
                m_currentTapStartTime = context.time();
                context.started();
                context.set_total_timeout_completion_time(
                    tapTimeValue * requiredTaps + (requiredTaps - 1) * tapDelayValue);
                context.set_timeout(tapTimeValue);
            }
            break;

        case Phase::WaitingForNextRelease:
            if (!context.control_is_actuated(release)) {
                if (context.time() - m_currentTapStartTime <= tapTimeValue) {
                    ++m_currentTapCount;
                    if (m_currentTapCount >= requiredTaps) {
                        context.performed();
                        reset();
                    } else {
                        m_phase = Phase::WaitingForNextPress;
                        m_lastTapReleaseTime = context.time();
                        context.set_timeout(tapDelayValue);
                    }
                } else {
                    context.canceled();
                    reset();
                }
            }
            break;

        case Phase::WaitingForNextPress:
            if (context.control_is_actuated(press)) {
                if (context.time() - m_lastTapReleaseTime <= tapDelayValue) {
                    m_phase = Phase::WaitingForNextRelease;
                    m_currentTapStartTime = context.time();
                    context.set_timeout(tapTimeValue);
                } else {
                    context.canceled();
                    reset();
                }
            }
            break;
        }
    }

    void reset() override {
        m_phase = Phase::None;
        m_currentTapCount = 0;
        m_currentTapStartTime = 0.0;
        m_lastTapReleaseTime = 0.0;
    }

    bool set_parameter(std::string_view name, const PrimitiveValue& value) override {
        if (equals_ignore_case(name, "tapTime")) {
            tapTime = value.as_float(tapTime);
            return true;
        }
        if (equals_ignore_case(name, "tapDelay")) {
            tapDelay = value.as_float(tapDelay);
            return true;
        }
        if (equals_ignore_case(name, "tapCount")) {
            tapCount = value.as_int(tapCount);
            return true;
        }
        if (equals_ignore_case(name, "pressPoint")) {
            pressPoint = value.as_float(pressPoint);
            return true;
        }
        return false;
    }

private:
    enum class Phase {
        None,
        WaitingForNextRelease,
        WaitingForNextPress,
    };

    Phase m_phase = Phase::None;
    int m_currentTapCount = 0;
    double m_currentTapStartTime = 0.0;
    double m_lastTapReleaseTime = 0.0;
};

} // namespace

ActionDef* InteractionContext::action() const {
    return state.action_def(actionIndex);
}

Control* InteractionContext::control() const {
    if (controlIndex < 0 || static_cast<std::size_t>(controlIndex) >= state.controls().size())
        return nullptr;
    return state.controls()[static_cast<std::size_t>(controlIndex)];
}

const Settings& InteractionContext::settings() const {
    static const Settings fallback;
    if (state.manager == nullptr)
        return fallback;
    return state.manager->settings();
}

bool InteractionContext::control_is_actuated(float threshold) const {
    Control* currentControl = control();
    if (currentControl == nullptr || currentControl->device == nullptr ||
        currentControl->device->current_state() == nullptr)
        return false;
    const float actuation =
        currentControl->evaluate_magnitude(currentControl->device->current_state());
    if (threshold <= 0.0f)
        return actuation > 0.0f;
    return actuation >= threshold;
}

float InteractionContext::compute_magnitude() const {
    return magnitude;
}

void InteractionContext::started() {
    startedAt = currentTime;
    state.change_phase_of_interaction(ActionPhase::Started, *this);
}

void InteractionContext::performed() {
    state.change_phase_of_interaction(ActionPhase::Performed, *this, phaseAfterPerform,
                                      phaseAfterCancel);
}

void InteractionContext::performed_and_stay_started() {
    state.change_phase_of_interaction(ActionPhase::Performed, *this, ActionPhase::Started,
                                      phaseAfterCancel);
}

void InteractionContext::performed_and_stay_performed() {
    state.change_phase_of_interaction(ActionPhase::Performed, *this, ActionPhase::Performed,
                                      phaseAfterCancel);
}

void InteractionContext::canceled() {
    state.change_phase_of_interaction(ActionPhase::Canceled, *this, phaseAfterPerform,
                                      phaseAfterCancel);
}

void InteractionContext::waiting() {
    state.change_phase_of_interaction(ActionPhase::Waiting, *this);
}

void InteractionContext::set_timeout(float seconds) {
    TriggerState trigger = make_trigger(*this);
    state.start_timeout(seconds, trigger);
}

void InteractionContext::set_total_timeout_completion_time(float seconds) {
    state.set_total_timeout_completion_time(seconds, interactionIndex);
}

void InteractionContext::read_value(void* out, std::size_t size) const {
    state.read_binding_value(bindingIndex, out, size);
}

void InteractionRegistry::add(std::string name, Factory factory) {
    for (Entry& entry : m_entries) {
        if (equals_ignore_case(entry.name, name)) {
            entry.factory = std::move(factory);
            return;
        }
    }
    m_entries.push_back(Entry{std::move(name), std::move(factory)});
}

std::unique_ptr<IInteraction> InteractionRegistry::create(std::string_view name) const {
    for (const Entry& entry : m_entries) {
        if (equals_ignore_case(entry.name, name))
            return entry.factory ? entry.factory() : nullptr;
    }
    return nullptr;
}

bool InteractionRegistry::has(std::string_view name) const {
    for (const Entry& entry : m_entries) {
        if (equals_ignore_case(entry.name, name))
            return true;
    }
    return false;
}

std::vector<std::string> InteractionRegistry::names() const {
    std::vector<std::string> result;
    result.reserve(m_entries.size());
    for (const Entry& entry : m_entries)
        result.push_back(entry.name);
    return result;
}

InteractionRegistry& interaction_registry() {
    static InteractionRegistry registry = [] {
        InteractionRegistry result;
        result.add("Tap", [] { return std::make_unique<TapInteraction>(); });
        result.add("Hold", [] { return std::make_unique<HoldInteraction>(); });
        result.add("SlowTap", [] { return std::make_unique<SlowTapInteraction>(); });
        result.add("Press", [] { return std::make_unique<PressInteraction>(); });
        result.add("MultiTap", [] { return std::make_unique<MultiTapInteraction>(); });
        return result;
    }();
    return registry;
}

void apply_parameters(IInteraction& interaction, const std::vector<NamedValue>& parameters) {
    for (const NamedValue& parameter : parameters) {
        if (!interaction.set_parameter(parameter.name, parameter.value)) {
            std::fprintf(stderr, "[insys] unknown interaction parameter '%s'\n",
                         parameter.name.c_str());
        }
    }
}

std::unique_ptr<IInteraction> make_interaction(std::string_view name,
                                               const std::vector<NamedValue>& parameters) {
    std::unique_ptr<IInteraction> interaction = interaction_registry().create(name);
    if (interaction)
        apply_parameters(*interaction, parameters);
    return interaction;
}

} // namespace insys
