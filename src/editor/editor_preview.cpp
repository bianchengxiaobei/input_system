#include "insys/editor/editor_preview.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "insys/actions/action_asset.h"
#include "insys/asset/asset_loader.h"
#include "insys/backend/hid_backend.h"
#include "insys/control.h"
#include "insys/control_path.h"
#include "insys/debugger_model.h"
#include "insys/device.h"
#include "insys/low/state_format.h"
#include "insys/manager.h"
#include "insys/string_utils.h"

#if defined(_WIN32)
#include "insys/backend/windows_hid.h"
#elif defined(__linux__)
#include "insys/backend/linux_hid.h"
#elif defined(__APPLE__) && !defined(INSYS_IOS)
#include "insys/backend/macos_hid.h"
#endif

namespace insys::editor {
namespace {

using JsonValue = asset::JsonValue;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char character) { return !std::isspace(character); };
    const auto begin = std::find_if(value.begin(), value.end(), notSpace);
    const auto end = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    if (begin >= end)
        return {};
    return std::string(begin, end);
}

std::string normalize_layout(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && value.front() == '<' && value.back() == '>')
        value = value.substr(1, value.size() - 2);
    return lower(std::move(value));
}

JsonValue make_object() {
    JsonValue value;
    value.type = JsonValue::Type::Object;
    return value;
}

JsonValue make_array() {
    JsonValue value;
    value.type = JsonValue::Type::Array;
    return value;
}

JsonValue make_string(std::string value) {
    JsonValue result;
    result.type = JsonValue::Type::String;
    result.string = std::move(value);
    return result;
}

JsonValue make_number(double value) {
    JsonValue result;
    result.type = JsonValue::Type::Number;
    result.number = value;
    return result;
}

JsonValue make_bool(bool value) {
    JsonValue result;
    result.type = JsonValue::Type::Bool;
    result.boolean = value;
    return result;
}

JsonValue make_null() {
    return JsonValue{};
}

void add(JsonValue& object, std::string key, JsonValue value) {
    object.object.emplace_back(std::move(key), std::move(value));
}

void write_bit(std::uint8_t* bits, std::uint32_t index, bool value) {
    const std::uint32_t byte = index / 8;
    const std::uint8_t mask = static_cast<std::uint8_t>(1u << (index % 8));
    if (value)
        bits[byte] |= mask;
    else
        bits[byte] &= static_cast<std::uint8_t>(~mask);
}

bool read_bit(const std::uint8_t* bits, std::uint32_t index) {
    return (bits[index / 8] & static_cast<std::uint8_t>(1u << (index % 8))) != 0;
}

