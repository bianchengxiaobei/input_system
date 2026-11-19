#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "core/bit_math.h"
#include "insys/backend/iruntime.h"
#include "insys/device.h"
#include "insys/low/event.h"
#include "insys/low/state_format.h"
#include "insys/manager.h"
#include "insys/state_buffers.h"

using namespace insys;

namespace {

class TestRuntime final : public IRuntime {
public:
    void set_manager(Manager* manager) override { m_manager = manager; }
    int allocate_device_id() override { return m_nextDeviceId++; }
    void queue_event(const low::Event* event) override {
        if (m_manager != nullptr)
            m_manager->queue_event(event);
    }
    long device_command(int, DeviceCommand*) override { return -1; }

    double now() const override { return m_now; }
    double now_for_fixed() const override { return m_now; }
    float unscaled_game_time() const override { return static_cast<float>(m_now); }
    Vector2 screen_size() const override { return {1280.0f, 720.0f}; }
    int screen_orientation() const override { return 0; }
    bool run_in_background() const override { return false; }
    void set_run_in_background(bool) override {}
    FocusState focus_state() const override { return FocusState::Normal; }
    void set_focus_state(FocusState) override {}
    float polling_frequency() const override { return 60.0f; }
    void set_polling_frequency(float) override {}

    double m_now = 1.0;

private:
    Manager* m_manager = nullptr;
    int m_nextDeviceId = 1;
};

} // namespace

TEST_CASE("Keyboard device exposes one control per key") {
    auto keyboard = make_keyboard(1);
    auto* keyboardPtr = static_cast<Keyboard*>(keyboard.get());
    REQUIRE(keyboardPtr->allControls.size() == kKeyCount + 2);
    REQUIRE(keyboardPtr->children.size() == kKeyCount + 1);
    REQUIRE(keyboardPtr->state_size_bytes() == (kKeyCount + 8) / 8);
    REQUIRE(keyboardPtr->key_control(Key::A) != nullptr);
    REQUIRE(keyboardPtr->key_control(Key::Space) != nullptr);
}

TEST_CASE("Mouse device layout matches the backend state") {
    auto mouse = make_mouse(2);
    auto* mousePtr = static_cast<Mouse*>(mouse.get());
    REQUIRE(mousePtr->position() != nullptr);
    REQUIRE(mousePtr->delta() != nullptr);
    REQUIRE(mousePtr->scroll() != nullptr);
    REQUIRE(mousePtr->left_button() != nullptr);
    REQUIRE(mousePtr->right_button() != nullptr);
    REQUIRE(mousePtr->middle_button() != nullptr);
    REQUIRE(mousePtr->state_size_bytes() == 28);
}

TEST_CASE("StateBuffers gives non-overlapping device regions") {
    auto keyboard = make_keyboard(1);
    auto mouse = make_mouse(2);
    std::vector<Device*> devices{keyboard.get(), mouse.get()};

    StateBuffers buffers;
    buffers.allocate_all(devices);

    REQUIRE(buffers.total_size() > 0);
    REQUIRE(buffers.front_for(0) != nullptr);
    REQUIRE(buffers.front_for(1) != nullptr);
    REQUIRE(buffers.front_for(0) != buffers.front_for(1));
    REQUIRE(buffers.front_for(0) + keyboard->state_size_bytes() <= buffers.front_for(1));
    REQUIRE(buffers.defaultState.size() == buffers.total_size());
    REQUIRE(buffers.noiseMask.size() == buffers.total_size());
    REQUIRE(buffers.resetMask.size() == buffers.total_size());

    std::memset(buffers.front_for(0), 0xAA, keyboard->state_size_bytes());
    std::memset(buffers.front_for(1), 0x55, mouse->state_size_bytes());
    REQUIRE(buffers.front_for(0)[0] == 0xAA);
    REQUIRE(buffers.front_for(1)[0] == 0x55);

    buffers.swap(0);
    REQUIRE(buffers.front_for(0)[0] == 0xAA);
}

TEST_CASE("Manager applies state events and flips at most once per update") {
    TestRuntime runtime;
    Manager manager(runtime);

    auto keyboard = make_keyboard(runtime.allocate_device_id());
    Device* device = manager.add_device(std::move(keyboard));
    REQUIRE(device != nullptr);
    REQUIRE(device->currentStatePtr != nullptr);

    std::array<uint8_t, (kKeyCount + 7) / 8> state{};
    detail::write_single_bit(state.data(), static_cast<uint32_t>(Key::A), true);

    const auto first = low::make_state_event(kFormatKeyboard, state.data(),
                                            static_cast<uint32_t>(state.size()),
                                            device->deviceId, 1.0);
    manager.queue_event(reinterpret_cast<const low::Event*>(first.data()));

    const auto second = low::make_state_event(kFormatKeyboard, state.data(),
                                             static_cast<uint32_t>(state.size()),
                                             device->deviceId, 1.1);
    manager.queue_event(reinterpret_cast<const low::Event*>(second.data()));

    manager.update(UpdateType::Dynamic);

    REQUIRE(manager.update_step_count() == 1);
    REQUIRE(device->lastUpdateStepCount == 1);

    auto* keyboardDevice = static_cast<Keyboard*>(device);
    REQUIRE(keyboardDevice->key_control(Key::A)->read_float(device->current_state()) == 1.0f);
    REQUIRE(keyboardDevice->key_control(Key::B)->read_float(device->current_state()) == 0.0f);
}

TEST_CASE("Manager exposes text events") {
    TestRuntime runtime;
    Manager manager(runtime);

    const auto event = low::make_text_event('x', 0, 1.0);
    manager.queue_event(reinterpret_cast<const low::Event*>(event.data()));
    manager.update(UpdateType::Dynamic);

    REQUIRE(manager.pending_text().size() == 1);
    REQUIRE(manager.pending_text()[0] == U'x');
    manager.clear_pending_text();
    REQUIRE(manager.pending_text().empty());
}
