#pragma once

#include <string_view>
#include <vector>

#include "insys/actions/input_user.h"

namespace insys {

class PlayerInput : public InputUser {
public:
    static PlayerInput* join(Manager& manager, const ActionAsset& actions,
                             std::string_view defaultActionMap = {});
    static std::vector<PlayerInput*> all();

    int player_index() const;
    bool switch_current_action_map(std::string_view nameOrId) {
        return switch_action_map(nameOrId);
    }

protected:
    explicit PlayerInput(Manager& manager) : InputUser(manager) {}
};

} // namespace insys