std::optional<Key> key_from_code(std::string_view code) {
    std::string value = lower(std::string(code));
    if (value.empty())
        return std::nullopt;

    if (value.size() == 4 && value.rfind("key", 0) == 0 &&
        value[3] >= 'a' && value[3] <= 'z') {
        return static_cast<Key>(static_cast<int>(Key::A) + (value[3] - 'a'));
    }
    if (value.size() == 6 && value.rfind("digit", 0) == 0 &&
        value[5] >= '0' && value[5] <= '9') {
        return static_cast<Key>(static_cast<int>(Key::Digit0) + (value[5] - '0'));
    }
    if (value.size() >= 2 && value[0] == 'f' && value.size() <= 3 &&
        std::isdigit(static_cast<unsigned char>(value[1]))) {
        const int number = std::stoi(value.substr(1));
        if (number >= 1 && number <= 12)
            return static_cast<Key>(static_cast<int>(Key::F1) + number - 1);
    }
    if (value.rfind("numpad", 0) == 0 && value.size() > 6) {
        const std::string suffix = value.substr(6);
        if (suffix.size() == 1 && suffix[0] >= '0' && suffix[0] <= '9')
            return static_cast<Key>(static_cast<int>(Key::Numpad0) + (suffix[0] - '0'));
        if (suffix == "add" || suffix == "plus") return Key::NumpadAdd;
        if (suffix == "subtract" || suffix == "minus") return Key::NumpadSubtract;
        if (suffix == "multiply") return Key::NumpadMultiply;
        if (suffix == "divide") return Key::NumpadDivide;
        if (suffix == "decimal" || suffix == "period") return Key::NumpadDecimal;
        if (suffix == "enter") return Key::NumpadEnter;
    }

    if (value == "space" || value == " ") return Key::Space;
    if (value == "enter" || value == "return") return Key::Enter;
    if (value == "escape" || value == "esc") return Key::Escape;
    if (value == "tab") return Key::Tab;
    if (value == "backspace") return Key::Backspace;
    if (value == "delete" || value == "del") return Key::Delete;
    if (value == "insert" || value == "ins") return Key::Insert;
    if (value == "home") return Key::Home;
    if (value == "end") return Key::End;
    if (value == "pageup") return Key::PageUp;
    if (value == "pagedown") return Key::PageDown;
    if (value == "arrowup" || value == "uparrow" || value == "up") return Key::ArrowUp;
    if (value == "arrowdown" || value == "downarrow" || value == "down") return Key::ArrowDown;
    if (value == "arrowleft" || value == "leftarrow" || value == "left") return Key::ArrowLeft;
    if (value == "arrowright" || value == "rightarrow" || value == "right") return Key::ArrowRight;

    if (value == "shiftleft" || value == "leftshift") return Key::LeftShift;
    if (value == "shiftright" || value == "rightshift") return Key::RightShift;
    if (value == "controlleft" || value == "leftctrl" || value == "leftcontrol")
        return Key::LeftCtrl;
    if (value == "controlright" || value == "rightctrl" || value == "rightcontrol")
        return Key::RightCtrl;
    if (value == "altleft" || value == "leftalt") return Key::LeftAlt;
    if (value == "altright" || value == "rightalt") return Key::RightAlt;
    if (value == "metaleft" || value == "leftmeta" || value == "leftsuper")
        return Key::LeftSuper;
    if (value == "metaright" || value == "rightmeta" || value == "rightsuper")
        return Key::RightSuper;

    if (value == "minus") return Key::Minus;
    if (value == "equal" || value == "equals") return Key::Equal;
    if (value == "bracketleft" || value == "leftbracket") return Key::LeftBracket;
    if (value == "bracketright" || value == "rightbracket") return Key::RightBracket;
    if (value == "backslash") return Key::Backslash;
    if (value == "semicolon") return Key::Semicolon;
    if (value == "quote" || value == "apostrophe") return Key::Apostrophe;
    if (value == "comma") return Key::Comma;
    if (value == "period") return Key::Period;
    if (value == "slash") return Key::Slash;
    if (value == "backquote" || value == "grave") return Key::Grave;
    if (value == "capslock") return Key::CapsLock;
    if (value == "numlock") return Key::NumLock;
    if (value == "scrolllock") return Key::ScrollLock;

    return std::nullopt;
}

bool expected_keyboard_button(const std::string& expected) {
    return expected.empty() || expected == "any" || expected == "button" ||
           expected == "key";
}

bool expected_mouse_axis(const std::string& expected) {
    return expected == "axis" || expected == "vector2" || expected == "delta" ||
           expected == "float";
}

bool control_matches_expected(const Control& control, const std::string& expected) {
    if (expected.empty() || expected == "any")
        return true;
    if (expected == "button")
        return control.is_button();
    if (expected == "axis" || expected == "float" || expected == "double" ||
        expected == "integer")
        return control.is_axis() && !control.is_button();
    if (expected == "vector2")
        return dynamic_cast<const Vector2Control*>(&control) != nullptr;
    if (expected == "vector3")
        return dynamic_cast<const Vector3Control*>(&control) != nullptr;
    if (expected == "stick")
        return dynamic_cast<const Vector2Control*>(&control) != nullptr ||
               lower(control.layout.str()) == "stick";
    if (expected == "dpad")
        return dynamic_cast<const DpadControl*>(&control) != nullptr ||
               lower(control.layout.str()) == "dpad";
    return equals_ignore_case(control.layout.str(), expected) ||
           equals_ignore_case(control.name.str(), expected);
}

