#pragma once

#include <functional>
#include <string>

#include "insys/guid.h"

namespace insys {

class ActionState;
class Control;
struct CallbackContext;

enum class ActionType {
    Value,
    Button,
    PassThrough,
};

enum class ActionPhase {
    Invalid = -1,
    Disabled,
    Waiting,
    Started,
    Performed,
    Canceled,
};

inline const char* action_phase_name(ActionPhase phase) {
    switch (phase) {
    case ActionPhase::Disabled: return "Disabled";
    case ActionPhase::Waiting: return "Waiting";
    case ActionPhase::Started: return "Started";
    case ActionPhase::Performed: return "Performed";
    case ActionPhase::Canceled: return "Canceled";
    case ActionPhase::Invalid:
    default: return "Invalid";
    }
}

struct ActionDef {
    std::string name;
    Guid id = Guid::create();
    ActionType type = ActionType::Button;
    std::string expectedControlType;
    std::string processors;
    std::string interactions;
    bool initialStateCheck = false;
    int priority = 0;

    ActionState* state = nullptr;
    int actionIndex = -1;
    int mapIndex = -1;

    std::function<void(const CallbackContext&)> started;
    std::function<void(const CallbackContext&)> performed;
    std::function<void(const CallbackContext&)> canceled;

    bool is_button() const { return type == ActionType::Button; }
    bool is_value() const { return type == ActionType::Value; }
    bool is_pass_through() const { return type == ActionType::PassThrough; }
};

} // namespace insys
