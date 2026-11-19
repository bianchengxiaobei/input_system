#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "insys/asset/json.h"
#include "insys/backend/runtime.h"
#include "insys/device_states.h"
#include "insys/low/key.h"
#include "insys/settings.h"

namespace insys {
class ActionAsset;
class Control;
class DebuggerModel;
class Device;
class Manager;
class Keyboard;
class Mouse;
} // namespace insys

namespace insys::backend {
class HidBackend;
} // namespace insys::backend

namespace insys::editor {

struct PreviewRuntimeOptions {
    bool enableHid = true;
    bool enableHidPlatform = true;
    Settings settings{};
    std::size_t maxRecentEvents = 128;
};

struct PreviewMouseInput {
    bool hasX = false;
    bool hasY = false;
    float x = 0.0f;
    float y = 0.0f;
    bool hasButton = false;
    int button = 0;
    bool pressed = false;
    bool hasWheel = false;
    float wheelX = 0.0f;
    float wheelY = 0.0f;
    bool hasDelta = false;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
};

struct PreviewCapturedControl {
    std::string path;
    std::string deviceId;
    std::string controlLayout;
    float value = 0.0f;
    bool isButton = false;
    double timestamp = 0.0;
};

class PreviewRuntime final {
public:
    explicit PreviewRuntime(PreviewRuntimeOptions options = {});
    ~PreviewRuntime();

    PreviewRuntime(const PreviewRuntime&) = delete;
    PreviewRuntime& operator=(const PreviewRuntime&) = delete;

    void start();
    void stop();
    bool running() const { return m_running; }

    std::uint64_t update_asset(std::string_view json,
                               std::uint64_t requestedGeneration = 0);
    std::uint64_t generation() const { return m_generation; }

    bool input_key(std::string_view code, bool pressed, bool repeat);
    bool input_mouse(const PreviewMouseInput& input);
    void ensure_hid();
    void poll_hid();
    void tick();

    asset::JsonValue snapshot();

    void start_capture(std::string expectedControlType, std::string deviceFilter);
    void stop_capture();
    bool capturing() const { return m_captureActive; }
    std::optional<PreviewCapturedControl> take_capture();

    Manager& manager() { return *m_manager; }
    backend::Runtime& runtime() { return m_runtime; }
    ActionAsset* asset() { return m_asset.get(); }
    backend::HidBackend* hid_backend() { return m_hid.get(); }

private:
    struct PlatformHid;

    void start_hid();
    void stop_hid();
    void scan_capture();
    void reset_capture_baseline();
    bool match_device(const Device& device) const;
    bool match_expected(const Control& control) const;
    void set_pending_capture(PreviewCapturedControl capture);
    asset::JsonValue snapshot_json(std::optional<PreviewCapturedControl> captured) const;

    backend::Runtime m_runtime;
    std::unique_ptr<Manager> m_manager;
    std::unique_ptr<ActionAsset> m_asset;
    std::unique_ptr<DebuggerModel> m_debugger;
    std::unique_ptr<backend::HidBackend> m_hid;
    std::unique_ptr<PlatformHid> m_platformHid;
    Keyboard* m_keyboard = nullptr;
    Mouse* m_mouse = nullptr;

    bool m_enableHid = true;
    bool m_enableHidPlatform = true;
    bool m_running = false;
    std::uint64_t m_generation = 0;
    int m_inputListenerId = 0;
    int m_actionListenerId = 0;

    std::array<std::uint8_t, (kKeyCount + 7) / 8> m_keyBits{};
    MouseState m_mouseState{};
    bool m_haveMousePosition = false;
    float m_lastMouseX = 0.0f;
    float m_lastMouseY = 0.0f;

    bool m_captureActive = false;
    std::string m_expectedControlType;
    std::string m_deviceFilter;
    std::optional<PreviewCapturedControl> m_pendingCapture;
    std::array<std::uint8_t, (kKeyCount + 7) / 8> m_captureKeyBits{};
    std::uint8_t m_captureMouseButtons[4]{};
    std::unordered_map<std::string, float> m_captureAxisValues;
    std::unordered_map<std::string, double> m_captureLastPaths;
};

} // namespace insys::editor
