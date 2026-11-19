#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "insys/backend/runtime.h"
#include "insys/device.h"
#include "insys/device_states.h"
#include "insys/manager.h"

#if defined(_WIN32)
#include "insys/backend/windows_raw_input.h"
#include "insys/backend/windows_touch.h"
#endif

struct GLFWwindow;

namespace insys::backend {

class HidBackend;
#if defined(_WIN32)
class WindowsHid;
#elif defined(__linux__)
class LinuxHid;
#elif defined(__APPLE__)
class MacOsHid;
#endif

class GlfwBackend {
public:
    GlfwBackend();
    ~GlfwBackend();

    GlfwBackend(const GlfwBackend&) = delete;
    GlfwBackend& operator=(const GlfwBackend&) = delete;

    bool initialize(GLFWwindow* window, Runtime& runtime, Manager& manager);
    void shutdown();
    void poll();

    Keyboard* keyboard() const { return m_keyboard; }
    Mouse* mouse() const { return m_mouse; }

    void set_log_events(bool value) { m_logEvents = value; }
    void set_skip_xinput_gamepads(bool value) { m_skipXInputGamepads = value; }
    bool raw_input_enabled() const { return m_rawInputEnabled; }
    bool hid_enabled() const { return m_hidBackend != nullptr; }
    void set_text_listener(std::function<void(char32_t)> listener) {
        m_textListener = std::move(listener);
    }

private:
    static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void char_callback(GLFWwindow* window, unsigned int codepoint);
    static void cursor_callback(GLFWwindow* window, double x, double y);
    static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
    static void scroll_callback(GLFWwindow* window, double xOffset, double yOffset);
    static void focus_callback(GLFWwindow* window, int focused);
    static void joystick_callback(int joystickId, int event);
    static GlfwBackend* from_window(GLFWwindow* window);

    void on_key(int key, int action);
    void on_char(unsigned int codepoint);
    void on_cursor(double x, double y);
    void on_mouse_button(int button, int action);
    void on_scroll(double xOffset, double yOffset);
    void on_focus(int focused);
    void poll_gamepads();
    void on_raw_mouse_delta(std::intptr_t device, int32_t deltaX, int32_t deltaY);
    void flush_raw_mouse();
    void emit_touch_state(const TouchscreenState& state);

    void emit_keyboard_state();
    void emit_mouse_state();
    void emit_gamepad_state(int deviceId, const GamepadState& state);
    void queue_text(uint32_t codepoint);

    GLFWwindow* m_window = nullptr;
    Runtime* m_runtime = nullptr;
    Manager* m_manager = nullptr;
    Keyboard* m_keyboard = nullptr;
    Mouse* m_mouse = nullptr;
    Touchscreen* m_touchscreen = nullptr;

    struct GamepadSlot {
        int deviceId = 0;
        GamepadState state{};
    };

    std::array<uint8_t, (kKeyCount + 7) / 8> m_keyBits{};
    MouseState m_mouseState{};
    std::unordered_map<int, GamepadSlot> m_gamepads;
    std::vector<int> m_pendingGamepadDisconnects;

    static GlfwBackend* s_instance;

    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    float m_lastMouseX = 0.0f;
    float m_lastMouseY = 0.0f;
    bool m_haveLastMouse = false;

    bool m_logEvents = false;
    bool m_skipXInputGamepads = false;
    std::function<void(char32_t)> m_textListener;

    struct RawMouseSlot {
        int deviceId = 0;
        bool primary = false;
        bool dirty = false;
        MouseState state{};
    };

    bool m_rawInputEnabled = false;
    bool m_rawHavePrimary = false;
    std::unordered_map<std::intptr_t, RawMouseSlot> m_rawMice;
    std::vector<std::intptr_t> m_pendingRawRemovals;
    std::unique_ptr<HidBackend> m_hidBackend;
    bool m_skipHidGamepads = false;
#if defined(_WIN32)
    std::unique_ptr<WindowsRawInput> m_rawInput;
    std::unique_ptr<WindowsTouch> m_touch;
    std::unique_ptr<WindowsHid> m_windowsHid;
#elif defined(__linux__)
    std::unique_ptr<LinuxHid> m_linuxHid;
#elif defined(__APPLE__)
    std::unique_ptr<MacOsHid> m_macosHid;
#endif
};

} // namespace insys::backend




