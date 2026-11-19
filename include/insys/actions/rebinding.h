#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "insys/actions/action.h"
#include "insys/actions/binding.h"
#include "insys/low/event.h"
#include "insys/manager.h"

namespace insys {

class ActionMap;
class ActionState;
class Control;

class RebindingOperation final : public IInputEventListener, public IBeforeUpdateListener {
public:
    RebindingOperation();
    ~RebindingOperation() override;

    RebindingOperation(const RebindingOperation&) = delete;
    RebindingOperation& operator=(const RebindingOperation&) = delete;

    RebindingOperation& with_action(ActionDef& action);
    RebindingOperation& with_target_binding(int bindingIndex);
    RebindingOperation& with_expected_control_type(std::string_view layout);
    RebindingOperation& with_controls_excluding(std::string_view path);
    RebindingOperation& with_controls_having_to_match_path(std::string_view path);
    RebindingOperation& with_magnitude_greater_than(float magnitude);
    RebindingOperation& with_timeout(float seconds);
    RebindingOperation& with_matching_events_suppressed(bool value = true);
    RebindingOperation& with_generalizing_path(bool value = true);
    RebindingOperation& without_generalizing_path() {
        m_generalize = false;
        return *this;
    }

    RebindingOperation& on_match(std::function<void(Control*)> callback);
    RebindingOperation& on_cancel(std::function<void()> callback);
    RebindingOperation& on_complete(std::function<void()> callback);

    RebindingOperation& start();
    void cancel();
    void dispose();

    bool running() const { return m_active; }
    bool matched() const { return m_matched; }
    Control* matched_control() const { return m_matchedControl; }
    const std::string& matched_path() const { return m_matchedPath; }

    void on_input_event(low::Event* event) override;
    void on_before_update() override;

private:
    void stop_listening();
    bool control_is_eligible(Control* control, const uint8_t* previousState) const;
    void accept(Control* control);
    std::string generate_path(Control* control) const;

    ActionDef* m_action = nullptr;
    int m_targetBinding = 0;
    ActionState* m_state = nullptr;
    ActionMap* m_map = nullptr;
    Binding* m_binding = nullptr;
    Manager* m_manager = nullptr;

    std::string m_expectedControlType;
    std::vector<std::string> m_exclusions;
    std::vector<std::string> m_requiredPaths;
    float m_magnitudeThreshold = 0.5f;
    float m_timeout = 0.0f;
    double m_deadline = 0.0;
    bool m_suppressEvents = true;
    bool m_generalize = false;

    bool m_active = false;
    bool m_matched = false;
    Control* m_matchedControl = nullptr;
    std::string m_matchedPath;

    std::function<void(Control*)> m_onMatch;
    std::function<void()> m_onCancel;
    std::function<void()> m_onComplete;
};

} // namespace insys
