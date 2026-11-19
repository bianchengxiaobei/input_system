#include "insys/backend/glfw_backend.h"

#include <cstring>
#include <iostream>
#include <string>

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include "core/bit_math.h"
#include "glfw_gamepad_map.h"
#include "glfw_key_map.h"
#include "insys/backend/hid_backend.h"
#include "insys/backend/xinput_mapping.h"
#if defined(_WIN32)
#include "insys/backend/windows_hid.h"
#elif defined(__linux__)
#include "insys/backend/linux_hid.h"
#elif defined(__APPLE__)
#include "insys/backend/macos_hid.h"
#endif
#include "insys/low/event.h"
#include "insys/low/state_format.h"

namespace insys::backend {

GlfwBackend* GlfwBackend::s_instance = nullptr;

GlfwBackend::GlfwBackend() = default;

GlfwBackend::~GlfwBackend() {
    shutdown();
}

bool GlfwBackend::initialize(GLFWwindow* window, Runtime& runtime, Manager& manager) {
    if (window == nullptr)
        return false;

    if (m_window != nullptr)
        shutdown();

    m_window = window;
    m_runtime = &runtime;
    m_manager = &manager;

    std::memset(m_keyBits.data(), 0, m_keyBits.size());
    m_mouseState = MouseState{};
    m_haveLastMouse = false;
    m_rawInputEnabled = false;
    m_rawHavePrimary = false;
    m_rawMice.clear();
    m_pendingRawRemovals.clear();
    m_skipHidGamepads = false;

    DeviceDescription keyboardDescription;
    keyboardDescription.interfaceName = "Keyboard";
    keyboardDescription.deviceClass = "Keyboard";
    keyboardDescription.product = "Keyboard";
    m_keyboard = static_cast<Keyboard*>(manager.create_device("Keyboard", keyboardDescription));

    DeviceDescription mouseDescription;
    mouseDescription.interfaceName = "Mouse";
    mouseDescription.deviceClass = "Mouse";
    mouseDescription.product = "Mouse";
    m_mouse = static_cast<Mouse*>(manager.create_device("Mouse", mouseDescription));

    if (m_keyboard == nullptr || m_mouse == nullptr)
        return false;

#if defined(_WIN32)
    if (manager.settings().useRawInput) {
        m_rawInput = std::make_unique<WindowsRawInput>();
        m_rawInput->set_mouse_delta_callback(
            [this](std::intptr_t device, int32_t deltaX, int32_t deltaY) {
                on_raw_mouse_delta(device, deltaX, deltaY);
            });
        m_rawInput->set_device_removed_callback([this](std::intptr_t device) {
            m_pendingRawRemovals.push_back(device);
        });
        if (m_rawInput->initialize(glfwGetWin32Window(window))) {
            m_rawInputEnabled = true;
        } else {
            std::cerr << "[input] failed to initialize Win32 raw input; using GLFW mouse delta\n";
            m_rawInput.reset();
        }
    }

    m_touch = std::make_unique<WindowsTouch>();
    m_touch->set_touch_callback(
        [this](const TouchscreenState& state) { emit_touch_state(state); });
    if (!m_touch->initialize(glfwGetWin32Window(window))) {
        m_touch.reset();
    }
#endif

    if (manager.settings().useHid) {
        m_hidBackend = std::make_unique<HidBackend>();
        if (m_hidBackend->initialize(runtime, manager)) {
            m_hidBackend->set_error_handler([](const HidError& error) {
                std::cerr << "[input] HID error " << static_cast<int>(error.code);
                if (!error.deviceId.empty())
                    std::cerr << " [" << error.deviceId << "]";
                std::cerr << ": " << error.message << '\n';
            });
#if defined(_WIN32)
            m_windowsHid = std::make_unique<WindowsHid>();
            if (m_windowsHid->initialize(*m_hidBackend, runtime)) {
                m_hidBackend->set_output_report_handler(
                    [this](std::string_view id, const uint8_t* data,
                           std::size_t size) {
                        return m_windowsHid != nullptr &&
                               m_windowsHid->write_report(id, data, size);
                    });
                m_hidBackend->set_feature_report_handler(
                    [this](std::string_view id, const uint8_t* data,
                           std::size_t size) {
                        return m_windowsHid != nullptr &&
                               m_windowsHid->write_feature_report(id, data,
                                                                   size);
                    });
                m_hidBackend->set_feature_report_reader(
                    [this](std::string_view id, uint8_t reportId, uint8_t* data,
                           std::size_t size) {
                        return m_windowsHid != nullptr
                                   ? m_windowsHid->read_feature_report(
                                         id, reportId, data, size)
                                   : -1;
                    });
            } else {
                m_windowsHid.reset();
                m_hidBackend->shutdown();
                m_hidBackend.reset();
            }
#elif defined(__linux__)
            m_linuxHid = std::make_unique<LinuxHid>();
            if (m_linuxHid->initialize(*m_hidBackend, runtime)) {
                m_hidBackend->set_output_report_handler(
                    [this](std::string_view id, const uint8_t* data,
                           std::size_t size) {
                        return m_linuxHid != nullptr &&
                               m_linuxHid->write_report(id, data, size);
                    });
                m_hidBackend->set_feature_report_handler(
                    [this](std::string_view id, const uint8_t* data,
                           std::size_t size) {
                        return m_linuxHid != nullptr &&
                               m_linuxHid->write_feature_report(id, data, size);
                    });
                m_hidBackend->set_feature_report_reader(
                    [this](std::string_view id, uint8_t reportId, uint8_t* data,
                           std::size_t size) {
                        return m_linuxHid != nullptr
                                   ? m_linuxHid->read_feature_report(
                                         id, reportId, data, size)
                                   : -1;
                    });
            } else {
                m_linuxHid.reset();
                m_hidBackend->shutdown();
                m_hidBackend.reset();
            }
#elif defined(__APPLE__)
            m_macosHid = std::make_unique<MacOsHid>();
            if (m_macosHid->initialize(*m_hidBackend, runtime)) {
                m_hidBackend->set_output_report_handler(
                    [this](std::string_view id, const uint8_t* data,
                           std::size_t size) {
                        return m_macosHid != nullptr &&
                               m_macosHid->write_report(id, data, size);
                    });
                m_hidBackend->set_feature_report_handler(
                    [this](std::string_view id, const uint8_t* data,
                           std::size_t size) {
                        return m_macosHid != nullptr &&
                               m_macosHid->write_feature_report(id, data, size);
                    });
                m_hidBackend->set_feature_report_reader(
                    [this](std::string_view id, uint8_t reportId, uint8_t* data,
                           std::size_t size) {
                        return m_macosHid != nullptr
                                   ? m_macosHid->read_feature_report(
                                         id, reportId, data, size)
                                   : -1;
                    });
            } else {
                m_macosHid.reset();
                m_hidBackend->shutdown();
                m_hidBackend.reset();
            }
#else
            m_hidBackend->shutdown();
            m_hidBackend.reset();
            std::cerr << "[input] HID backend is only wired on desktop platforms\n";
#endif
        } else {
            m_hidBackend.reset();
        }
    }

    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCharCallback(window, char_callback);
    glfwSetCursorPosCallback(window, cursor_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetWindowFocusCallback(window, focus_callback);

    s_instance = this;
    glfwSetJoystickCallback(joystick_callback);

    return true;
}

void GlfwBackend::shutdown() {
#if defined(_WIN32)
    if (m_touch) {
        m_touch->shutdown();
        m_touch.reset();
    }
    if (m_windowsHid) {
        m_windowsHid->shutdown();
        m_windowsHid.reset();
    }
#elif defined(__linux__)
    if (m_linuxHid) {
        m_linuxHid->shutdown();
        m_linuxHid.reset();
    }
#elif defined(__APPLE__)
    if (m_macosHid) {
        m_macosHid->shutdown();
        m_macosHid.reset();
    }
#endif
    if (m_hidBackend) {
        m_hidBackend->shutdown();
        m_hidBackend.reset();
    }
    m_skipHidGamepads = false;
#if defined(_WIN32)
    if (m_rawInput) {
        m_rawInput->shutdown();
        m_rawInput.reset();
    }
#endif
    if (m_manager != nullptr) {
        for (const auto& [device, slot] : m_rawMice) {
            (void)device;
            if (!slot.primary)
                m_manager->remove_device(m_manager->device_by_id(slot.deviceId));
        }
    }
    m_rawInputEnabled = false;
    m_rawHavePrimary = false;
    m_rawMice.clear();
    m_pendingRawRemovals.clear();

    if (m_window != nullptr) {
        glfwSetKeyCallback(m_window, nullptr);
        glfwSetCharCallback(m_window, nullptr);
        glfwSetCursorPosCallback(m_window, nullptr);
        glfwSetMouseButtonCallback(m_window, nullptr);
        glfwSetScrollCallback(m_window, nullptr);
        glfwSetWindowFocusCallback(m_window, nullptr);
        glfwSetWindowUserPointer(m_window, nullptr);
    }

    if (s_instance == this) {
        glfwSetJoystickCallback(nullptr);
        s_instance = nullptr;
    }

    if (m_manager != nullptr) {
        for (auto& [joystickId, slot] : m_gamepads) {
            (void)joystickId;
            m_manager->remove_device(m_manager->device_by_id(slot.deviceId));
        }
        if (m_keyboard != nullptr)
            m_manager->remove_device(m_keyboard);
        if (m_mouse != nullptr)
            m_manager->remove_device(m_mouse);
        if (m_touchscreen != nullptr)
            m_manager->remove_device(m_touchscreen);
    }

    m_gamepads.clear();
    m_pendingGamepadDisconnects.clear();
    m_keyboard = nullptr;
    m_mouse = nullptr;
    m_touchscreen = nullptr;
    m_window = nullptr;
    m_manager = nullptr;
    m_runtime = nullptr;
}

void GlfwBackend::poll() {
    flush_raw_mouse();
#if defined(_WIN32)
    if (m_windowsHid) {
        m_windowsHid->poll();
        m_skipHidGamepads = m_windowsHid->device_count() != 0;
    }
#elif defined(__linux__)
    if (m_linuxHid) {
        m_linuxHid->poll();
        m_skipHidGamepads = m_linuxHid->device_count() != 0;
    }
#elif defined(__APPLE__)
    if (m_macosHid) {
        m_macosHid->poll();
        m_skipHidGamepads = m_macosHid->device_count() != 0;
    }
#endif
    if (m_hidBackend)
        m_hidBackend->poll();
    poll_gamepads();
}

GlfwBackend* GlfwBackend::from_window(GLFWwindow* window) {
    return static_cast<GlfwBackend*>(glfwGetWindowUserPointer(window));
}

void GlfwBackend::key_callback(GLFWwindow* window, int key, int, int action, int) {
    if (GlfwBackend* backend = from_window(window))
        backend->on_key(key, action);
}

void GlfwBackend::char_callback(GLFWwindow* window, unsigned int codepoint) {
    if (GlfwBackend* backend = from_window(window))
        backend->on_char(codepoint);
}

void GlfwBackend::cursor_callback(GLFWwindow* window, double x, double y) {
    if (GlfwBackend* backend = from_window(window))
        backend->on_cursor(x, y);
}

void GlfwBackend::mouse_button_callback(GLFWwindow* window, int button, int action, int) {
    if (GlfwBackend* backend = from_window(window))
        backend->on_mouse_button(button, action);
}

void GlfwBackend::scroll_callback(GLFWwindow* window, double xOffset, double yOffset) {
    if (GlfwBackend* backend = from_window(window))
        backend->on_scroll(xOffset, yOffset);
}

void GlfwBackend::focus_callback(GLFWwindow* window, int focused) {
    if (GlfwBackend* backend = from_window(window))
        backend->on_focus(focused);
}

void GlfwBackend::joystick_callback(int joystickId, int event) {
    if (s_instance == nullptr)
        return;
    if (event == GLFW_DISCONNECTED)
        s_instance->m_pendingGamepadDisconnects.push_back(joystickId);
}

void GlfwBackend::on_key(int key, int action) {
    Key mapped{};
    if (!map_glfw_key(key, mapped))
        return;

    const bool down = (action != GLFW_RELEASE);
    detail::write_single_bit(m_keyBits.data(), static_cast<uint32_t>(mapped), down);

    if (m_logEvents)
        std::cout << "[key] " << key_name(mapped) << (down ? " down" : " up") << '\n';

    emit_keyboard_state();
}

void GlfwBackend::on_char(unsigned int codepoint) {
    if (codepoint == 0)
        return;

    if (m_logEvents)
        std::cout << "[char] U+" << std::hex << codepoint << std::dec << '\n';

    queue_text(codepoint);

    if (m_textListener)
        m_textListener(static_cast<char32_t>(codepoint));
}

void GlfwBackend::on_cursor(double x, double y) {
    m_mouseX = static_cast<float>(x);
    m_mouseY = static_cast<float>(y);

    if (!m_rawInputEnabled) {
        if (m_haveLastMouse) {
            m_mouseState.delta.x = m_mouseX - m_lastMouseX;
            m_mouseState.delta.y = m_mouseY - m_lastMouseY;
        } else {
            m_mouseState.delta = {};
            m_haveLastMouse = true;
        }
    } else {
        m_haveLastMouse = true;
    }

    m_lastMouseX = m_mouseX;
    m_lastMouseY = m_mouseY;

    m_mouseState.position = {m_mouseX, m_mouseY};

    emit_mouse_state();
}

void GlfwBackend::on_mouse_button(int button, int action) {
    int index = -1;
    switch (button) {
    case GLFW_MOUSE_BUTTON_LEFT: index = 0; break;
    case GLFW_MOUSE_BUTTON_RIGHT: index = 1; break;
    case GLFW_MOUSE_BUTTON_MIDDLE: index = 2; break;
    default: return;
    }

    const bool down = (action != GLFW_RELEASE);
    detail::write_single_bit(m_mouseState.buttons, static_cast<uint32_t>(index), down);

    if (m_logEvents) {
        static const char* names[] = {"left", "right", "middle"};
        std::cout << "[mouse] " << names[index] << (down ? " down" : " up") << '\n';
    }

    emit_mouse_state();
}

void GlfwBackend::on_scroll(double xOffset, double yOffset) {
    m_mouseState.scroll.x += static_cast<float>(xOffset);
    m_mouseState.scroll.y += static_cast<float>(yOffset);

    if (m_logEvents)
        std::cout << "[mouse] scroll " << xOffset << ", " << yOffset << '\n';

    emit_mouse_state();

    m_mouseState.scroll = {};
}

void GlfwBackend::on_focus(int focused) {
    if (m_runtime != nullptr)
        m_runtime->set_focus_state(focused ? FocusState::Normal : FocusState::Unfocused);
}

void GlfwBackend::poll_gamepads() {
    if (m_manager == nullptr || m_runtime == nullptr)
        return;

    for (int joystickId : m_pendingGamepadDisconnects) {
        const auto it = m_gamepads.find(joystickId);
        if (it == m_gamepads.end())
            continue;
        m_manager->remove_device(m_manager->device_by_id(it->second.deviceId));
        m_gamepads.erase(it);
    }
    m_pendingGamepadDisconnects.clear();

    if (m_skipHidGamepads) {
        for (auto& [joystickId, slot] : m_gamepads) {
            (void)joystickId;
            m_manager->remove_device(m_manager->device_by_id(slot.deviceId));
        }
        m_gamepads.clear();
        return;
    }

    for (int joystickId = GLFW_JOYSTICK_1; joystickId <= GLFW_JOYSTICK_LAST; ++joystickId) {
        if (!glfwJoystickIsGamepad(joystickId)) {
            const auto it = m_gamepads.find(joystickId);
            if (it != m_gamepads.end()) {
                m_manager->remove_device(m_manager->device_by_id(it->second.deviceId));
                m_gamepads.erase(it);
            }
            continue;
        }

        const char* gamepadName = glfwGetGamepadName(joystickId);
        if (m_skipXInputGamepads && gamepadName != nullptr &&
            is_xinput_gamepad_name(gamepadName)) {
            const auto existing = m_gamepads.find(joystickId);
            if (existing != m_gamepads.end()) {
                m_manager->remove_device(m_manager->device_by_id(existing->second.deviceId));
                m_gamepads.erase(existing);
            }
            continue;
        }

        GLFWgamepadstate rawState{};
        if (!glfwGetGamepadState(joystickId, &rawState))
            continue;

        const GamepadState state = map_gamepad_state(rawState);
        const auto it = m_gamepads.find(joystickId);
        if (it == m_gamepads.end()) {
            DeviceDescription description;
            description.interfaceName = "Gamepad";
            description.deviceClass = "Gamepad";
            description.manufacturer = "GLFW";
            const char* name = glfwGetGamepadName(joystickId);
            description.product = name != nullptr ? name : "Gamepad";

            Device* device = m_manager->create_device("Gamepad", description);
            if (device == nullptr)
                continue;

            m_gamepads.emplace(joystickId, GamepadSlot{device->deviceId, state});
            emit_gamepad_state(device->deviceId, state);

            if (m_logEvents)
                std::cout << "[gamepad] connected: " << description.product
                          << " (device " << device->deviceId << ")\n";
        } else if (std::memcmp(&it->second.state, &state, sizeof(GamepadState)) != 0) {
            emit_gamepad_state(it->second.deviceId, state);
            it->second.state = state;
        }
    }
}

void GlfwBackend::on_raw_mouse_delta(std::intptr_t device, int32_t deltaX, int32_t deltaY) {
    if (!m_rawInputEnabled || m_manager == nullptr || m_mouse == nullptr)
        return;

    auto it = m_rawMice.find(device);
    if (it == m_rawMice.end()) {
        RawMouseSlot slot;
        if (!m_rawHavePrimary) {
            m_rawHavePrimary = true;
            slot.primary = true;
            slot.deviceId = m_mouse->deviceId;
        } else {
            DeviceDescription description;
            description.interfaceName = "RawInput";
            description.deviceClass = "Mouse";
            description.manufacturer = "Raw Input";
            description.product = m_rawInput ? m_rawInput->device_name(device) : "Raw Mouse";
            Device* created = m_manager->create_device("Mouse", description);
            if (created == nullptr)
                return;
            slot.deviceId = created->deviceId;
        }
        it = m_rawMice.emplace(device, slot).first;
    }

    RawMouseSlot& slot = it->second;
    slot.dirty = true;
    if (slot.primary) {
        m_mouseState.delta.x += static_cast<float>(deltaX);
        m_mouseState.delta.y += static_cast<float>(deltaY);
    } else {
        slot.state.delta.x += static_cast<float>(deltaX);
        slot.state.delta.y += static_cast<float>(deltaY);
    }

}

void GlfwBackend::emit_touch_state(const TouchscreenState& state) {
    if (m_manager == nullptr || m_runtime == nullptr)
        return;

    if (m_touchscreen == nullptr) {
        m_touchscreen =
            static_cast<Touchscreen*>(m_manager->device_by_layout("Touchscreen"));
        if (m_touchscreen == nullptr) {
            DeviceDescription description;
            description.interfaceName = "Touch";
            description.deviceClass = "Touchscreen";
            description.product = "Touchscreen";
            m_touchscreen =
                static_cast<Touchscreen*>(m_manager->create_device("Touchscreen", description));
        }
    }
    if (m_touchscreen == nullptr)
        return;

    const auto event = low::make_state_event(
        kFormatTouchscreen, &state, sizeof(state), m_touchscreen->deviceId,
        m_runtime->now());
    m_runtime->queue_event(reinterpret_cast<const low::Event*>(event.data()));
}

void GlfwBackend::flush_raw_mouse() {
    if (!m_pendingRawRemovals.empty()) {
        for (std::intptr_t device : m_pendingRawRemovals) {
            const auto it = m_rawMice.find(device);
            if (it == m_rawMice.end())
                continue;

            if (it->second.primary) {
                m_rawHavePrimary = false;
            } else if (m_manager != nullptr) {
                if (Device* removed = m_manager->device_by_id(it->second.deviceId))
                    m_manager->remove_device(removed);
            }
            m_rawMice.erase(it);
        }
        m_pendingRawRemovals.clear();
    }

    if (!m_rawInputEnabled)
        return;

    for (auto& [device, slot] : m_rawMice) {
        (void)device;
        if (!slot.dirty)
            continue;

        if (slot.primary) {
            emit_mouse_state();
            m_mouseState.delta = {};
        } else if (m_manager != nullptr && m_runtime != nullptr) {
            const auto buffer = low::make_state_event(
                kFormatMouse, &slot.state, static_cast<uint32_t>(sizeof(MouseState)),
                slot.deviceId, m_runtime->now());
            m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
            slot.state.delta = {};
        }
        slot.dirty = false;
    }
}

void GlfwBackend::emit_keyboard_state() {
    if (m_manager == nullptr || m_keyboard == nullptr || m_runtime == nullptr)
        return;

    const auto buffer = low::make_state_event(kFormatKeyboard, m_keyBits.data(),
                                             static_cast<uint32_t>(m_keyBits.size()),
                                             m_keyboard->deviceId, m_runtime->now());
    m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
}

void GlfwBackend::emit_mouse_state() {
    if (m_manager == nullptr || m_mouse == nullptr || m_runtime == nullptr)
        return;

    const auto buffer = low::make_state_event(kFormatMouse, &m_mouseState,
                                             static_cast<uint32_t>(sizeof(MouseState)),
                                             m_mouse->deviceId, m_runtime->now());
    m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
}

void GlfwBackend::emit_gamepad_state(int deviceId, const GamepadState& state) {
    if (m_manager == nullptr || m_runtime == nullptr)
        return;

    const auto buffer = low::make_state_event(kFormatGamepad, &state,
                                             static_cast<uint32_t>(sizeof(GamepadState)),
                                             deviceId, m_runtime->now());
    m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
}

void GlfwBackend::queue_text(uint32_t codepoint) {
    if (m_manager == nullptr || m_runtime == nullptr)
        return;

    const int deviceId = m_keyboard != nullptr ? m_keyboard->deviceId : 0;
    const auto buffer = low::make_text_event(static_cast<int32_t>(codepoint), deviceId,
                                             m_runtime->now());
    m_manager->queue_event(reinterpret_cast<const low::Event*>(buffer.data()));
}

} // namespace insys::backend






