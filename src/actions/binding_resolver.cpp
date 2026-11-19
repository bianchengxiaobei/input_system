#include "insys/actions/binding_resolver.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "insys/actions/action_map.h"
#include "insys/actions/composite.h"
#include "insys/actions/descriptor_parser.h"
#include "insys/actions/interaction.h"
#include "insys/actions/processor.h"
#include "insys/control_path.h"
#include "insys/device.h"
#include "insys/manager.h"
#include "insys/string_utils.h"

namespace insys {
namespace {

struct ResolvedBinding {
    std::vector<Control*> controls;
    int controlStart = 0;
    int processorStart = 0;
    int processorCount = 0;
    int interactionStart = 0;
    int interactionCount = 0;
    int actionIndex = kInvalidIndex;
    int mapIndex = 0;
    int compositeIndex = kInvalidIndex;
    int compositeBindingIndex = kInvalidIndex;
    int partIndex = kInvalidIndex;
    bool isComposite = false;
    bool isPart = false;
    bool wantsInitialCheck = false;
    bool disabled = false;
};

bool device_allowed(Device* device, const std::vector<Device*>* devices) {
    if (devices == nullptr)
        return true;
    return std::find(devices->begin(), devices->end(), device) != devices->end();
}

std::vector<Control*> find_controls(Manager& manager, std::string_view path,
                                   const std::vector<Device*>* devices) {
    std::vector<Control*> result;
    if (path.empty())
        return result;

    for (Device* device : manager.devices()) {
        if (device == nullptr || !device_allowed(device, devices))
            continue;
        for (Control* control : device->allControls) {
            if (path_matches(path, *control))
                result.push_back(control);
        }
    }
    return result;
}

std::string_view trim_group(std::string_view text) {
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
            text.front() == '\n'))
        text.remove_prefix(1);
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
            text.back() == '\n'))
        text.remove_suffix(1);
    return text;
}

bool group_list_contains(std::string_view groups, std::string_view expected) {
    while (!groups.empty()) {
        const std::size_t separator = groups.find_first_of(";,");
        std::string_view token =
            separator == std::string_view::npos ? groups : groups.substr(0, separator);
        token = trim_group(token);
        if (!token.empty() && equals_ignore_case(token, expected))
            return true;
        if (separator == std::string_view::npos)
            break;
        groups.remove_prefix(separator + 1);
    }
    return false;
}

bool binding_matches_mask(const Binding& binding, const Binding* mask) {
    if (mask == nullptr)
        return true;
    if (!mask->name.empty() && !equals_ignore_case(binding.name, mask->name))
        return false;
    if (!mask->effective_path().empty() &&
        binding.effective_path() != mask->effective_path())
        return false;
    if (!mask->groups.empty()) {
        if (binding.groups.empty())
            return true;
        std::string_view maskGroups = mask->groups;
        while (!maskGroups.empty()) {
            const std::size_t separator = maskGroups.find_first_of(";,");
            std::string_view token =
                separator == std::string_view::npos ? maskGroups
                                                    : maskGroups.substr(0, separator);
            token = trim_group(token);
            if (!token.empty() && group_list_contains(binding.groups, token))
                return true;
            if (separator == std::string_view::npos)
                break;
            maskGroups.remove_prefix(separator + 1);
        }
        return false;
    }
    return true;
}

