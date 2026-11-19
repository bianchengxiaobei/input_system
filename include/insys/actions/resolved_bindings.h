#pragma once

#include <memory>
#include <vector>

#include "insys/actions/action_state_memory.h"
#include "insys/actions/composite.h"
#include "insys/actions/interaction.h"
#include "insys/actions/processor.h"

namespace insys {

class ActionMap;
struct ActionDef;
class Control;

struct ResolvedBindings {
    std::vector<ActionMap*> maps;
    std::vector<ActionDef*> actions;
    std::vector<Control*> controls;
    std::vector<IInteraction*> interactions;
    std::vector<IProcessor*> processors;
    std::vector<IComposite*> composites;

    std::vector<std::unique_ptr<IInteraction>> interactionStorage;
    std::vector<std::unique_ptr<IProcessor>> processorStorage;
    std::vector<std::unique_ptr<IComposite>> compositeStorage;

    ActionStateMemory memory;
};

} // namespace insys
