#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <memory>

#include "insys/backend/iruntime.h"
#include "insys/device.h"
#include "insys/device_builder.h"
#include "insys/layout_registry.h"
#include "insys/device_states.h"
#include "insys/low/event.h"
#include "insys/low/state_format.h"
#include "insys/manager.h"

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

DeviceDescription description(std::string product) {
    DeviceDescription value;
    value.interfaceName = "Test";
    value.product = std::move(product);
    return value;
}

} // namespace

TEST_CASE("Gamepad layout exposes sticks, triggers and buttons") {
    LayoutRegistry registry;
    register_default_layouts(registry);
    DeviceBuilder builder(registry);

    auto device = builder.build("Gamepad", "", 5, description("Test Gamepad"));
    REQUIRE(device != nullptr);
    auto* gamepad = static_cast<Gamepad*>(device.get());

    REQUIRE(gamepad->left_stick() != nullptr);
    REQUIRE(gamepad->right_stick() != nullptr);
    REQUIRE(gamepad->left_trigger() != nullptr);
    REQUIRE(gamepad->right_trigger() != nullptr);
    REQUIRE(gamepad->dpad() != nullptr);
    REQUIRE(gamepad->button("buttonSouth") != nullptr);

    REQUIRE(gamepad->left_stick()->stateBlock.byteOffset == 4);
    REQUIRE(gamepad->right_stick()->stateBlock.byteOffset == 12);
    REQUIRE(gamepad->left_trigger()->stateBlock.byteOffset == 20);
    REQUIRE(gamepad->right_trigger()->stateBlock.byteOffset == 24);
    REQUIRE(gamepad->state_size_bytes() == 28);
}

TEST_CASE("Gamepad state events update controls") {
    TestRuntime runtime;
    Manager manager(runtime);

    Gamepad* gamepad = static_cast<Gamepad*>(
        manager.create_device("Gamepad", description("Test Gamepad")));
    REQUIRE(gamepad != nullptr);

    GamepadState state;
    state.buttons = (1u << 6) | (1u << 0) | (1u << 3);
    state.leftStick = {0.5f, -0.25f};
    state.rightStick = {-0.75f, 0.125f};
    state.leftTrigger = 0.75f;
    state.rightTrigger = 0.2f;

    const auto event = low::make_state_event(kFormatGamepad, &state, sizeof(state),
                                            gamepad->deviceId, 1.0);
    manager.queue_event(reinterpret_cast<const low::Event*>(event.data()));
    manager.update(UpdateType::Dynamic);

    const uint8_t* current = gamepad->current_state();
    REQUIRE(current != nullptr);
    REQUIRE(gamepad->button("buttonSouth")->read_float(current) == 1.0f);
    REQUIRE(gamepad->button("buttonNorth")->read_float(current) == 0.0f);

    const Vector2 left = gamepad->left_stick()->read_vector2(current);
    REQUIRE(left.x == 0.5f);
    REQUIRE(left.y == -0.25f);

    const Vector2 right = gamepad->right_stick()->read_vector2(current);
    REQUIRE(right.x == -0.75f);
    REQUIRE(right.y == 0.125f);

    REQUIRE(gamepad->left_trigger()->read_float(current) == 0.75f);
    REQUIRE(gamepad->right_trigger()->read_float(current) == 0.2f);

    const Vector2 dpad = gamepad->dpad()->read_vector2(current);
    REQUIRE(dpad.x == 1.0f);
    REQUIRE(dpad.y == 1.0f);
}

TEST_CASE("Mouse layout reads position and buttons") {
    LayoutRegistry registry;
    register_default_layouts(registry);
    DeviceBuilder builder(registry);

    auto device = builder.build("Mouse", "", 4, description("Test Mouse"));
    REQUIRE(device != nullptr);
    auto* mouse = static_cast<Mouse*>(device.get());

    MouseState state;
    state.position = {100.0f, 200.0f};
    state.delta = {3.0f, -4.0f};
    state.scroll = {0.0f, 1.0f};
    state.buttons[0] |= 0x01;

    std::array<uint8_t, sizeof(MouseState)> buffer{};
    std::memcpy(buffer.data(), &state, sizeof(state));

    REQUIRE(mouse->position()->read_vector2(buffer.data()).x == 100.0f);
    REQUIRE(mouse->delta()->read_vector2(buffer.data()).y == -4.0f);
    REQUIRE(mouse->left_button()->read_float(buffer.data()) == 1.0f);
    REQUIRE(mouse->right_button()->read_float(buffer.data()) == 0.0f);
}

TEST_CASE("Keyboard anyKey reacts to any key bit") {
    TestRuntime runtime;
    Manager manager(runtime);

    Keyboard* keyboard = static_cast<Keyboard*>(
        manager.create_device("Keyboard", description("Test Keyboard")));
    REQUIRE(keyboard != nullptr);

    std::array<uint8_t, (kKeyCount + 8) / 8> state{};
    state[static_cast<std::size_t>(Key::A) / 8] |= static_cast<uint8_t>(
        1u << (static_cast<std::size_t>(Key::A) % 8));

    const auto event = low::make_state_event(kFormatKeyboard, state.data(),
                                            static_cast<uint32_t>(state.size()),
                                            keyboard->deviceId, 1.0);
    manager.queue_event(reinterpret_cast<const low::Event*>(event.data()));
    manager.update(UpdateType::Dynamic);

    REQUIRE(keyboard->any_key()->read_float(keyboard->current_state()) == 1.0f);
    REQUIRE(keyboard->key_control(Key::A)->read_float(keyboard->current_state()) == 1.0f);
    REQUIRE(keyboard->key_control(Key::B)->read_float(keyboard->current_state()) == 0.0f);
}
