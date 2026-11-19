#include "insys/actions/rebinding.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "insys/actions/action_map.h"
#include "insys/actions/action_state.h"
#include "insys/control_path.h"
#include "insys/device.h"
#include "insys/string_utils.h"

namespace insys {
namespace {

bool is_pointer_noise_control(const Control* control) {
    if (control == nullptr || control->device == nullptr)
        return true;
    if (!equals_ignore_case(control->device->layout.str(), "Mouse"))
        return false;
    return equals_ignore_case(control->name.str(), "position") ||
           equals_ignore_case(control->name.str(), "delta") ||
           equals_ignore_case(control->name.str(), "scroll");
}

float magnitude_of(const Control* control, const uint8_t* state) {
    if (control == nullptr || state == nullptr)
        return 0.0f;
    return control->evaluate_magnitude(state);
}

} // namespace

RebindingOperation::RebindingOperation() = default;

RebindingOperation::~RebindingOperation() {
    dispose();
}

RebindingOperation& RebindingOperation::with_action(ActionDef& action) {
    m_action = &action;
    return *this;
}

RebindingOperation& RebindingOperation::with_target_binding(int bindingIndex) {
    m_targetBinding = bindingIndex;
    return *this;
}

RebindingOperation& RebindingOperation::with_expected_control_type(std::string_view layout) {
    m_expectedControlType = std::string(layout);
    return *this;
}

RebindingOperation& RebindingOperation::with_controls_excluding(std::string_view path) {
    m_exclusions.emplace_back(path);
    return *this;
}

RebindingOperation& RebindingOperation::with_controls_having_to_match_path(
    std::string_view path) {
    m_requiredPaths.emplace_back(path);
    return *this;
}

RebindingOperation& RebindingOperation::with_magnitude_greater_than(float magnitude) {
    m_magnitudeThreshold = magnitude;
    return *this;
}

RebindingOperation& RebindingOperation::with_timeout(float seconds) {
    m_timeout = seconds;
    return *this;
}

RebindingOperation& RebindingOperation::with_matching_events_suppressed(bool value) {
    m_suppressEvents = value;
    return *this;
}

RebindingOperation& RebindingOperation::with_generalizing_path(bool value) {
    m_generalize = value;
    return *this;
}

RebindingOperation& RebindingOperation::on_match(std::function<void(Control*)> callback) {
    m_onMatch = std::move(callback);
    return *this;
}

RebindingOperation& RebindingOperation::on_cancel(std::function<void()> callback) {
    m_onCancel = std::move(callback);
    return *this;
}

RebindingOperation& RebindingOperation::on_complete(std::function<void()> callback) {
    m_onComplete = std::move(callback);
    return *this;
}

RebindingOperation& RebindingOperation::start() {
    dispose();

    m_matched = false;
    m_matchedControl = nullptr;
    m_matchedPath.clear();
    m_state = m_action != nullptr ? m_action->state : nullptr;
    if (m_action == nullptr || m_state == nullptr || m_state->manager == nullptr)
        return *this;

    m_manager = m_state->manager;
    const auto& maps = m_state->maps();
    if (m_action->mapIndex < 0 || m_action->mapIndex >= static_cast<int>(maps.size()))
        return *this;
    m_map = maps[static_cast<std::size_t>(m_action->mapIndex)];
    if (m_map == nullptr || m_targetBinding < 0 ||
        m_targetBinding >= static_cast<int>(m_map->bindings.size()))
        return *this;
    m_binding = &m_map->bindings[static_cast<std::size_t>(m_targetBinding)];

    m_deadline = m_timeout > 0.0f ? m_manager->runtime().now() + m_timeout : 0.0;
    m_manager->add_input_event_listener(this);
    if (m_timeout > 0.0f)
        m_manager->add_before_update_listener(this);
    m_active = true;
    return *this;
}

void RebindingOperation::cancel() {
    if (m_active) {
        m_active = false;
        stop_listening();
        if (m_onCancel)
            m_onCancel();
    } else {
        stop_listening();
    }
}

void RebindingOperation::dispose() {
    m_active = false;
    stop_listening();
}

void RebindingOperation::stop_listening() {
    if (m_manager != nullptr) {
        m_manager->remove_input_event_listener(this);
        m_manager->remove_before_update_listener(this);
    }
    m_manager = nullptr;
}

bool RebindingOperation::control_is_eligible(Control* control,
                                             const uint8_t* previousState) const {
    if (control == nullptr || control->device == nullptr ||
        control->device->current_state() == nullptr)
        return false;
    if (control->parent == nullptr)
        return false;
    if ((control->flags & kControlIsNoisy) != 0 ||
        (control->flags & kControlIsSynthetic) != 0)
        return false;
    if (is_pointer_noise_control(control))
        return false;

    if (!m_expectedControlType.empty()) {
        bool matches = equals_ignore_case(control->layout.str(), m_expectedControlType);
        if (!matches && equals_ignore_case(m_expectedControlType, "Button"))
            matches = control->is_button();
        if (!matches && equals_ignore_case(m_expectedControlType, "Axis"))
            matches = control->is_axis();
        if (!matches)
            return false;
    }

    for (const std::string& exclusion : m_exclusions) {
        if (path_matches(exclusion, *control))
            return false;
    }
    for (const std::string& required : m_requiredPaths) {
        if (!path_matches(required, *control))
            return false;
    }

    const uint8_t* currentState = control->device->current_state();
    const float current = magnitude_of(control, currentState);
    const float previous = magnitude_of(control, previousState);
    if (current < m_magnitudeThreshold || current <= previous + 1e-5f)
        return false;
    return true;
}

std::string RebindingOperation::generate_path(Control* control) const {
    if (control == nullptr)
        return {};
    if (!m_generalize || control->device == nullptr)
        return control->path();

    std::vector<std::string> names;
    for (const Control* current = control; current != nullptr && current != control->device;
         current = current->parent) {
        names.push_back(current->name.str());
    }
    std::reverse(names.begin(), names.end());
    std::string result = "<" + control->device->layout.str() + ">";
    for (const std::string& name : names)
        result += "/" + name;
    return result;
}

void RebindingOperation::accept(Control* control) {
    m_matched = true;
    m_matchedControl = control;
    m_matchedPath = generate_path(control);
    m_active = false;
    stop_listening();

    if (m_binding != nullptr)
        m_binding->overridePath = m_matchedPath;

    if (m_onMatch)
        m_onMatch(control);

    if (m_state != nullptr)
        m_state->re_resolve();

    if (m_onComplete)
        m_onComplete();
}

void RebindingOperation::on_input_event(low::Event* event) {
    if (!m_active || event == nullptr || m_manager == nullptr)
        return;
    if (event->type != low::kStateEvent.value && event->type != low::kDeltaStateEvent.value)
        return;

    Device* device = m_manager->device_by_id(event->deviceId);
    if (device == nullptr || device->current_state() == nullptr)
        return;

    const uint8_t* previousState = device->previousStatePtr;
    Control* best = nullptr;
    float bestScore = -1.0f;
    for (Control* control : device->allControls) {
        if (!control_is_eligible(control, previousState))
            continue;
        const float magnitude = magnitude_of(control, device->current_state());
        const float score = magnitude + (control->is_button() ? 1.0f : 0.0f);
        if (score > bestScore) {
            bestScore = score;
            best = control;
        }
    }

    if (best == nullptr)
        return;
    if (m_suppressEvents)
        low::set_event_handled(event, true);
    accept(best);
}

void RebindingOperation::on_before_update() {
    if (!m_active || m_timeout <= 0.0f || m_manager == nullptr)
        return;
    if (m_manager->runtime().now() >= m_deadline)
        cancel();
}

} // namespace insys