ValueType value_type_of_control(const Control* control) {
    if (control == nullptr)
        return ValueType::None;
    if (dynamic_cast<const DpadControl*>(control) != nullptr)
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

void configure_composite_part(IComposite& composite, int partIndex,
                              const std::vector<Control*>& controls) {
    ValueType type = ValueType::None;
    std::size_t size = 0;
    bool isButton = true;
    for (const Control* control : controls) {
        if (control == nullptr)
            continue;
        const ValueType controlType = value_type_of_control(control);
        if (type == ValueType::None)
            type = controlType;
        else if (type != controlType)
            type = ValueType::None;
        size = std::max<std::size_t>(size, control->value_size_bytes());
        isButton = isButton && control->is_button();
    }
    composite.configure_part(partIndex, type, size, isButton);
}

void add_processors(ResolvedBindings& resolved, std::string_view text,
                    std::vector<int>& ranges) {
    for (const NameAndParameters& item : parse_list(text)) {
        auto processor = make_processor(item.name, item.parameters);
        if (!processor)
            continue;
        resolved.processorStorage.push_back(std::move(processor));
        resolved.processors.push_back(resolved.processorStorage.back().get());
        ranges.push_back(static_cast<int>(resolved.processorStorage.size()) - 1);
    }
}

void add_interactions(ResolvedBindings& resolved, std::string_view text,
                      std::vector<int>& ranges) {
    for (const NameAndParameters& item : parse_list(text)) {
        auto interaction = make_interaction(item.name, item.parameters);
        if (!interaction)
            continue;
        resolved.interactionStorage.push_back(std::move(interaction));
        resolved.interactions.push_back(resolved.interactionStorage.back().get());
        ranges.push_back(static_cast<int>(resolved.interactionStorage.size()) - 1);
    }
}

} // namespace