std::string mouse_button_name(int button) {
    switch (button) {
    case 0: return "leftButton";
    case 1: return "rightButton";
    case 2: return "middleButton";
    case 3: return "forwardButton";
    case 4: return "backButton";
    default: return {};
    }
}

std::string control_device_key(const Device& device, std::string_view path) {
    return std::to_string(device.deviceId) + "|" + std::string(path);
}

} // namespace

struct PreviewRuntime::PlatformHid {
#if defined(_WIN32)
    backend::WindowsHid backend;
#elif defined(__linux__)
    backend::LinuxHid backend;
#elif defined(__APPLE__) && !defined(INSYS_IOS)
    backend::MacOsHid backend;
#endif

    bool initialize(backend::HidBackend& hid, backend::Runtime& runtime) {
#if defined(_WIN32) || defined(__linux__) || (defined(__APPLE__) && !defined(INSYS_IOS))
        return backend.initialize(hid, runtime);
#else
        (void)hid;
        (void)runtime;
        return false;
#endif
    }

    void shutdown() {
#if defined(_WIN32) || defined(__linux__) || (defined(__APPLE__) && !defined(INSYS_IOS))
        backend.shutdown();
#endif
    }

    void poll() {
#if defined(_WIN32) || defined(__linux__) || (defined(__APPLE__) && !defined(INSYS_IOS))
        backend.poll();
#endif
    }
};

PreviewRuntime::PreviewRuntime(PreviewRuntimeOptions options)
    : m_manager(std::make_unique<Manager>(m_runtime, options.settings)),
      m_debugger(std::make_unique<DebuggerModel>(
          DebuggerModel::Options{options.maxRecentEvents, 128, true})) {
    m_runtime.set_manager(m_manager.get());
    m_enableHid = options.enableHid;
    m_enableHidPlatform = options.enableHidPlatform;

    DeviceDescription keyboardDescription;
    keyboardDescription.interfaceName = "Keyboard";
    keyboardDescription.deviceClass = "Keyboard";
    keyboardDescription.product = "Keyboard";
    m_keyboard = static_cast<Keyboard*>(
        m_manager->create_device("Keyboard", keyboardDescription));

    DeviceDescription mouseDescription;
    mouseDescription.interfaceName = "Mouse";
    mouseDescription.deviceClass = "Mouse";
    mouseDescription.product = "Mouse";
    m_mouse = static_cast<Mouse*>(m_manager->create_device("Mouse", mouseDescription));

    m_inputListenerId = m_manager->add_event_listener(
        [this](low::Event& event, Device* device) {
            m_debugger->record_event(event, device);
        });
    m_actionListenerId = m_manager->add_action_change_listener(
        [this](ActionDef* action, ActionChange change) {
            m_debugger->record_action_change(action, change);
        });
}

PreviewRuntime::~PreviewRuntime() {
    stop();
    if (m_manager) {
        if (m_inputListenerId != 0)
            m_manager->remove_event_listener(m_inputListenerId);
        if (m_actionListenerId != 0)
            m_manager->remove_action_change_listener(m_actionListenerId);
    }
}

void PreviewRuntime::start() {
    if (m_running)
        return;
    m_running = true;
    ensure_hid();
    if (m_generation == 0)
        m_generation = 1;
}

void PreviewRuntime::ensure_hid() {
    start_hid();
}

void PreviewRuntime::start_hid() {
    if (!m_enableHid || m_hid != nullptr || m_manager == nullptr)
        return;
    m_hid = std::make_unique<backend::HidBackend>();
    if (m_hid->initialize(m_runtime, *m_manager)) {
        if (m_enableHidPlatform) {
            m_platformHid = std::make_unique<PlatformHid>();
            if (!m_platformHid->initialize(*m_hid, m_runtime))
                m_platformHid.reset();
        }
    }
}

void PreviewRuntime::poll_hid() {
    if (!m_manager)
        return;
    if (m_platformHid)
        m_platformHid->poll();
    if (m_hid)
        m_hid->poll();
    m_manager->update(UpdateType::Dynamic);
}

