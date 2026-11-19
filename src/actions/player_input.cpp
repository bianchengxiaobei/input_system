#include "insys/actions/player_input.h"

#include "insys/manager.h"

namespace insys {

PlayerInput* PlayerInput::join(Manager& manager, const ActionAsset& actions,
                               std::string_view defaultActionMap) {
    return static_cast<PlayerInput*>(register_user(
        std::unique_ptr<PlayerInput>(new PlayerInput(manager)), manager, actions, true, false,
        defaultActionMap));
}

std::vector<PlayerInput*> PlayerInput::all() {
    std::vector<PlayerInput*> players;
    for (InputUser* user : InputUser::all()) {
        if (auto* player = dynamic_cast<PlayerInput*>(user))
            players.push_back(player);
    }
    return players;
}

int PlayerInput::player_index() const {
    const std::vector<PlayerInput*> players = PlayerInput::all();
    for (std::size_t i = 0; i < players.size(); ++i) {
        if (players[static_cast<std::size_t>(i)] == this)
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace insys