ResolvedBindings BindingResolver::resolve(const std::vector<ActionMap*>& maps) {
    ResolvedBindings resolved;
    resolved.maps = maps;

    int totalBindings = 0;
    for (ActionMap* map : maps)
        totalBindings += static_cast<int>(map->bindings.size());

    std::vector<ResolvedBinding> bindings(static_cast<std::size_t>(totalBindings));

    int bindingIndex = 0;
    for (int mapIndex = 0; mapIndex < static_cast<int>(maps.size()); ++mapIndex) {
        ActionMap* map = maps[static_cast<std::size_t>(mapIndex)];
        map->mapIndex = mapIndex;

        for (ActionDef& action : map->actions) {
            action.actionIndex = static_cast<int>(resolved.actions.size());
            action.mapIndex = mapIndex;
            action.state = nullptr;
            resolved.actions.push_back(&action);
        }

        int currentCompositeBindingIndex = kInvalidIndex;
        int currentCompositeIndex = kInvalidIndex;
        int currentCompositeActionIndex = kInvalidIndex;
        int currentCompositePartCount = 0;

        for (Binding& binding : map->bindings) {
            ResolvedBinding& info = bindings[static_cast<std::size_t>(bindingIndex)];
            info.isComposite = binding.isComposite;
            info.isPart = !binding.isComposite && binding.isPartOfComposite;
            info.mapIndex = mapIndex;
            if (!binding_matches_mask(binding, m_bindingMask))
                info.disabled = true;

            ActionDef* action = nullptr;
            if (info.isPart) {
                if (currentCompositeBindingIndex < 0) {
                    info.disabled = true;
                } else {
                    info.compositeBindingIndex = currentCompositeBindingIndex;
                    info.compositeIndex = currentCompositeIndex;
                    if (currentCompositeActionIndex >= 0)
                        action = resolved.actions[static_cast<std::size_t>(currentCompositeActionIndex)];
                }
            } else if (!binding.action.empty()) {
                action = map->find_action(binding.action);
            } else if (map->actions.size() == 1) {
                action = &map->actions.front();
            }

            if (info.isComposite && !info.disabled) {
                currentCompositeBindingIndex = bindingIndex;
                currentCompositeIndex = static_cast<int>(resolved.compositeStorage.size());
                currentCompositePartCount = 0;
                currentCompositeActionIndex = action != nullptr ? action->actionIndex : kInvalidIndex;

                const std::vector<NameAndParameters> items = parse_list(binding.path);
                std::unique_ptr<IComposite> composite;
                if (!items.empty())
                    composite = make_composite(items.front().name, items.front().parameters);
                if (!composite) {
                    info.disabled = true;
                    currentCompositeBindingIndex = kInvalidIndex;
                    currentCompositeIndex = kInvalidIndex;
                } else {
                    resolved.compositeStorage.push_back(std::move(composite));
                    resolved.composites.push_back(resolved.compositeStorage.back().get());
                    info.compositeIndex = currentCompositeIndex;
                }
            } else if (!info.isPart) {
                currentCompositeBindingIndex = kInvalidIndex;
                currentCompositeIndex = kInvalidIndex;
                currentCompositeActionIndex = kInvalidIndex;
                currentCompositePartCount = 0;
            }

            if (!info.disabled && action == nullptr)
                info.disabled = true;

            if (!info.disabled)
                info.actionIndex = action->actionIndex;
            else if (info.isPart && currentCompositeActionIndex >= 0)
                info.actionIndex = currentCompositeActionIndex;

            IComposite* composite =
                info.compositeIndex >= 0
                    ? resolved.composites[static_cast<std::size_t>(info.compositeIndex)]
                    : nullptr;

            if (!info.disabled && !info.isComposite) {
                info.controls = find_controls(*m_manager, binding.effective_path(), m_devices);
                if (info.controls.empty())
                    info.disabled = true;
            }

            if (info.isPart && composite != nullptr && !info.disabled) {
                int partCount = currentCompositePartCount;
                const int partIndex = composite->assign_part(binding.name, partCount);
                binding.partIndex = partIndex;
                if (partIndex < 0) {
                    info.disabled = true;
                } else {
                    currentCompositePartCount = partCount;
                    info.partIndex = partIndex;
                    composite->set_part_binding_index(partIndex, bindingIndex);
                    configure_composite_part(*composite, partIndex, info.controls);
                }
            }

            if (!info.disabled && info.isPart) {
                ResolvedBinding& compositeInfo =
                    bindings[static_cast<std::size_t>(currentCompositeBindingIndex)];
                compositeInfo.controls.insert(compositeInfo.controls.end(), info.controls.begin(),
                                              info.controls.end());
            }

            if (!info.disabled && !info.isComposite) {
                info.controlStart = static_cast<int>(resolved.controls.size());
                for (Control* control : info.controls)
                    resolved.controls.push_back(control);
            } else if (info.isComposite) {
                info.controlStart = static_cast<int>(resolved.controls.size());
            }

            if (!info.disabled) {
                std::vector<int> processorIndices;
                add_processors(resolved, binding.effective_processors(), processorIndices);
                if (action != nullptr)
                    add_processors(resolved, action->processors, processorIndices);
                if (!processorIndices.empty()) {
                    info.processorStart = processorIndices.front();
                    info.processorCount = static_cast<int>(processorIndices.size());
                }

                if (info.isPart && currentCompositeBindingIndex >= 0) {
                    const ResolvedBinding& compositeInfo =
                        bindings[static_cast<std::size_t>(currentCompositeBindingIndex)];
                    info.interactionStart = compositeInfo.interactionStart;
                    info.interactionCount = compositeInfo.interactionCount;
                } else {
                    std::vector<int> interactionIndices;
                    add_interactions(resolved, binding.effective_interactions(),
                                     interactionIndices);
                    if (action != nullptr)
                        add_interactions(resolved, action->interactions, interactionIndices);
                    if (!interactionIndices.empty()) {
                        info.interactionStart = interactionIndices.front();
                        info.interactionCount = static_cast<int>(interactionIndices.size());
                    }
                }
            }

            if (action != nullptr &&
                (action->type == ActionType::Value || action->initialStateCheck))
                info.wantsInitialCheck = true;

            ++bindingIndex;
        }
    }

    resolved.memory.allocate(static_cast<int>(maps.size()),
                             static_cast<int>(resolved.actions.size()),
                             static_cast<int>(resolved.interactions.size()),
                             static_cast<int>(bindings.size()),
                             static_cast<int>(resolved.controls.size()),
                             static_cast<int>(resolved.composites.size()));

    ActionStateMemory& memory = resolved.memory;

    for (int i = 0; i < memory.actionCount; ++i) {
        memory.actionStates[i].set_phase(ActionPhase::Disabled);
        memory.actionStates[i].controlIndex = kInvalidIndex16;
        memory.actionStates[i].bindingIndex = kInvalidIndex16;
        memory.actionStates[i].interactionIndex = kInvalidIndex16;
        memory.actionStates[i].mapIndex =
            static_cast<uint8_t>(resolved.actions[static_cast<std::size_t>(i)]->mapIndex);
    }

    int controlCursor = 0;
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        ResolvedBinding& info = bindings[i];
        BindingState& state = memory.bindingStates[i];

        if (info.isComposite)
            state.flags |= kBindingComposite;
        if (info.isPart)
            state.flags |= kBindingPartOfComposite;
        if (info.wantsInitialCheck)
            state.flags |= kBindingWantsInitialStateCheck;
        if (info.disabled)
            state.flags |= kBindingDisabled;

        state.actionIndex = info.actionIndex >= 0 ? static_cast<uint16_t>(info.actionIndex)
                                                  : kInvalidIndex16;
        state.mapIndex = static_cast<uint8_t>(info.mapIndex);
        state.controlStartIndex = static_cast<uint16_t>(controlCursor);
        state.controlCount = static_cast<uint8_t>(info.controls.size());
        state.processorStartIndex = info.processorStart >= 0
                                        ? static_cast<uint16_t>(info.processorStart)
                                        : kInvalidIndex16;
        state.processorCount = static_cast<uint8_t>(info.processorCount);
        state.interactionStartIndex = info.interactionStart >= 0
                                          ? static_cast<uint16_t>(info.interactionStart)
                                          : kInvalidIndex16;
        state.interactionCount = static_cast<uint8_t>(info.interactionCount);
        state.partIndex = info.partIndex >= 0 ? static_cast<uint8_t>(info.partIndex) : 0;
        if (info.isComposite)
            state.compositeOrCompositeBindingIndex =
                info.compositeIndex >= 0 ? static_cast<uint16_t>(info.compositeIndex)
                                         : kInvalidIndex16;
        else if (info.isPart)
            state.compositeOrCompositeBindingIndex =
                info.compositeBindingIndex >= 0
                    ? static_cast<uint16_t>(info.compositeBindingIndex)
                    : kInvalidIndex16;
        else
            state.compositeOrCompositeBindingIndex = kInvalidIndex16;

        if (!info.isComposite) {
            for (Control* control : info.controls) {
                (void)control;
                if (controlCursor < memory.controlCount)
                    memory.controlIndexToBindingIndex[controlCursor] = static_cast<int32_t>(i);
                ++controlCursor;
            }
        }
    }

    int bindingCursor = 0;
    for (std::size_t i = 0; i < maps.size(); ++i) {
        ActionMap* map = maps[i];
        ActionMapIndices& indices = memory.mapIndices[i];
        indices.actionStart = map->actions.empty() ? 0 : map->actions.front().actionIndex;
        indices.actionCount = static_cast<int>(map->actions.size());
        indices.bindingStart = bindingCursor;
        indices.bindingCount = static_cast<int>(map->bindings.size());
        if (indices.bindingCount > 0) {
            const int first = indices.bindingStart;
            const int last = indices.bindingStart + indices.bindingCount - 1;
            indices.controlStart = memory.bindingStates[first].controlStartIndex;
            indices.controlCount =
                memory.bindingStates[last].controlStartIndex +
                memory.bindingStates[last].controlCount - indices.controlStart;
        } else {
            indices.controlStart = 0;
            indices.controlCount = 0;
        }
        bindingCursor += indices.bindingCount;
    }

    int actionBindingCursor = 0;
    for (int actionIndex = 0; actionIndex < memory.actionCount; ++actionIndex) {
        int count = 0;
        for (std::size_t i = 0; i < bindings.size(); ++i) {
            if (bindings[i].actionIndex == actionIndex && !bindings[i].disabled)
                ++count;
        }
        memory.actionBindingIndicesAndCounts[actionIndex * 2] =
            static_cast<uint16_t>(actionBindingCursor);
        memory.actionBindingIndicesAndCounts[actionIndex * 2 + 1] = static_cast<uint16_t>(count);
        for (std::size_t i = 0; i < bindings.size(); ++i) {
            if (bindings[i].actionIndex == actionIndex && !bindings[i].disabled)
                memory.actionBindingIndices[actionBindingCursor++] = static_cast<uint16_t>(i);
        }
    }

    return resolved;
}

} // namespace insys
