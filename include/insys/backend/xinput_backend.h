#pragma once

#if defined(_WIN32)

#include <array>

#include "insys/backend/runtime.h"
#include "insys/device_states.h"
#include "insys/manager.h"

namespace insys::backend {

class XInputBackend {
public:
    XInputBackend() = default;
    ~XInputBackend();

    XInputBackend(const XInputBackend&) = delete;
    XInputBackend& operator=(const XInputBackend&) = delete;

    bool initialize(Runtime& runtime, Manager& manager);
    void shutdown();
    void poll();

    bool set_rumble(int slot, float lowFrequency, float highFrequency);
    int device_id(int slot) const;
    bool connected(int slot) const;

private:
    struct Slot {
        bool connected = false;
        int deviceId = 0;
        GamepadState state{};
        float lowFrequency = 0.0f;
        float highFrequency = 0.0f;
    };

    void remove_slot(int slot);
    long handle_device_command(int deviceId, DeviceCommand* command);

    Runtime* m_runtime = nullptr;
    Manager* m_manager = nullptr;
    std::array<Slot, 4> m_slots{};
};

} // namespace insys::backend

#endif
