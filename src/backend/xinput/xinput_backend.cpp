#include "insys/backend/xinput_backend.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "insys/backend/xinput_mapping.h"
#include "insys/device_commands.h"
#include "insys/device_states.h"
#include "insys/low/state_format.h"

namespace insys::backend {

namespace {

constexpr int kXInputSlotCount = 4;

XInputPadState to_pad_state(const XINPUT_GAMEPAD& gamepad) {
    XInputPadState state;
    state.buttons = gamepad.wButtons;
    state.leftTrigger = gamepad.bLeftTrigger;
    state.rightTrigger = gamepad.bRightTrigger;
    state.leftThumbX = gamepad.sThumbLX;
    state.leftThumbY = gamepad.sThumbLY;
    state.rightThumbX = gamepad.sThumbRX;
    state.rightThumbY = gamepad.sThumbRY;
    return state;
}

std::string capabilities_string(const XINPUT_CAPABILITIES& capabilities) {
    std::string text = "type=" + std::to_string(capabilities.Type);
    text += ";subtype=" + std::to_string(capabilities.SubType);
    text += ";flags=" + std::to_string(capabilities.Flags);
    return text;
}

} // namespace

XInputBackend::~XInputBackend() {
    shutdown();
}

bool XInputBackend::initialize(Runtime& runtime, Manager& manager) {
    if (m_runtime != nullptr)
        shutdown();

    m_runtime = &runtime;
    m_manager = &manager;
    m_runtime->set_device_command_handler(
        [this](int deviceId, DeviceCommand* command) {
            return handle_device_command(deviceId, command);
        });
    return true;
}

void XInputBackend::shutdown() {
    for (int slot = 0; slot < kXInputSlotCount; ++slot) {
        if (m_slots[static_cast<std::size_t>(slot)].connected)
            set_rumble(slot, 0.0f, 0.0f);
        remove_slot(slot);
    }

    if (m_runtime != nullptr)
        m_runtime->clear_device_command_handler();

    m_runtime = nullptr;
    m_manager = nullptr;
}

void XInputBackend::poll() {
    if (m_runtime == nullptr || m_manager == nullptr)
        return;

    for (int slot = 0; slot < kXInputSlotCount; ++slot) {
        XINPUT_STATE rawState{};
        const DWORD result = XInputGetState(static_cast<DWORD>(slot), &rawState);
        if (result != ERROR_SUCCESS) {
            remove_slot(slot);
            continue;
        }

        const GamepadState state = map_xinput_state(to_pad_state(rawState.Gamepad));
        Slot& current = m_slots[static_cast<std::size_t>(slot)];

        if (!current.connected) {
            DeviceDescription description;
            description.interfaceName = "XInput";
            description.deviceClass = "Gamepad";
            description.manufacturer = "Microsoft";
            description.serial = "XInput slot " + std::to_string(slot);

            XINPUT_CAPABILITIES capabilities{};
            const bool haveCapabilities =
                XInputGetCapabilities(static_cast<DWORD>(slot), 0, &capabilities) ==
                ERROR_SUCCESS;
            if (haveCapabilities) {
                description.product = (capabilities.Flags & XINPUT_CAPS_WIRELESS) != 0
                                          ? "Xbox Wireless Controller"
                                          : "Xbox Controller";
                description.capabilities = capabilities_string(capabilities);
            } else {
                description.product = "Xbox Controller";
            }

            Device* device = m_manager->create_device("Gamepad", description);
            if (device == nullptr)
                continue;

            current.connected = true;
            current.deviceId = device->deviceId;
            current.state = state;
            device->deviceFlags = haveCapabilities ? capabilities.Flags : 0;

            const auto buffer = low::make_state_event(
                kFormatGamepad, &state, sizeof(state), current.deviceId, m_runtime->now());
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
        } else if (std::memcmp(&current.state, &state, sizeof(GamepadState)) != 0) {
            current.state = state;
            const auto buffer = low::make_state_event(
                kFormatGamepad, &state, sizeof(state), current.deviceId, m_runtime->now());
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
        }
    }
}

bool XInputBackend::set_rumble(int slot, float lowFrequency, float highFrequency) {
    if (slot < 0 || slot >= kXInputSlotCount || !m_slots[static_cast<std::size_t>(slot)].connected)
        return false;

    const float low = std::clamp(lowFrequency, 0.0f, 1.0f);
    const float high = std::clamp(highFrequency, 0.0f, 1.0f);
    XINPUT_VIBRATION vibration{};
    vibration.wLeftMotorSpeed = static_cast<WORD>(low * 65535.0f);
    vibration.wRightMotorSpeed = static_cast<WORD>(high * 65535.0f);
    if (XInputSetState(static_cast<DWORD>(slot), &vibration) != ERROR_SUCCESS)
        return false;

    Slot& current = m_slots[static_cast<std::size_t>(slot)];
    current.lowFrequency = low;
    current.highFrequency = high;
    return true;
}

int XInputBackend::device_id(int slot) const {
    if (slot < 0 || slot >= kXInputSlotCount || !m_slots[static_cast<std::size_t>(slot)].connected)
        return 0;
    return m_slots[static_cast<std::size_t>(slot)].deviceId;
}

bool XInputBackend::connected(int slot) const {
    return slot >= 0 && slot < kXInputSlotCount &&
           m_slots[static_cast<std::size_t>(slot)].connected;
}

void XInputBackend::remove_slot(int slot) {
    if (slot < 0 || slot >= kXInputSlotCount)
        return;

    Slot& current = m_slots[static_cast<std::size_t>(slot)];
    if (!current.connected)
        return;

    if (m_manager != nullptr) {
        if (Device* device = m_manager->device_by_id(current.deviceId))
            m_manager->remove_device(device);
    }
    current = Slot{};
}

long XInputBackend::handle_device_command(int deviceId, DeviceCommand* command) {
    if (command == nullptr || command->data == nullptr)
        return -1;

    int slot = -1;
    for (int i = 0; i < kXInputSlotCount; ++i) {
        if (m_slots[static_cast<std::size_t>(i)].connected &&
            m_slots[static_cast<std::size_t>(i)].deviceId == deviceId) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return -1;

    if (command->type == kGetHapticCapabilitiesCommand) {
        auto* request = static_cast<GetHapticCapabilitiesCommand*>(command->data);
        if (request->capabilities == nullptr)
            return -1;
        request->capabilities->supportsLowFrequency = true;
        request->capabilities->supportsHighFrequency = true;
        request->capabilities->supportsLeftTrigger = false;
        request->capabilities->supportsRightTrigger = false;
        request->capabilities->maxBufferSize = 0;
        return 1;
    }

    if (command->type == kSendHapticImpulseCommand) {
        auto* request = static_cast<SendHapticImpulseCommand*>(command->data);
        Slot& current = m_slots[static_cast<std::size_t>(slot)];
        if (request->motorChannel == static_cast<int>(SendHapticImpulseCommand::Motor::LowFrequency))
            current.lowFrequency = std::clamp(request->amplitude, 0.0f, 1.0f);
        else if (request->motorChannel ==
                 static_cast<int>(SendHapticImpulseCommand::Motor::HighFrequency))
            current.highFrequency = std::clamp(request->amplitude, 0.0f, 1.0f);
        else
            return -1;

        return set_rumble(slot, current.lowFrequency, current.highFrequency) ? 1 : -1;
    }

    if (command->type == kSendBufferedHapticsCommand)
        return -1;

    return -1;
}

} // namespace insys::backend

#endif
