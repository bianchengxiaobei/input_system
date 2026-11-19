#pragma once

#include <cstdint>

#include "insys/actions/action.h"

namespace insys {

constexpr int kInvalidIndex = -1;
constexpr uint16_t kInvalidIndex16 = 0xFFFF;

struct ActionMapIndices {
    int actionStart = 0;
    int actionCount = 0;
    int bindingStart = 0;
    int bindingCount = 0;
    int controlStart = 0;
    int controlCount = 0;
};

enum TriggerFlags : uint8_t {
    kTriggerHaveMagnitude = 1u << 0,
    kTriggerPassThrough = 1u << 1,
    kTriggerButton = 1u << 2,
    kTriggerPressed = 1u << 3,
    kTriggerMultipleConcurrentActuations = 1u << 4,
    kTriggerInProcessing = 1u << 5,
};

struct TriggerState {
    uint8_t phase = static_cast<uint8_t>(ActionPhase::Disabled);
    uint8_t flags = 0;
    uint8_t mapIndex = 0;
    uint8_t padding0 = 0;
    uint16_t controlIndex = kInvalidIndex16;
    uint16_t bindingIndex = kInvalidIndex16;
    uint16_t interactionIndex = kInvalidIndex16;
    float magnitude = 0.0f;
    double time = 0.0;
    double startTime = 0.0;
    uint32_t lastPerformedInUpdate = 0;
    uint32_t lastCanceledInUpdate = 0;
    uint32_t pressedInUpdate = 0;
    uint32_t releasedInUpdate = 0;
    uint32_t lastCompletedInUpdate = 0;
    int32_t framePerformed = 0;
    int32_t framePressed = 0;
    int32_t frameReleased = 0;
    int32_t frameCompleted = 0;

    ActionPhase phase_value() const { return static_cast<ActionPhase>(phase); }
    void set_phase(ActionPhase value) { phase = static_cast<uint8_t>(value); }
};

enum BindingFlags : uint8_t {
    kBindingComposite = 1u << 0,
    kBindingPartOfComposite = 1u << 1,
    kBindingInitialStateCheckPending = 1u << 2,
    kBindingWantsInitialStateCheck = 1u << 3,
    kBindingDisabled = 1u << 4,
};

struct BindingState {
    uint8_t controlCount = 0;
    uint8_t interactionCount = 0;
    uint8_t processorCount = 0;
    uint8_t mapIndex = 0;
    uint8_t flags = 0;
    uint8_t partIndex = 0;
    uint16_t actionIndex = kInvalidIndex16;
    uint16_t compositeOrCompositeBindingIndex = kInvalidIndex16;
    uint16_t processorStartIndex = kInvalidIndex16;
    uint16_t interactionStartIndex = kInvalidIndex16;
    uint16_t controlStartIndex = 0;
    double pressTime = 0.0;
    uint32_t triggerEventIdForComposite = 0;

    bool is_composite() const { return (flags & kBindingComposite) != 0; }
    bool is_part_of_composite() const { return (flags & kBindingPartOfComposite) != 0; }
    bool wants_initial_state_check() const { return (flags & kBindingWantsInitialStateCheck) != 0; }
    bool initial_state_check_pending() const { return (flags & kBindingInitialStateCheckPending) != 0; }
    bool is_disabled() const { return (flags & kBindingDisabled) != 0; }

    void set_initial_state_check_pending(bool value) {
        if (value)
            flags |= kBindingInitialStateCheckPending;
        else
            flags &= static_cast<uint8_t>(~kBindingInitialStateCheckPending);
    }
};

enum InteractionFlags : uint8_t {
    kInteractionTimerRunning = 1u << 0,
};

struct InteractionState {
    uint16_t triggerControlIndex = kInvalidIndex16;
    uint8_t phase = static_cast<uint8_t>(ActionPhase::Waiting);
    uint8_t flags = 0;
    float timerDuration = 0.0f;
    double startTime = 0.0;
    double timerStartTime = 0.0;
    double performedTime = 0.0;
    float totalTimeoutCompletionDone = 0.0f;
    float totalTimeoutCompletionTimeRemaining = 0.0f;
    int64_t timerMonitorIndex = 0;
};

} // namespace insys
