#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "insys/actions/action.h"
#include "insys/control_layout.h"

namespace insys {

class ActionState;
class Control;
class IProcessor;
struct Settings;

enum class PressBehavior {
    PressOnly = 0,
    ReleaseOnly = 1,
    PressAndRelease = 2,
};

class InteractionContext {
public:
    ActionState& state;
    ActionPhase interactionPhase = ActionPhase::Waiting;
    ActionPhase phaseAfterPerform = ActionPhase::Waiting;
    ActionPhase phaseAfterCancel = ActionPhase::Waiting;
    int actionIndex = -1;
    int bindingIndex = -1;
    int controlIndex = -1;
    int interactionIndex = -1;
    double currentTime = 0.0;
    double startedAt = 0.0;
    bool timerExpired = false;
    float magnitude = 0.0f;

    ActionPhase phase() const { return interactionPhase; }
    double time() const { return currentTime; }
    double startTime() const { return startedAt; }
    bool timerHasExpired() const { return timerExpired; }

    ActionDef* action() const;
    Control* control() const;
    const Settings& settings() const;

    bool is_waiting() const { return interactionPhase == ActionPhase::Waiting; }
    bool is_started() const { return interactionPhase == ActionPhase::Started; }
    bool is_performed() const { return interactionPhase == ActionPhase::Performed; }

    bool control_is_actuated(float threshold = 0.0f) const;
    float compute_magnitude() const;

    void started();
    void performed();
    void performed_and_stay_started();
    void performed_and_stay_performed();
    void canceled();
    void waiting();

    void set_timeout(float seconds);
    void set_total_timeout_completion_time(float seconds);

    void read_value(void* out, std::size_t size) const;
    template <typename T>
    T read_value() const {
        T value{};
        read_value(&value, sizeof(T));
        return value;
    }
};

class IInteraction {
public:
    virtual ~IInteraction() = default;

    virtual void process(InteractionContext& context) = 0;
    virtual void reset() = 0;
    virtual bool set_parameter(std::string_view name, const PrimitiveValue& value) {
        (void)name;
        (void)value;
        return false;
    }
};

class InteractionRegistry {
public:
    using Factory = std::function<std::unique_ptr<IInteraction>()>;

    void add(std::string name, Factory factory);
    std::unique_ptr<IInteraction> create(std::string_view name) const;
    bool has(std::string_view name) const;
    std::vector<std::string> names() const;

private:
    struct Entry {
        std::string name;
        Factory factory;
    };
    std::vector<Entry> m_entries;
};

InteractionRegistry& interaction_registry();

void apply_parameters(IInteraction& interaction, const std::vector<NamedValue>& parameters);
std::unique_ptr<IInteraction> make_interaction(std::string_view name,
                                               const std::vector<NamedValue>& parameters);

} // namespace insys
