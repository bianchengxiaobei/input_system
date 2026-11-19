#include "insys/enhanced_touch.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <utility>

#include "insys/manager.h"

namespace insys::enhanced_touch {
namespace {

constexpr float kContactThreshold = 0.0f;
constexpr double kTapTime = 0.25;
constexpr float kTapRadius = 10.0f;

struct CallbackEntry {
    int id = 0;
    std::function<void(const Finger&)> callback;
    bool active = true;
};

struct GlobalState {
    Manager* manager = nullptr;
    int enabledCount = 0;
    int historyLength = 64;
    int nextCallbackId = 1;
    int nextTouchId = 1;
    uint64_t nextRecordId = 1;
    uint32_t lastStep = 0;
    bool hasStep = false;
    std::vector<Touchscreen*> screens;
    std::vector<std::unique_ptr<Finger>> fingers;
    std::vector<Finger*> allFingers;
    std::vector<Finger*> activeFingers;
    std::vector<Touch> activeTouches;
    std::vector<CallbackEntry> onDown;
    std::vector<CallbackEntry> onMove;
    std::vector<CallbackEntry> onUp;
};

GlobalState g_state;

void invoke_callbacks(std::vector<CallbackEntry>& callbacks, const Finger& finger) {
    for (std::size_t i = 0; i < callbacks.size(); ++i) {
        if (!callbacks[i].active || !callbacks[i].callback)
            continue;
        const std::function<void(const Finger&)> callback = callbacks[i].callback;
        callback(finger);
    }
}

int add_callback(std::vector<CallbackEntry>& callbacks,
                 std::function<void(const Finger&)> callback) {
    if (!callback)
        return 0;
    const int id = g_state.nextCallbackId++;
    callbacks.push_back({id, std::move(callback), true});
    return id;
}

void deactivate_callback(std::vector<CallbackEntry>& callbacks, int callbackId) {
    for (CallbackEntry& entry : callbacks) {
        if (entry.id == callbackId) {
            entry.active = false;
            entry.callback = {};
            break;
        }
    }
}

TouchPhase point_phase(const TouchPointState& point) {
    return static_cast<TouchPhase>(static_cast<int>(point.phase));
}

bool point_is_active(const TouchPointState& point) {
    const TouchPhase phase = point_phase(point);
    if (phase != TouchPhase::Began && phase != TouchPhase::Moved &&
        phase != TouchPhase::Stationary)
        return false;
    return point.press > kContactThreshold;
}

bool point_is_finished(const TouchPointState& point) {
    const TouchPhase phase = point_phase(point);
    return phase == TouchPhase::Ended || phase == TouchPhase::Canceled;
}

bool point_is_begin(const TouchPointState& point) {
    return point_phase(point) == TouchPhase::Began;
}

bool point_changed(const TouchPointState& left, const TouchPointState& right) {
    return left.position != right.position || left.delta != right.delta ||
           left.startPosition != right.startPosition || left.phase != right.phase ||
           left.press != right.press || left.tapCount != right.tapCount;
}

TouchPointState read_touch_point(const Touchscreen& screen, int index) {
    TouchPointState point{};
    const uint8_t* state = screen.current_state();
    Control* group = screen.touch(index);
    if (state == nullptr || group == nullptr)
        return point;

    const uint32_t offset = group->stateBlock.effective_byte_offset();
    if (offset + sizeof(point) <= screen.state_size_bytes())
        std::memcpy(&point, state + offset, sizeof(point));
    return point;
}

void reset_state() {
    const int historyLength = g_state.historyLength;
    g_state = {};
    g_state.historyLength = historyLength;
}

} // namespace

bool Touch::in_progress() const {
    return m_valid && (m_phase == TouchPhase::Began || m_phase == TouchPhase::Moved ||
                       m_phase == TouchPhase::Stationary);
}

bool Touch::ended() const {
    return m_valid &&
           (m_phase == TouchPhase::Ended || m_phase == TouchPhase::Canceled);
}

Touchscreen* Touch::screen() const {
    return m_finger != nullptr ? m_finger->screen() : nullptr;
}

TouchHistory Touch::history() const {
    if (!m_valid || m_finger == nullptr)
        return {};

    const auto& records = m_finger->m_history;
    const auto current = std::find_if(records.rbegin(), records.rend(),
                                      [this](const Touch& touch) {
                                          return touch.record_id() == m_recordId;
                                      });
    if (current == records.rend())
        return {};

    std::vector<Touch> history;
    for (auto it = std::next(current); it != records.rend(); ++it) {
        if (it->touch_id() != m_touchId)
            break;
        history.push_back(*it);
    }
    return TouchHistory(std::move(history));
}

const std::vector<Touch>& Touch::active_touches() {
    return EnhancedTouchSupport::active_touches();
}

const std::vector<Finger*>& Touch::fingers() {
    return EnhancedTouchSupport::fingers();
}

const std::vector<Finger*>& Touch::active_fingers() {
    return EnhancedTouchSupport::active_fingers();
}

const std::vector<Touchscreen*>& Touch::screens() {
    return EnhancedTouchSupport::screens();
}

int Touch::max_history_length_per_finger() {
    return EnhancedTouchSupport::max_history_length_per_finger();
}

int Touch::add_on_finger_down(std::function<void(const Finger&)> callback) {
    return EnhancedTouchSupport::add_on_finger_down(std::move(callback));
}

int Touch::add_on_finger_move(std::function<void(const Finger&)> callback) {
    return EnhancedTouchSupport::add_on_finger_move(std::move(callback));
}

int Touch::add_on_finger_up(std::function<void(const Finger&)> callback) {
    return EnhancedTouchSupport::add_on_finger_up(std::move(callback));
}

void Touch::remove_callback(int callbackId) {
    EnhancedTouchSupport::remove_callback(callbackId);
}

Vector2 Finger::screen_position() const {
    const Touch touch = last_touch();
    return touch.valid() ? touch.screen_position() : Vector2{};
}

Touch Finger::last_touch() const {
    return m_history.empty() ? Touch{} : m_history.back();
}

Touch Finger::current_touch() const {
    Touch touch = last_touch();
    if (!touch.valid())
        return {};
    if (touch.in_progress() && touch.update_step_count() != g_state.lastStep &&
        (touch.phase() == TouchPhase::Began || touch.phase() == TouchPhase::Moved)) {
        touch.m_phase = TouchPhase::Stationary;
        touch.m_delta = {};
    }
    if (touch.in_progress())
        return touch;
    if (touch.update_step_count() == g_state.lastStep)
        return touch;
    return {};
}

TouchHistory Finger::touch_history() const {
    std::vector<Touch> history(m_history.rbegin(), m_history.rend());
    return TouchHistory(std::move(history));
}

void EnhancedTouchSupport::enable(Manager& manager) {
    if (g_state.enabledCount > 0) {
        if (g_state.manager == &manager) {
            ++g_state.enabledCount;
            return;
        }
        reset_state();
    }

    g_state.manager = &manager;
    g_state.enabledCount = 1;
    g_state.hasStep = false;
    g_state.screens.clear();
    g_state.fingers.clear();
    g_state.activeFingers.clear();
    g_state.activeTouches.clear();
}

void EnhancedTouchSupport::disable() {
    if (g_state.enabledCount == 0)
        return;
    --g_state.enabledCount;
    if (g_state.enabledCount == 0)
        reset_state();
}

void EnhancedTouchSupport::disable_for(Manager* manager) {
    if (manager != nullptr && g_state.manager == manager)
        reset_state();
}

bool EnhancedTouchSupport::enabled() {
    return g_state.enabledCount > 0 && g_state.manager != nullptr;
}

void EnhancedTouchSupport::update() {
    if (!enabled())
        return;

    std::vector<Touchscreen*> screens;
    for (Device* device : g_state.manager->devices()) {
        if (auto* touchscreen = dynamic_cast<Touchscreen*>(device))
            screens.push_back(touchscreen);
    }

    const bool screensChanged =
        screens.size() != g_state.screens.size() ||
        !std::equal(screens.begin(), screens.end(), g_state.screens.begin());
    if (screensChanged) {
        g_state.screens = screens;
        g_state.fingers.clear();
        g_state.activeFingers.clear();
        g_state.activeTouches.clear();
        g_state.allFingers.clear();
        for (Touchscreen* screen : g_state.screens) {
            for (int i = 0; i < screen->touch_count(); ++i) {
                g_state.fingers.push_back(std::unique_ptr<Finger>(new Finger(screen, i)));
                g_state.allFingers.push_back(g_state.fingers.back().get());
            }
        }
    }

    const uint32_t step = g_state.manager->update_step_count();
    if (g_state.hasStep && g_state.lastStep == step)
        return;

    g_state.lastStep = step;
    g_state.hasStep = true;

    const double now = g_state.manager->runtime().now();

    auto append_record = [&](Finger& finger, TouchPhase phaseSlot,
                             const TouchPointState& point, bool isTap,
                             int tapCount) {
        Touch touch;
        touch.m_finger = &finger;
        touch.m_valid = true;
        touch.m_phase = phaseSlot;
        touch.m_touchId = finger.m_currentTouchId;
        touch.m_pressure = point.press;
        touch.m_startTime = finger.m_startTime;
        touch.m_time = now;
        touch.m_position = point.position;
        touch.m_startPosition = finger.m_startPosition;
        touch.m_delta = phaseSlot == TouchPhase::Began ? Vector2{} : point.delta;
        touch.m_tapCount = tapCount;
        touch.m_isTap = isTap;
        touch.m_updateStepCount = step;
        touch.m_recordId = g_state.nextRecordId++;

        finger.m_history.push_back(touch);
        const int historyLength = std::max(1, g_state.historyLength);
        while (static_cast<int>(finger.m_history.size()) > historyLength)
            finger.m_history.pop_front();

        switch (phaseSlot) {
        case TouchPhase::Began:
            invoke_callbacks(g_state.onDown, finger);
            break;
        case TouchPhase::Moved:
            invoke_callbacks(g_state.onMove, finger);
            break;
        case TouchPhase::Ended:
        case TouchPhase::Canceled:
            invoke_callbacks(g_state.onUp, finger);
            break;
        case TouchPhase::Stationary:
            break;
        }
        return touch;
    };

    auto begin_contact = [&](Finger& finger, const TouchPointState& point) {
        finger.m_contactActive = true;
        finger.m_currentTouchId = g_state.nextTouchId++;
        finger.m_startTime = now;
        finger.m_startPosition =
            point.startPosition.is_zero() ? point.position : point.startPosition;
        finger.m_tapCount =
            point.tapCount > 0.0f ? static_cast<int>(point.tapCount) : 1;
        append_record(finger, TouchPhase::Began, point, false, finger.m_tapCount);
    };

    auto end_contact = [&](Finger& finger, TouchPointState point) {
        const TouchPhase phase = point_phase(point);
        TouchPhase finalPhase = TouchPhase::Ended;
        if (phase == TouchPhase::Canceled)
            finalPhase = TouchPhase::Canceled;
        else if (phase == TouchPhase::Ended)
            finalPhase = TouchPhase::Ended;
        point.press = 0.0f;

        bool isTap = false;
        int tapCount = 0;
        if (finalPhase == TouchPhase::Ended) {
            const double duration = now - finger.m_startTime;
            const float distance = (point.position - finger.m_startPosition).length();
            isTap = duration <= kTapTime && distance <= kTapRadius;
            if (isTap)
                tapCount = std::max(1, finger.m_tapCount);
        }

        append_record(finger, finalPhase, point, isTap, tapCount);
        finger.m_contactActive = false;
    };

    for (const std::unique_ptr<Finger>& fingerPtr : g_state.fingers) {
        Finger& finger = *fingerPtr;
        const TouchPointState point = read_touch_point(*finger.screen(), finger.index());
        const TouchPhase phase = point_phase(point);

        if (!finger.m_contactActive) {
            if (point_is_active(point))
                begin_contact(finger, point);
            continue;
        }

        if (point_is_begin(point)) {
            if (finger.m_history.empty() ||
                finger.m_history.back().in_progress()) {
                TouchPointState previous = finger.m_lastPoint;
                previous.phase = static_cast<float>(TouchPhase::Ended);
                end_contact(finger, previous);
            }
            begin_contact(finger, point);
        } else if (point_is_active(point)) {
            if (!finger.m_hasLastPoint || point_changed(finger.m_lastPoint, point)) {
                const int tapCount =
                    point.tapCount > 0.0f
                        ? static_cast<int>(point.tapCount)
                        : finger.m_tapCount;
                append_record(finger, phase, point, false, tapCount);
            }
        } else if (point_is_finished(point)) {
            end_contact(finger, point);
        } else {
            TouchPointState previous = finger.m_lastPoint;
            previous.phase = static_cast<float>(TouchPhase::Ended);
            end_contact(finger, previous);
        }

        finger.m_lastPoint = point;
        finger.m_hasLastPoint = true;
    }

    g_state.activeTouches.clear();
    g_state.activeFingers.clear();
    for (const std::unique_ptr<Finger>& fingerPtr : g_state.fingers) {
        const Touch current = fingerPtr->current_touch();
        if (current.valid()) {
            g_state.activeTouches.push_back(current);
            g_state.activeFingers.push_back(fingerPtr.get());
        }
    }
}

void EnhancedTouchSupport::reset() {
    reset_state();
}

const std::vector<Touch>& EnhancedTouchSupport::active_touches() {
    update();
    return g_state.activeTouches;
}

const std::vector<Finger*>& EnhancedTouchSupport::fingers() {
    update();
    return g_state.allFingers;
}

const std::vector<Finger*>& EnhancedTouchSupport::active_fingers() {
    update();
    return g_state.activeFingers;
}

const std::vector<Touchscreen*>& EnhancedTouchSupport::screens() {
    update();
    return g_state.screens;
}

int EnhancedTouchSupport::max_history_length_per_finger() {
    return g_state.historyLength;
}

void EnhancedTouchSupport::set_max_history_length_per_finger(int length) {
    g_state.historyLength = std::max(1, length);
}

int EnhancedTouchSupport::add_on_finger_down(
    std::function<void(const Finger&)> callback) {
    return add_callback(g_state.onDown, std::move(callback));
}

int EnhancedTouchSupport::add_on_finger_move(
    std::function<void(const Finger&)> callback) {
    return add_callback(g_state.onMove, std::move(callback));
}

int EnhancedTouchSupport::add_on_finger_up(
    std::function<void(const Finger&)> callback) {
    return add_callback(g_state.onUp, std::move(callback));
}

void EnhancedTouchSupport::remove_callback(int callbackId) {
    deactivate_callback(g_state.onDown, callbackId);
    deactivate_callback(g_state.onMove, callbackId);
    deactivate_callback(g_state.onUp, callbackId);
}

Manager* EnhancedTouchSupport::manager() {
    return g_state.manager;
}

uint32_t EnhancedTouchSupport::current_update_step() {
    return g_state.lastStep;
}

} // namespace insys::enhanced_touch