void PreviewRuntime::stop_hid() {
    stop_capture();
    if (m_platformHid) {
        m_platformHid->shutdown();
        m_platformHid.reset();
    }
    if (m_hid) {
        m_hid->shutdown();
        m_hid.reset();
    }
}

void PreviewRuntime::stop() {
    stop_hid();
    if (m_asset) {
        m_asset->disable();
        m_asset.reset();
    }
    if (m_debugger)
        m_debugger->clear_assets();
    m_running = false;
    m_generation = 0;
}

std::uint64_t PreviewRuntime::update_asset(std::string_view json,
                                           std::uint64_t requestedGeneration) {
    if (!m_running)
        start();

    std::unique_ptr<ActionAsset> next =
        std::make_unique<ActionAsset>(asset::AssetLoader::load_from_json(json));

    if (m_asset)
        m_asset->disable();
    next->enable(*m_manager);
    m_asset = std::move(next);

    m_debugger->clear_assets();
    m_debugger->add_asset(*m_asset);
    m_debugger->refresh(*m_manager, true);

    if (requestedGeneration > m_generation)
        m_generation = requestedGeneration;
    ++m_generation;
    return m_generation;
}

bool PreviewRuntime::input_key(std::string_view code, bool pressed, bool repeat) {
    if (!m_running || m_keyboard == nullptr)
        return false;
    const std::optional<Key> key = key_from_code(code);
    if (!key.has_value())
        return false;
    (void)repeat;
    write_bit(m_keyBits.data(), static_cast<std::uint32_t>(*key), pressed);

    const auto event = low::make_state_event(
        kFormatKeyboard, m_keyBits.data(), static_cast<std::uint32_t>(m_keyBits.size()),
        m_keyboard->deviceId, m_runtime.now());
    m_manager->queue_event(reinterpret_cast<const low::Event*>(event.data()));
    tick();
    return true;
}

bool PreviewRuntime::input_mouse(const PreviewMouseInput& input) {
    if (!m_running || m_mouse == nullptr)
        return false;

    bool hasChange = false;
    if (input.hasX || input.hasY) {
        const float nextX = input.hasX ? input.x : m_mouseState.position.x;
        const float nextY = input.hasY ? input.y : m_mouseState.position.y;
        if (m_haveMousePosition) {
            m_mouseState.delta.x += nextX - m_lastMouseX;
            m_mouseState.delta.y += nextY - m_lastMouseY;
        }
        m_lastMouseX = nextX;
        m_lastMouseY = nextY;
        m_mouseState.position = {nextX, nextY};
        m_haveMousePosition = true;
        hasChange = true;
    }
    if (input.hasDelta) {
        m_mouseState.delta.x += input.deltaX;
        m_mouseState.delta.y += input.deltaY;
        hasChange = true;
    }
    if (input.hasWheel) {
        m_mouseState.scroll.x += input.wheelX;
        m_mouseState.scroll.y += input.wheelY;
        hasChange = true;
    }
    if (input.hasButton) {
        const std::string name = mouse_button_name(input.button);
        if (name.empty())
            return false;
        write_bit(m_mouseState.buttons, static_cast<std::uint32_t>(input.button),
                  input.pressed);
        hasChange = true;
    }
    if (!hasChange)
        return true;

    const auto event = low::make_state_event(
        kFormatMouse, &m_mouseState, static_cast<std::uint32_t>(sizeof(m_mouseState)),
        m_mouse->deviceId, m_runtime.now());
    m_manager->queue_event(reinterpret_cast<const low::Event*>(event.data()));
    tick();

    m_mouseState.scroll = {};
    m_mouseState.delta = {};
    return true;
}

void PreviewRuntime::tick() {
    if (!m_running || m_manager == nullptr)
        return;
    poll_hid();
    scan_capture();
}

void PreviewRuntime::start_capture(std::string expectedControlType,
                                   std::string deviceFilter) {
    m_captureActive = true;
    m_expectedControlType = lower(trim(std::move(expectedControlType)));
    m_deviceFilter = normalize_layout(std::move(deviceFilter));
    m_pendingCapture.reset();
    m_captureLastPaths.clear();
    reset_capture_baseline();
}

