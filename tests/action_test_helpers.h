#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

#include "insys/backend/iruntime.h"
#include "insys/device.h"
#include "insys/device_builder.h"
#include "insys/device_states.h"
#include "insys/low/event.h"
#include "insys/low/key.h"
#include "insys/low/state_format.h"
#include "insys/manager.h"

namespace insys_test {

class TestRuntime final : public insys::IRuntime {
public:
    void set_manager(insys::Manager* manager) override { m_manager = manager; }
    int allocate_device_id() override { return m_nextDeviceId++; }
    void queue_event(const insys::low::Event* event) override {
        if (m_manager != nullptr)
            m_manager->queue_event(event);
    }
    long device_command(int, insys::DeviceCommand*) override { return -1; }

    double now() const override { return m_now; }
    double now_for_fixed() const override { return m_now; }
    float unscaled_game_time() const override { return static_cast<float>(m_now); }
    insys::Vector2 screen_size() const override { return {1280.0f, 720.0f}; }
    int screen_orientation() const override { return m_screenOrientation; }
    bool run_in_background() const override { return false; }
    void set_run_in_background(bool) override {}
    insys::FocusState focus_state() const override { return insys::FocusState::Normal; }
    void set_focus_state(insys::FocusState) override {}
    float polling_frequency() const override { return 60.0f; }
    void set_polling_frequency(float) override {}

    void set_screen_orientation(int orientation) { m_screenOrientation = orientation; }

    double m_now = 1.0;

private:
    insys::Manager* m_manager = nullptr;
    int m_nextDeviceId = 1;
    int m_screenOrientation = static_cast<int>(insys::ScreenOrientation::Unknown);
};

inline insys::Keyboard* add_keyboard(insys::Manager& manager) {
    auto keyboard = insys::make_keyboard(manager.runtime().allocate_device_id());
    return static_cast<insys::Keyboard*>(manager.add_device(std::move(keyboard)));
}

inline insys::Gamepad* add_gamepad(insys::Manager& manager) {
    auto gamepad = std::unique_ptr<insys::Device>(
        insys::DeviceBuilder(manager.layouts())
            .build("Gamepad", "", manager.runtime().allocate_device_id(),
                   insys::DeviceDescription{}));
    return static_cast<insys::Gamepad*>(manager.add_device(std::move(gamepad)));
}

inline void set_keys(insys::Manager& manager, insys::Device* keyboard,
                     std::initializer_list<insys::Key> keys, double time) {
    std::array<uint8_t, (insys::kKeyCount + 7) / 8> state{};
    for (insys::Key key : keys) {
        const uint32_t bit = static_cast<uint32_t>(key);
        state[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
    }
    const auto event = insys::low::make_state_event(
        insys::kFormatKeyboard, state.data(), static_cast<uint32_t>(state.size()),
        keyboard->deviceId, time);
    manager.queue_event(reinterpret_cast<const insys::low::Event*>(event.data()));
    manager.update(insys::UpdateType::Dynamic);
}

inline void set_gamepad_stick(insys::Manager& manager, insys::Device* gamepad,
                              insys::Vector2 leftStick, double time) {
    insys::GamepadState state;
    state.leftStick = leftStick;
    const auto event = insys::low::make_state_event(
        insys::kFormatGamepad, &state, sizeof(state), gamepad->deviceId, time);
    manager.queue_event(reinterpret_cast<const insys::low::Event*>(event.data()));
    manager.update(insys::UpdateType::Dynamic);
}

inline void set_gamepad_buttons(insys::Manager& manager, insys::Device* gamepad,
                                uint32_t buttons, double time) {
    insys::GamepadState state;
    state.buttons = buttons;
    const auto event = insys::low::make_state_event(
        insys::kFormatGamepad, &state, sizeof(state), gamepad->deviceId, time);
    manager.queue_event(reinterpret_cast<const insys::low::Event*>(event.data()));
    manager.update(insys::UpdateType::Dynamic);
}

} // namespace insys_test
