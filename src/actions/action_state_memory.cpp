#include "insys/actions/action_state_memory.h"

#include <cstring>

namespace insys {
namespace {

std::size_t offset_for(std::size_t& cursor, std::size_t count, std::size_t elementSize,
                       std::size_t alignment) {
    cursor = align_up_size(cursor, alignment);
    const std::size_t result = cursor;
    cursor += count * elementSize;
    return result;
}

} // namespace

void ActionStateMemory::allocate(int mapCountValue, int actionCountValue,
                                 int interactionCountValue, int bindingCountValue,
                                 int controlCountValue, int compositeCountValue) {
    mapCount = mapCountValue;
    actionCount = actionCountValue;
    interactionCount = interactionCountValue;
    bindingCount = bindingCountValue;
    controlCount = controlCountValue;
    compositeCount = compositeCountValue;

    std::size_t cursor = 0;
    const std::size_t mapIndicesOffset =
        offset_for(cursor, static_cast<std::size_t>(mapCount), sizeof(ActionMapIndices),
                   alignof(ActionMapIndices));
    const std::size_t actionStatesOffset =
        offset_for(cursor, static_cast<std::size_t>(actionCount), sizeof(TriggerState),
                   alignof(TriggerState));
    const std::size_t interactionStatesOffset =
        offset_for(cursor, static_cast<std::size_t>(interactionCount), sizeof(InteractionState),
                   alignof(InteractionState));
    const std::size_t bindingStatesOffset =
        offset_for(cursor, static_cast<std::size_t>(bindingCount), sizeof(BindingState),
                   alignof(BindingState));
    const std::size_t controlMagnitudesOffset =
        offset_for(cursor, static_cast<std::size_t>(controlCount), sizeof(float), alignof(float));
    const std::size_t compositeMagnitudesOffset =
        offset_for(cursor, static_cast<std::size_t>(compositeCount), sizeof(float), alignof(float));
    const std::size_t controlIndexToBindingOffset =
        offset_for(cursor, static_cast<std::size_t>(controlCount), sizeof(int32_t),
                   alignof(int32_t));
    const std::size_t controlGroupingOffset =
        offset_for(cursor, static_cast<std::size_t>(controlCount) * 2, sizeof(uint16_t),
                   alignof(uint16_t));
    const std::size_t actionBindingIndicesAndCountsOffset =
        offset_for(cursor, static_cast<std::size_t>(actionCount) * 2, sizeof(uint16_t),
                   alignof(uint16_t));
    const std::size_t actionBindingIndicesOffset =
        offset_for(cursor, static_cast<std::size_t>(bindingCount), sizeof(uint16_t),
                   alignof(uint16_t));
    const std::size_t enabledControlsOffset =
        offset_for(cursor, (static_cast<std::size_t>(controlCount) + 31) / 32, sizeof(uint32_t),
                   alignof(uint32_t));

    arena.allocate(cursor);
    std::memset(arena.data(), 0, arena.size());

    mapIndices = arena.at<ActionMapIndices>(mapIndicesOffset);
    actionStates = arena.at<TriggerState>(actionStatesOffset);
    interactionStates = arena.at<InteractionState>(interactionStatesOffset);
    bindingStates = arena.at<BindingState>(bindingStatesOffset);
    controlMagnitudes = arena.at<float>(controlMagnitudesOffset);
    compositeMagnitudes = arena.at<float>(compositeMagnitudesOffset);
    controlIndexToBindingIndex = arena.at<int32_t>(controlIndexToBindingOffset);
    controlGroupingAndPriority = arena.at<uint16_t>(controlGroupingOffset);
    actionBindingIndicesAndCounts = arena.at<uint16_t>(actionBindingIndicesAndCountsOffset);
    actionBindingIndices = arena.at<uint16_t>(actionBindingIndicesOffset);
    enabledControlsBits = arena.at<uint32_t>(enabledControlsOffset);

    for (int i = 0; i < actionCount; ++i) {
        actionStates[i].controlIndex = kInvalidIndex16;
        actionStates[i].bindingIndex = kInvalidIndex16;
        actionStates[i].interactionIndex = kInvalidIndex16;
    }
    for (int i = 0; i < interactionCount; ++i)
        interactionStates[i] = InteractionState{};
    for (int i = 0; i < controlCount; ++i)
        controlIndexToBindingIndex[i] = -1;
}

bool ActionStateMemory::control_enabled(int controlIndex) const {
    if (controlIndex < 0 || controlIndex >= controlCount || enabledControlsBits == nullptr)
        return false;
    const std::size_t word = static_cast<std::size_t>(controlIndex) / 32;
    const uint32_t bit = 1u << (static_cast<uint32_t>(controlIndex) & 31u);
    return (enabledControlsBits[word] & bit) != 0;
}

void ActionStateMemory::set_control_enabled(int controlIndex, bool enabled) {
    if (controlIndex < 0 || controlIndex >= controlCount || enabledControlsBits == nullptr)
        return;
    const std::size_t word = static_cast<std::size_t>(controlIndex) / 32;
    const uint32_t bit = 1u << (static_cast<uint32_t>(controlIndex) & 31u);
    if (enabled)
        enabledControlsBits[word] |= bit;
    else
        enabledControlsBits[word] &= ~bit;
}

} // namespace insys
