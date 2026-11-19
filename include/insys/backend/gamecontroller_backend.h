#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "insys/backend/gamecontroller_mapping.h"
#include "insys/backend/runtime.h"
#include "insys/manager.h"

namespace insys::backend {

class GameControllerBackend {
public:
    bool initialize(Runtime& runtime, Manager& manager);
    void shutdown();

    bool add_controller(std::string id, const DeviceDescription& description = {});
    bool remove_controller(std::string_view id);
    bool update_controller(std::string_view id, const GameControllerState& state);
    void poll();

    bool connected(std::string_view id) const;
    int device_id(std::string_view id) const;

private:
    struct Slot {
        int deviceId = 0;
        GamepadState state{};
        bool dirty = false;
    };

    Runtime* m_runtime = nullptr;
    Manager* m_manager = nullptr;
    std::unordered_map<std::string, Slot> m_slots;
    std::vector<std::string> m_dirtyControllers;
};

} // namespace insys::backend