void PreviewRuntime::stop_capture() {
    m_captureActive = false;
    m_pendingCapture.reset();
    m_expectedControlType.clear();
    m_deviceFilter.clear();
    m_captureAxisValues.clear();
    m_captureLastPaths.clear();
}

void PreviewRuntime::reset_capture_baseline() {
    m_captureKeyBits = m_keyBits;
    std::memcpy(m_captureMouseButtons, m_mouseState.buttons,
                sizeof(m_captureMouseButtons));
    m_captureAxisValues.clear();

    for (Device* device : m_manager->devices()) {
        if (device == nullptr || device == m_keyboard || device == m_mouse)
            continue;
        const std::uint8_t* state = device->current_state();
        for (Control* control : device->allControls) {
            if (control == nullptr || control == device || state == nullptr)
                continue;
            if ((control->flags & (kControlIsNoisy | kControlIsSynthetic)) != 0)
                continue;
            if (!control->is_button() && !control->is_axis())
                continue;
            m_captureAxisValues[control_device_key(*device, control_path(*control))] =
                control->evaluate_magnitude(state);
        }
    }
}

bool PreviewRuntime::match_device(const Device& device) const {
    if (m_deviceFilter.empty())
        return true;
    if (equals_ignore_case(device.layout.str(), m_deviceFilter))
        return true;
    for (const std::string& candidate : device.layoutLineage) {
        if (equals_ignore_case(candidate, m_deviceFilter))
            return true;
    }
    return false;
}

bool PreviewRuntime::match_expected(const Control& control) const {
    return control_matches_expected(control, m_expectedControlType);
}

void PreviewRuntime::set_pending_capture(PreviewCapturedControl capture) {
    if (m_pendingCapture.has_value())
        return;
    const double now = m_runtime.now();
    const auto previous = m_captureLastPaths.find(capture.path);
    if (previous != m_captureLastPaths.end() && now - previous->second < 0.1)
        return;
    m_captureLastPaths[capture.path] = now;
    m_pendingCapture = std::move(capture);
}

