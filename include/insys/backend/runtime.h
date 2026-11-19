#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <vector>

#include "insys/backend/iruntime.h"

namespace insys::backend {

class Runtime final : public IRuntime {
public:
    Runtime();

    void set_manager(Manager* manager) override;
    int allocate_device_id() override;
    void queue_event(const low::Event* event) override;
    long device_command(int deviceId, DeviceCommand* command) override;

    using DeviceCommandHandler = std::function<long(int, DeviceCommand*)>;
    void set_device_command_handler(DeviceCommandHandler handler);
    void clear_device_command_handler();
    int add_device_command_handler(DeviceCommandHandler handler);
    void remove_device_command_handler(int handlerId);

    double now() const override;
    double now_for_fixed() const override;
    float unscaled_game_time() const override;

    Vector2 screen_size() const override { return m_screenSize; }
    int screen_orientation() const override { return m_screenOrientation; }
    bool run_in_background() const override { return m_runInBackground; }
    void set_run_in_background(bool value) override { m_runInBackground = value; }
    FocusState focus_state() const override { return m_focusState; }
    void set_focus_state(FocusState value) override { m_focusState = value; }
    float polling_frequency() const override { return m_pollingFrequency; }
    void set_polling_frequency(float value) override { m_pollingFrequency = value; }

    void set_screen_size(Vector2 size) { m_screenSize = size; }
    void set_screen_orientation(int orientation) { m_screenOrientation = orientation; }

private:
    Manager* m_manager = nullptr;
    std::atomic<int> m_nextDeviceId{1};
    std::chrono::steady_clock::time_point m_start;
    Vector2 m_screenSize{1920.0f, 1080.0f};
    int m_screenOrientation = static_cast<int>(ScreenOrientation::Unknown);
    FocusState m_focusState = FocusState::Normal;
    bool m_runInBackground = false;
    float m_pollingFrequency = 60.0f;
    struct CommandHandlerEntry {
        int id = 0;
        DeviceCommandHandler handler;
    };

    DeviceCommandHandler m_deviceCommandHandler;
    std::vector<CommandHandlerEntry> m_deviceCommandHandlers;
    int m_nextDeviceCommandHandlerId = 1;
};

} // namespace insys::backend

