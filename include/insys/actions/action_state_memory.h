#pragma once

#include "insys/actions/action_state_types.h"
#include "insys/actions/unmanaged_memory.h"

namespace insys {

struct ActionStateMemory {
    UnmanagedMemory arena;

    ActionMapIndices* mapIndices = nullptr;
    TriggerState* actionStates = nullptr;
    InteractionState* interactionStates = nullptr;
    BindingState* bindingStates = nullptr;
    float* controlMagnitudes = nullptr;
    float* compositeMagnitudes = nullptr;
    int32_t* controlIndexToBindingIndex = nullptr;
    uint16_t* controlGroupingAndPriority = nullptr;
    uint16_t* actionBindingIndicesAndCounts = nullptr;
    uint16_t* actionBindingIndices = nullptr;
    uint32_t* enabledControlsBits = nullptr;

    int mapCount = 0;
    int actionCount = 0;
    int interactionCount = 0;
    int bindingCount = 0;
    int controlCount = 0;
    int compositeCount = 0;

    void allocate(int mapCount, int actionCount, int interactionCount, int bindingCount,
                  int controlCount, int compositeCount);

    bool control_enabled(int controlIndex) const;
    void set_control_enabled(int controlIndex, bool enabled);
};

} // namespace insys