void PreviewRuntime::scan_capture() {
    if (!m_captureActive || m_manager == nullptr)
        return;

    const bool keyboardAllowed =
        m_keyboard != nullptr ? match_device(*m_keyboard) : m_deviceFilter.empty();
    const bool mouseAllowed =
        m_mouse != nullptr ? match_device(*m_mouse) : m_deviceFilter.empty();

    if (keyboardAllowed && expected_keyboard_button(m_expectedControlType)) {
        for (std::size_t index = 0; index < kKeyCount; ++index) {
            const bool down = read_bit(m_keyBits.data(), static_cast<std::uint32_t>(index));
            const bool wasDown = read_bit(m_captureKeyBits.data(),
                                          static_cast<std::uint32_t>(index));
            if (down && !wasDown) {
                PreviewCapturedControl capture;
                capture.path = "<Keyboard>/" + std::string(key_name(static_cast<Key>(index)));
                capture.deviceId = m_keyboard != nullptr
                                       ? std::to_string(m_keyboard->deviceId)
                                       : "Keyboard";
                capture.controlLayout = "Key";
                capture.value = 1.0f;
                capture.isButton = true;
                capture.timestamp = m_runtime.now();
                set_pending_capture(std::move(capture));
                break;
            }
        }
    }
    m_captureKeyBits = m_keyBits;

    for (int button = 0; button < 5; ++button) {
        const std::uint8_t mask =
            static_cast<std::uint8_t>(1u << (static_cast<unsigned>(button) % 8u));
        const bool down =
            (m_mouseState.buttons[static_cast<std::size_t>(button) / 8u] & mask) != 0;
        const bool wasDown =
            (m_captureMouseButtons[static_cast<std::size_t>(button) / 8u] & mask) != 0;
        if (down && !wasDown && mouseAllowed &&
            expected_keyboard_button(m_expectedControlType)) {
            const std::string name = mouse_button_name(button);
            if (!name.empty()) {
                PreviewCapturedControl capture;
                capture.path = "<Mouse>/" + name;
                capture.deviceId = m_mouse != nullptr ? std::to_string(m_mouse->deviceId)
                                                      : "Mouse";
                capture.controlLayout = "Button";
                capture.value = 1.0f;
                capture.isButton = true;
                capture.timestamp = m_runtime.now();
                set_pending_capture(std::move(capture));
            }
        }
    }
    std::memcpy(m_captureMouseButtons, m_mouseState.buttons,
                sizeof(m_captureMouseButtons));

    if (mouseAllowed && expected_mouse_axis(m_expectedControlType)) {
        const float deltaLength = m_mouseState.delta.length();
        if (deltaLength > 0.01f) {
            PreviewCapturedControl capture;
            capture.path = "<Mouse>/delta";
            capture.deviceId = m_mouse != nullptr ? std::to_string(m_mouse->deviceId)
                                                  : "Mouse";
            capture.controlLayout = "Delta";
            capture.value = deltaLength;
            capture.isButton = false;
            capture.timestamp = m_runtime.now();
            set_pending_capture(std::move(capture));
        }
    }
    if (mouseAllowed && expected_mouse_axis(m_expectedControlType) &&
        (std::fabs(m_mouseState.scroll.x) > 0.01f ||
         std::fabs(m_mouseState.scroll.y) > 0.01f)) {
        PreviewCapturedControl capture;
        capture.path = "<Mouse>/scroll";
        capture.deviceId = m_mouse != nullptr ? std::to_string(m_mouse->deviceId) : "Mouse";
        capture.controlLayout = "Vector2";
        capture.value = m_mouseState.scroll.length();
        capture.isButton = false;
        capture.timestamp = m_runtime.now();
        set_pending_capture(std::move(capture));
    }
    for (Device* device : m_manager->devices()) {
        if (device == nullptr || device == m_keyboard || device == m_mouse)
            continue;
        if (!match_device(*device))
            continue;
        const std::uint8_t* state = device->current_state();
        if (state == nullptr)
            continue;
        for (Control* control : device->allControls) {
            if (control == nullptr || control == device)
                continue;
            if ((control->flags & (kControlIsNoisy | kControlIsSynthetic)) != 0)
                continue;
            if (!control->is_button() && !control->is_axis())
                continue;
            if (!match_expected(*control))
                continue;
            const std::string path = control_path(*control);
            const std::string key = control_device_key(*device, path);
            const float current = control->evaluate_magnitude(state);
            const auto found = m_captureAxisValues.find(key);
            const float previous = found != m_captureAxisValues.end() ? found->second : 0.0f;
            m_captureAxisValues[key] = current;

            const bool triggered = control->is_button()
                                       ? current >= 0.5f && previous < 0.5f
                                       : std::fabs(current) >= 0.3f &&
                                             std::fabs(previous) < 0.3f;
            if (!triggered)
                continue;

            PreviewCapturedControl capture;
            capture.path = path;
            capture.deviceId = std::to_string(device->deviceId);
            capture.controlLayout = control->layout.str();
            capture.value = current;
            capture.isButton = control->is_button();
            capture.timestamp = m_runtime.now();
            set_pending_capture(std::move(capture));
        }
    }
}

std::optional<PreviewCapturedControl> PreviewRuntime::take_capture() {
    if (!m_pendingCapture.has_value())
        return std::nullopt;
    std::optional<PreviewCapturedControl> result = std::move(m_pendingCapture);
    m_pendingCapture.reset();
    return result;
}

JsonValue PreviewRuntime::snapshot() {
    tick();
    const std::optional<PreviewCapturedControl> captured = take_capture();
    m_debugger->refresh(*m_manager, true);
    return snapshot_json(std::move(captured));
}

JsonValue PreviewRuntime::snapshot_json(
    std::optional<PreviewCapturedControl> captured) const {
    JsonValue root = make_object();
    add(root, "generation", make_number(static_cast<double>(m_generation)));
    add(root, "running", make_bool(m_running));
    add(root, "asset", make_string(m_asset ? m_asset->name : std::string{}));

    JsonValue devices = make_array();
    for (const DebuggerDeviceInfo& device : m_debugger->devices()) {
        JsonValue item = make_object();
        add(item, "deviceId", make_number(static_cast<double>(device.deviceId)));
        add(item, "name", make_string(device.name));
        add(item, "layout", make_string(device.layout));
        add(item, "enabled", make_bool(device.enabled));
        add(item, "connected", make_bool(device.connected));
        JsonValue controls = make_array();
        for (const DebuggerControlInfo& control : device.controls) {
            JsonValue controlJson = make_object();
            add(controlJson, "path", make_string(control.path));
            add(controlJson, "layout", make_string(control.layout));
            add(controlJson, "value", make_string(control.value));
            add(controlJson, "offset", make_number(static_cast<double>(control.offset)));
            add(controlJson, "sizeInBits",
                make_number(static_cast<double>(control.sizeInBits)));
            add(controlJson, "button", make_bool(control.button));
            add(controlJson, "axis", make_bool(control.axis));
            controls.array.push_back(std::move(controlJson));
        }
        add(item, "controls", std::move(controls));
        devices.array.push_back(std::move(item));
    }
    add(root, "devices", std::move(devices));

    JsonValue actions = make_array();
    for (const DebuggerActionInfo& action : m_debugger->actions()) {
        JsonValue item = make_object();
        add(item, "map", make_string(action.map));
        add(item, "name", make_string(action.name));
        add(item, "phase", make_string(action_phase_name(action.phase)));
        add(item, "value", make_string(action.value));
        add(item, "drivingControl", make_string(action.drivingControl));
        add(item, "lastStartedTime", make_number(action.lastStartedTime));
        add(item, "lastPerformedTime", make_number(action.lastPerformedTime));
        add(item, "lastCanceledTime", make_number(action.lastCanceledTime));
        add(item, "lastPerformedFrame",
            make_number(static_cast<double>(action.lastPerformedFrame)));
        add(item, "lastCanceledFrame",
            make_number(static_cast<double>(action.lastCanceledFrame)));
        actions.array.push_back(std::move(item));
    }
    add(root, "actions", std::move(actions));

    JsonValue bindings = make_array();
    for (const DebuggerBindingInfo& binding : m_debugger->bindings()) {
        JsonValue item = make_object();
        add(item, "map", make_string(binding.map));
        add(item, "action", make_string(binding.action));
        add(item, "path", make_string(binding.path));
        add(item, "interactions", make_string(binding.interactions));
        add(item, "processors", make_string(binding.processors));
        add(item, "name", make_string(binding.name));
        add(item, "overridden", make_bool(binding.overridden));
        add(item, "composite", make_bool(binding.composite));
        add(item, "partOfComposite", make_bool(binding.partOfComposite));
        bindings.array.push_back(std::move(item));
    }
    add(root, "bindings", std::move(bindings));

    JsonValue events = make_array();
    for (const DebuggerEventInfo& event : m_debugger->recent_events()) {
        JsonValue item = make_object();
        add(item, "time", make_number(event.time));
        add(item, "type", make_string(event.type));
        add(item, "deviceId", make_number(static_cast<double>(event.deviceId)));
        add(item, "size", make_number(static_cast<double>(event.size)));
        events.array.push_back(std::move(item));
    }
    add(root, "events", std::move(events));

    JsonValue hid = make_object();
    add(hid, "enabled", make_bool(m_hid != nullptr));
    add(hid, "platform", make_bool(m_platformHid != nullptr));
    add(root, "hid", std::move(hid));

    if (captured.has_value()) {
        JsonValue item = make_object();
        add(item, "path", make_string(captured->path));
        add(item, "deviceId", make_string(captured->deviceId));
        add(item, "controlLayout", make_string(captured->controlLayout));
        add(item, "value", make_number(captured->value));
        add(item, "isButton", make_bool(captured->isButton));
        add(item, "timestamp", make_number(captured->timestamp));
        add(root, "captured", std::move(item));
    } else {
        add(root, "captured", make_null());
    }
    return root;
}

} // namespace insys::editor
