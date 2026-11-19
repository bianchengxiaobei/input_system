#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>

#include "insys/actions/action_asset.h"
#include "insys/actions/action_state.h"
#include "insys/actions/binding_override.h"
#include "insys/actions/rebinding.h"
#include "insys/asset/asset_loader.h"
#include "insys/control_path.h"
#include "insys/debugger_model.h"
#include "insys/event_trace.h"
#include "insys/input_listeners.h"
#include "insys/string_utils.h"
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/EventListener.h>

#include "RmlUi_Renderer_GL3.h"
#include "insys/backend/glfw_backend.h"
#include "insys/backend/runtime.h"
#if defined(_WIN32)
#include "insys/backend/xinput_backend.h"
#endif
#include "insys/system.h"
#include "insys/ui/file_interface.h"
#include "insys/ui/rmlui_input_adapter.h"
#include "insys/ui/rmlui_system_interface.h"

namespace {

class CallbackListener : public Rml::EventListener {
public:
    explicit CallbackListener(std::function<void()> callback) : m_callback(std::move(callback)) {}

    void ProcessEvent(Rml::Event&) override {
        if (m_callback)
            m_callback();
    }

private:
    std::function<void()> m_callback;
};

class ButtonListener : public Rml::EventListener {
public:
    void set_status_element(Rml::Element* element) { m_status = element; }

    void ProcessEvent(Rml::Event&) override {
        ++m_clicks;
        std::cout << "clicked (" << m_clicks << ")\n";
        if (m_status != nullptr)
            m_status->SetInnerRML("clicks: " + std::to_string(m_clicks));
    }

private:
    Rml::Element* m_status = nullptr;
    int m_clicks = 0;
};

void set_rml(Rml::Element* element, const std::string& text) {
    if (element != nullptr)
        element->SetInnerRML(text);
}

const char* action_phase_class(insys::ActionPhase phase) {
    switch (phase) {
    case insys::ActionPhase::Started: return "started";
    case insys::ActionPhase::Performed: return "performed";
    case insys::ActionPhase::Canceled: return "canceled";
    case insys::ActionPhase::Waiting: return "waiting";
    case insys::ActionPhase::Disabled:
    case insys::ActionPhase::Invalid:
    default: return "disabled";
    }
}

std::string escape_rml(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (char character : text) {
        if (character == '&')
            escaped += "&amp;";
        else if (character == '<')
            escaped += "&lt;";
        else if (character == '>')
            escaped += "&gt;";
        else
            escaped += character;
    }
    return escaped;
}

void glfw_error_callback(int error, const char* description) {
    std::cerr << "[GLFW] error " << error << ": " << description << '\n';
}

} // namespace

int main(int argc, char** argv) {
    int smokeFrames = 0;
    int initialPage = 0;
    bool traceOnStart = false;
    bool rawInput = false;
    bool useXInput = false;
    bool useHid = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--smoke" && i + 1 < argc)
            smokeFrames = std::atoi(argv[++i]);
        else if (argument == "--page" && i + 1 < argc)
            initialPage = std::atoi(argv[++i]);
        else if (argument == "--trace")
            traceOnStart = true;
        else if (argument == "--raw-input")
            rawInput = true;
        else if (argument == "--xinput")
            useXInput = true;
        else if (argument == "--hid")
            useHid = true;
    }

    glfwSetErrorCallback(glfw_error_callback);

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "insys M7 - Platform Ecosystem", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    Rml::String glMessage;
    if (!RmlGL3::Initialize(&glMessage)) {
        std::cerr << "Failed to load OpenGL functions: " << glMessage << "\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    auto renderer = std::make_unique<RenderInterface_GL3>();
    if (!*renderer) {
        std::cerr << "Failed to initialize the RmlUi OpenGL3 renderer\n";
        RmlGL3::Shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    insys::backend::Runtime runtime;
    insys::Settings settings;
    settings.useRawInput = rawInput;
    settings.useXInput = useXInput;
    settings.useHid = useHid;
    insys::System system(runtime, settings);

    insys::backend::GlfwBackend backend;
    if (!backend.initialize(window, runtime, system.manager())) {
        std::cerr << "Failed to initialize the GLFW input backend\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    backend.set_log_events(true);

#if defined(_WIN32)
    std::unique_ptr<insys::backend::XInputBackend> xinputBackend;
    if (useXInput) {
        xinputBackend = std::make_unique<insys::backend::XInputBackend>();
        if (xinputBackend->initialize(runtime, system.manager())) {
            backend.set_skip_xinput_gamepads(true);
            std::cout << "[input] XInput backend enabled\n";
        } else {
            std::cerr << "[input] failed to initialize XInput backend\n";
            xinputBackend.reset();
        }
    }
#else
    if (useXInput)
        std::cerr << "[input] --xinput is only available on Windows\n";
#endif
    if (rawInput)
        std::cout << "[input] raw input " << (backend.raw_input_enabled() ? "enabled" : "unavailable")
                  << "\n";
    if (useHid)
        std::cout << "[input] HID backend " << (backend.hid_enabled() ? "enabled" : "unavailable")
                  << "\n";

    std::filesystem::path executable = std::filesystem::absolute(argv[0]);
    std::filesystem::path assetRoot = executable.parent_path() / "assets";

    insys::ui::FileInterface fileInterface;
    fileInterface.set_root(assetRoot);

    insys::ui::RmlUiSystemInterface systemInterface(runtime);

    Rml::SetSystemInterface(&systemInterface);
    Rml::SetFileInterface(&fileInterface);
    Rml::SetRenderInterface(renderer.get());

    if (!Rml::Initialise()) {
        std::cerr << "Failed to initialize RmlUi\n";
        backend.shutdown();
        renderer.reset();
        RmlGL3::Shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    if (!Rml::LoadFontFace("C:/Windows/Fonts/segoeui.ttf", "SegoeUI",
                           Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal)) {
        Rml::LoadFontFace("C:/Windows/Fonts/arial.ttf", "Arial",
                          Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);
    }

    int windowWidth = 1280;
    int windowHeight = 720;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);

    int framebufferWidth = windowWidth;
    int framebufferHeight = windowHeight;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0) {
        framebufferWidth = windowWidth;
        framebufferHeight = windowHeight;
    }

    float contentScaleX = 1.0f;
    float contentScaleY = 1.0f;
    glfwGetWindowContentScale(window, &contentScaleX, &contentScaleY);
    if (contentScaleX <= 0.0f)
        contentScaleX = 1.0f;
    if (contentScaleY <= 0.0f)
        contentScaleY = 1.0f;

    Rml::Context* context = Rml::CreateContext("main",
                                               Rml::Vector2i(framebufferWidth, framebufferHeight));
    if (context == nullptr) {
        std::cerr << "Failed to create RmlUi context\n";
        Rml::Shutdown();
        backend.shutdown();
        renderer.reset();
        RmlGL3::Shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    context->SetDensityIndependentPixelRatio(contentScaleX);


    Rml::ElementDocument* document = context->LoadDocument("main.rml");
    if (document == nullptr) {
        std::cerr << "Failed to load main.rml (looking in " << assetRoot.string() << ")\n";
        Rml::RemoveContext("main");
        Rml::Shutdown();
        backend.shutdown();
        renderer.reset();
        RmlGL3::Shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    document->Show(Rml::ModalFlag::None, Rml::FocusFlag::Auto);

    std::unordered_map<std::string, GLFWcursor*> cursors;
    auto create_cursor = [&](int shape) {
        GLFWcursor* cursor = glfwCreateStandardCursor(shape);
        return cursor;
    };
    cursors["arrow"] = nullptr;
    cursors["pointer"] = create_cursor(GLFW_HAND_CURSOR);
    cursors["text"] = create_cursor(GLFW_IBEAM_CURSOR);
    cursors["move"] = create_cursor(GLFW_RESIZE_ALL_CURSOR);
    cursors["resize"] = create_cursor(GLFW_RESIZE_NWSE_CURSOR);
    cursors["cross"] = create_cursor(GLFW_CROSSHAIR_CURSOR);
    cursors["unavailable"] = create_cursor(GLFW_NOT_ALLOWED_CURSOR);

    systemInterface.set_mouse_cursor_callback([&](const std::string& name) {
        GLFWcursor* cursor = nullptr;
        if (name == "pointer")
            cursor = cursors["pointer"];
        else if (name == "text")
            cursor = cursors["text"];
        else if (name == "move" || name.rfind("rmlui-scroll", 0) == 0)
            cursor = cursors["move"];
        else if (name == "resize")
            cursor = cursors["resize"];
        else if (name == "cross")
            cursor = cursors["cross"];
        else if (name == "unavailable")
            cursor = cursors["unavailable"];
        glfwSetCursor(window, cursor);
    });

    ButtonListener buttonListener;
    buttonListener.set_status_element(document->GetElementById("status"));
    if (Rml::Element* button = document->GetElementById("clickme"))
        button->AddEventListener("click", &buttonListener);

    insys::ActionAsset uiAsset;

    insys::ui::RmlUiInputAdapter inputAdapter(*context, system.manager());
    inputAdapter.set_devices(backend.keyboard(), backend.mouse());
    inputAdapter.set_capture_policy(insys::ui::CapturePolicy::PointerAndText);
    inputAdapter.set_navigation_dead_zone(0.5f);
    const float pointerScaleX = windowWidth > 0
                                    ? static_cast<float>(framebufferWidth) / windowWidth
                                    : contentScaleX;
    const float pointerScaleY = windowHeight > 0
                                    ? static_cast<float>(framebufferHeight) / windowHeight
                                    : contentScaleY;
    inputAdapter.set_coordinate_scale({pointerScaleX, pointerScaleY});
    insys::Vector2 appliedCoordinateScale{pointerScaleX, pointerScaleY};
    float appliedContentScale = contentScaleX;

    std::array<Rml::Element*, 6> pages{
        document->GetElementById("page_devices"),
        document->GetElementById("page_actions"),
        document->GetElementById("page_ui"),
        document->GetElementById("page_rebind"),
        document->GetElementById("page_debugger"),
        document->GetElementById("page_trace"),
    };
    std::size_t visiblePage = 0;
    auto show_page = [&](std::size_t index) {
        visiblePage = index;
        std::cout << "[ui] page " << index << '\n';
        for (std::size_t i = 0; i < pages.size(); ++i) {
            if (pages[i] != nullptr)
                pages[i]->SetProperty("display", i == index ? "block" : "none");
        }
    };
    show_page(static_cast<std::size_t>(std::clamp(initialPage, 0, 5)));

    CallbackListener devicesPageListener([&] { show_page(0); });
    CallbackListener actionsPageListener([&] { show_page(1); });
    CallbackListener uiPageListener([&] { show_page(2); });
    CallbackListener rebindPageListener([&] { show_page(3); });
    CallbackListener debuggerPageListener([&] { show_page(4); });
    CallbackListener tracePageListener([&] { show_page(5); });
    if (Rml::Element* button = document->GetElementById("tab_devices"))
        button->AddEventListener("click", &devicesPageListener);
    if (Rml::Element* button = document->GetElementById("tab_actions"))
        button->AddEventListener("click", &actionsPageListener);
    if (Rml::Element* button = document->GetElementById("tab_ui"))
        button->AddEventListener("click", &uiPageListener);
    if (Rml::Element* button = document->GetElementById("tab_rebind"))
        button->AddEventListener("click", &rebindPageListener);
    if (Rml::Element* button = document->GetElementById("tab_debugger"))
        button->AddEventListener("click", &debuggerPageListener);
    if (Rml::Element* button = document->GetElementById("tab_trace"))
        button->AddEventListener("click", &tracePageListener);

    Rml::Element* devicesElement = document->GetElementById("devices");
    Rml::Element* gamepadElement = document->GetElementById("gamepad");
    Rml::Element* moveElement = document->GetElementById("action_move");
    Rml::Element* jumpElement = document->GetElementById("action_jump");
    Rml::Element* fireElement = document->GetElementById("action_fire");
    Rml::Element* statusLineElement = document->GetElementById("status_line");
    Rml::Element* uiGateElement = document->GetElementById("ui_gate");
    Rml::Element* actionCountsElement = document->GetElementById("action_counts");
    Rml::Element* uiStatusElement = document->GetElementById("ui_status");
    Rml::Element* focusStatusElement = document->GetElementById("focus_status");
    Rml::Element* navStatusElement = document->GetElementById("nav_status");
    Rml::Element* jumpBindingElement = document->GetElementById("binding_jump");
    Rml::Element* fireBindingElement = document->GetElementById("binding_fire");
    Rml::Element* rebindStatusElement = document->GetElementById("rebind_status");
    Rml::Element* debuggerSummaryElement = document->GetElementById("debugger_summary");
    Rml::Element* debuggerDevicesElement = document->GetElementById("debugger_devices");
    Rml::Element* debuggerActionsElement = document->GetElementById("debugger_actions");
    Rml::Element* debuggerEventsElement = document->GetElementById("debugger_events");
    Rml::Element* debuggerBindingsElement = document->GetElementById("debugger_bindings");
    Rml::Element* debuggerPauseElement = document->GetElementById("debugger_pause");
    Rml::Element* traceStatusElement = document->GetElementById("trace_status");
    Rml::Element* traceToggleElement = document->GetElementById("trace_toggle");

    double lastJumpTime = 0.0;
    double lastFireTime = 0.0;
    int blockedJumps = 0;
    int blockedFires = 0;
    int jumpCount = 0;
    int fireCount = 0;

    insys::ActionAsset actionAsset;
    try {
        actionAsset = insys::asset::AssetLoader::load_from_file(assetRoot / "player.json");
    } catch (const std::exception& error) {
        std::cerr << "Failed to load player.json: " << error.what() << '\n';
        Rml::RemoveContext("main");
        Rml::Shutdown();
        backend.shutdown();
        renderer.reset();
        RmlGL3::Shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    insys::ActionMap* loadedMap = actionAsset.find_map("Player");
    if (loadedMap == nullptr) {
        std::cerr << "player.json has no Player action map\n";
        Rml::RemoveContext("main");
        Rml::Shutdown();
        backend.shutdown();
        renderer.reset();
        RmlGL3::Shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    insys::ActionMap& actionMap = *loadedMap;

    insys::ActionDef* moveAction = actionMap.find_action("Move");
    insys::ActionDef* jumpAction = actionMap.find_action("Jump");
    insys::ActionDef* fireAction = actionMap.find_action("Fire");
    if (moveAction == nullptr || jumpAction == nullptr || fireAction == nullptr) {
        std::cerr << "player.json is missing Move/Jump/Fire actions\n";
        Rml::RemoveContext("main");
        Rml::Shutdown();
        backend.shutdown();
        renderer.reset();
        RmlGL3::Shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    jumpAction->performed = [&](const insys::CallbackContext& context) {
        if (inputAdapter.is_capturing_keyboard()) {
            ++blockedJumps;
            return;
        }
        ++jumpCount;
        lastJumpTime = context.time;
        std::cout << "[action] Jump performed\n";
    };
    fireAction->performed = [&](const insys::CallbackContext& context) {
        if (inputAdapter.is_capturing_pointer()) {
            ++blockedFires;
            return;
        }
        ++fireCount;
        lastFireTime = context.time;
        std::cout << "[action] Fire performed\n";
    };

    insys::ActionDef* uiNavigate = nullptr;
    insys::ActionDef* uiSubmit = nullptr;
    insys::ActionDef* uiCancel = nullptr;
    try {
        uiAsset = insys::asset::AssetLoader::load_from_file(assetRoot / "ui.json");
        if (insys::ActionMap* uiMap = uiAsset.find_map("UI")) {
            uiNavigate = uiMap->find_action("Navigate");
            uiSubmit = uiMap->find_action("Submit");
            uiCancel = uiMap->find_action("Cancel");
            uiAsset.enable(system.manager());
            inputAdapter.set_ui_actions(uiNavigate, uiSubmit, uiCancel);
        }
    } catch (const std::exception& error) {
        std::cerr << "[ui] failed to load ui.json: " << error.what() << '\n';
    }

    auto update_nav_status = [&] {
        if (navStatusElement == nullptr)
            return;
        if (uiNavigate == nullptr) {
            navStatusElement->SetInnerRML(
                "Navigation: raw gamepad (ui.json unavailable)");
            return;
        }
        navStatusElement->SetInnerRML(
            inputAdapter.navigation_source() == insys::ui::NavigationSource::Actions
                ? "Navigation: UI actions (gamepad left stick/dpad, A/B)"
                : "Navigation: raw controls (gamepad left stick/dpad, A/B)");
    };
    update_nav_status();

    CallbackListener navToggleListener([&] {
        if (uiNavigate == nullptr)
            return;
        inputAdapter.set_navigation_source(
            inputAdapter.navigation_source() == insys::ui::NavigationSource::RawControls
                ? insys::ui::NavigationSource::Actions
                : insys::ui::NavigationSource::RawControls);
        update_nav_status();
    });
    if (Rml::Element* button = document->GetElementById("nav_toggle"))
        button->AddEventListener("click", &navToggleListener);

    const std::filesystem::path overrideFile = assetRoot / "bindings.json";
    if (std::filesystem::exists(overrideFile)) {
        std::ifstream input(overrideFile, std::ios::binary);
        std::ostringstream content;
        content << input.rdbuf();
        try {
            insys::load_binding_overrides(actionAsset, content.str());
        } catch (const std::exception& error) {
            std::cerr << "[rebind] load failed: " << error.what() << '\n';
        }
    }

    actionAsset.enable(system.manager());

    insys::DebuggerModel debugger;
    debugger.add_asset(actionAsset);
    debugger.add_asset(uiAsset);
    system.manager().add_event_listener(
        [&](insys::low::Event& event, insys::Device* device) {
            debugger.record_event(event, device);
        });

    insys::EventTrace trace;
    trace.attach(system.manager());
    if (traceOnStart)
        trace.enable();
    const std::filesystem::path traceFile = assetRoot / "trace.bin";

    CallbackListener debuggerPauseListener([&] {
        debugger.set_paused(!debugger.paused());
        if (debuggerPauseElement != nullptr)
            debuggerPauseElement->SetInnerRML(debugger.paused() ? "Resume refresh"
                                                                 : "Pause refresh");
    });
    CallbackListener debuggerClearListener([&] { debugger.clear_events(); });
    if (Rml::Element* button = document->GetElementById("debugger_pause"))
        button->AddEventListener("click", &debuggerPauseListener);
    if (Rml::Element* button = document->GetElementById("debugger_clear_events"))
        button->AddEventListener("click", &debuggerClearListener);

    std::string traceMessage;
    CallbackListener traceToggleListener([&] {
        if (trace.enabled()) {
            trace.disable();
            traceMessage = "Recording stopped";
        } else {
            trace.enable();
            traceMessage = "Recording started";
        }
        std::cout << "[trace] " << traceMessage << '\n';
    });
    CallbackListener traceClearListener([&] {
        trace.clear();
        traceMessage = "Trace cleared";
        std::cout << "[trace] " << traceMessage << '\n';
    });
    CallbackListener traceSaveListener([&] {
        traceMessage = trace.save(traceFile)
                           ? "Saved " + std::to_string(trace.event_count()) + " events to " +
                                 traceFile.string()
                           : "Save failed: " + trace.last_error();
        std::cout << "[trace] " << traceMessage << '\n';
    });
    CallbackListener traceLoadListener([&] {
        trace.disable();
        traceMessage = trace.load(traceFile)
                           ? "Loaded " + std::to_string(trace.event_count()) + " events from " +
                                 traceFile.string()
                           : "Load failed: " + trace.last_error();
        std::cout << "[trace] " << traceMessage << '\n';
    });
    CallbackListener traceReplayListener([&] {
        if (trace.event_count() != 0) {
            trace.replay(system.manager());
            traceMessage = "Replayed " + std::to_string(trace.event_count()) + " events";
        }
        std::cout << "[trace] " << traceMessage << '\n';
    });
    CallbackListener traceStepListener([&] {
        traceMessage = trace.replay_next(system.manager()) ? "Replay step" : "Replay complete";
        std::cout << "[trace] " << traceMessage << '\n';
    });
    if (Rml::Element* button = document->GetElementById("trace_toggle"))
        button->AddEventListener("click", &traceToggleListener);
    if (Rml::Element* button = document->GetElementById("trace_clear"))
        button->AddEventListener("click", &traceClearListener);
    if (Rml::Element* button = document->GetElementById("trace_save"))
        button->AddEventListener("click", &traceSaveListener);
    if (Rml::Element* button = document->GetElementById("trace_load"))
        button->AddEventListener("click", &traceLoadListener);
    if (Rml::Element* button = document->GetElementById("trace_replay"))
        button->AddEventListener("click", &traceReplayListener);
    if (Rml::Element* button = document->GetElementById("trace_step"))
        button->AddEventListener("click", &traceStepListener);

    std::unique_ptr<insys::RebindingOperation> rebindOperation;
    auto first_binding_index = [](const insys::ActionMap& map, const std::string& actionName) {
        for (std::size_t i = 0; i < map.bindings.size(); ++i) {
            const insys::Binding& binding = map.bindings[i];
            if (!binding.isPartOfComposite &&
                insys::equals_ignore_case(binding.action, actionName))
                return static_cast<int>(i);
        }
        return -1;
    };
    const int jumpBindingIndex = first_binding_index(actionMap, "Jump");
    const int fireBindingIndex = first_binding_index(actionMap, "Fire");

    auto updateBindingLabels = [&] {
        auto display = [](const insys::Binding& binding) {
            std::string text = insys::to_display_string(binding.effective_path());
            std::string escaped;
            for (char character : text) {
                if (character == '&')
                    escaped += "&amp;";
                else if (character == '<')
                    escaped += "&lt;";
                else if (character == '>')
                    escaped += "&gt;";
                else
                    escaped += character;
            }
            return escaped;
        };
        if (jumpBindingElement != nullptr && jumpBindingIndex >= 0)
            jumpBindingElement->SetInnerRML(
                "Jump binding: " + display(actionMap.bindings[static_cast<std::size_t>(jumpBindingIndex)]));
        if (fireBindingElement != nullptr && fireBindingIndex >= 0)
            fireBindingElement->SetInnerRML(
                "Fire binding: " + display(actionMap.bindings[static_cast<std::size_t>(fireBindingIndex)]));
    };

    auto setRebindStatus = [&](const std::string& text) {
        if (rebindStatusElement != nullptr)
            rebindStatusElement->SetInnerRML(text);
    };

    auto startRebind = [&](insys::ActionDef& action, int bindingIndex, const std::string& label) {
        if (!action.state)
            return;
        rebindOperation = std::make_unique<insys::RebindingOperation>();
        rebindOperation->with_action(action)
            .with_target_binding(bindingIndex)
            .with_timeout(5.0f)
            .with_magnitude_greater_than(0.5f)
            .on_match([&, label](insys::Control* control) {
                const std::string path = control != nullptr ? control->path() : std::string{};
                setRebindStatus(label + " bound to " + path);
                std::cout << "[rebind] " << label << " -> " << path << '\n';
            })
            .on_cancel([&, label] {
                setRebindStatus(label + " rebind canceled");
                std::cout << "[rebind] " << label << " canceled\n";
            })
            .on_complete([&] { updateBindingLabels(); });
        rebindOperation->start();
        setRebindStatus("Press a key or button for " + label + "...");
    };

    CallbackListener rebindJumpListener(
        [&] { startRebind(*jumpAction, jumpBindingIndex, "Jump"); });
    CallbackListener rebindFireListener(
        [&] { startRebind(*fireAction, fireBindingIndex, "Fire"); });
    CallbackListener saveBindingsListener([&] {
        std::ofstream output(overrideFile, std::ios::binary);
        output << insys::save_binding_overrides(actionAsset);
        setRebindStatus("Bindings saved to " + overrideFile.string());
        std::cout << "[rebind] saved " << overrideFile.string() << '\n';
    });
    CallbackListener resetBindingsListener([&] {
        rebindOperation.reset();
        for (insys::ActionMap& map : actionAsset.maps) {
            for (insys::Binding& binding : map.bindings) {
                binding.overridePath.clear();
                binding.overrideInteractions.clear();
                binding.overrideProcessors.clear();
            }
        }
        if (actionAsset.state() != nullptr)
            actionAsset.state()->re_resolve();
        std::error_code error;
        std::filesystem::remove(overrideFile, error);
        updateBindingLabels();
        setRebindStatus("Bindings reset");
        std::cout << "[rebind] reset\n";
    });

    if (Rml::Element* button = document->GetElementById("rebind_jump"))
        button->AddEventListener("click", &rebindJumpListener);
    if (Rml::Element* button = document->GetElementById("rebind_fire"))
        button->AddEventListener("click", &rebindFireListener);
    if (Rml::Element* button = document->GetElementById("save_bindings"))
        button->AddEventListener("click", &saveBindingsListener);
    if (Rml::Element* button = document->GetElementById("reset_bindings"))
        button->AddEventListener("click", &resetBindingsListener);

    updateBindingLabels();

    auto sync_ui_dimensions = [&] {
        int newWindowWidth = 0;
        int newWindowHeight = 0;
        int newFramebufferWidth = 0;
        int newFramebufferHeight = 0;
        glfwGetWindowSize(window, &newWindowWidth, &newWindowHeight);
        glfwGetFramebufferSize(window, &newFramebufferWidth, &newFramebufferHeight);
        if (newFramebufferWidth <= 0 || newFramebufferHeight <= 0)
            return;

        float newContentScaleX = 1.0f;
        float newContentScaleY = 1.0f;
        glfwGetWindowContentScale(window, &newContentScaleX, &newContentScaleY);
        if (newContentScaleX <= 0.0f)
            newContentScaleX = 1.0f;
        if (newContentScaleY <= 0.0f)
            newContentScaleY = 1.0f;

        const bool framebufferChanged =
            newFramebufferWidth != framebufferWidth || newFramebufferHeight != framebufferHeight;
        windowWidth = newWindowWidth;
        windowHeight = newWindowHeight;
        framebufferWidth = newFramebufferWidth;
        framebufferHeight = newFramebufferHeight;
        contentScaleX = newContentScaleX;
        contentScaleY = newContentScaleY;

        if (framebufferChanged) {
            context->SetDimensions(Rml::Vector2i(framebufferWidth, framebufferHeight));
            renderer->SetViewport(framebufferWidth, framebufferHeight);
        }
        if (contentScaleX != appliedContentScale) {
            context->SetDensityIndependentPixelRatio(contentScaleX);
            appliedContentScale = contentScaleX;
        }
        const insys::Vector2 coordinateScale{
            windowWidth > 0 ? static_cast<float>(framebufferWidth) / windowWidth
                            : contentScaleX,
            windowHeight > 0 ? static_cast<float>(framebufferHeight) / windowHeight
                             : contentScaleY};
        if (coordinateScale != appliedCoordinateScale) {
            inputAdapter.set_coordinate_scale(coordinateScale);
            appliedCoordinateScale = coordinateScale;
        }
    };
    sync_ui_dimensions();
    renderer->SetViewport(framebufferWidth, framebufferHeight);

    auto update_debugger_ui = [&] {
        if (visiblePage != 4)
            return;

        debugger.refresh(system.manager());

        if (debuggerSummaryElement != nullptr) {
            std::ostringstream stream;
            stream << "Debugger: devices=" << debugger.devices().size()
                   << " actions=" << debugger.actions().size()
                   << " events=" << debugger.recent_events().size()
                   << (debugger.paused() ? " (paused)" : "");
            debuggerSummaryElement->SetInnerRML(stream.str());
        }

        if (debuggerDevicesElement != nullptr) {
            std::ostringstream stream;
            for (const insys::DebuggerDeviceInfo& device : debugger.devices()) {
                stream << "<p>#" << device.deviceId << " "
                       << escape_rml(device.name) << " [" << escape_rml(device.layout)
                       << "]</p>";
                for (const insys::DebuggerControlInfo& control : device.controls) {
                    stream << "<p>" << escape_rml(control.path) << " = "
                           << escape_rml(control.value) << " (" << control.offset << "B/"
                           << control.sizeInBits << "bit)</p>";
                }
            }
            debuggerDevicesElement->SetInnerRML(stream.str());
        }

        if (debuggerActionsElement != nullptr) {
            std::ostringstream stream;
            for (const insys::DebuggerActionInfo& action : debugger.actions()) {
                stream << "<p>" << escape_rml(action.map) << "/" << escape_rml(action.name)
                       << " <span class=\"phase-" << action_phase_class(action.phase) << "\">["
                       << insys::action_phase_name(action.phase) << "]</span> "
                       << escape_rml(action.value);
                if (!action.drivingControl.empty())
                    stream << " via " << escape_rml(action.drivingControl);
                if (action.lastPerformedTime > 0.0)
                    stream << " last-performed=" << action.lastPerformedTime;
                stream << "</p>";
            }
            debuggerActionsElement->SetInnerRML(stream.str());
        }

        if (debuggerEventsElement != nullptr) {
            std::ostringstream stream;
            stream.setf(std::ios::fixed);
            stream.precision(2);
            const std::size_t start = debugger.recent_events().size() > 20
                                          ? debugger.recent_events().size() - 20
                                          : 0;
            for (std::size_t i = start; i < debugger.recent_events().size(); ++i) {
                const insys::DebuggerEventInfo& event = debugger.recent_events()[i];
                stream << "<p>" << event.time << " " << escape_rml(event.type)
                       << " device=" << event.deviceId << " size=" << event.size << "</p>";
            }
            if (debugger.recent_events().empty())
                stream << "<p>(no events)</p>";
            debuggerEventsElement->SetInnerRML(stream.str());
            debuggerEventsElement->SetScrollTop(debuggerEventsElement->GetScrollHeight());
        }

        if (debuggerBindingsElement != nullptr) {
            std::ostringstream stream;
            for (const insys::DebuggerBindingInfo& binding : debugger.bindings()) {
                stream << "<p>" << escape_rml(binding.map) << "/"
                       << escape_rml(binding.action) << " -> "
                       << escape_rml(binding.path);
                if (!binding.interactions.empty())
                    stream << " interactions=" << escape_rml(binding.interactions);
                if (!binding.processors.empty())
                    stream << " processors=" << escape_rml(binding.processors);
                if (binding.overridden)
                    stream << " [override]";
                stream << "</p>";
            }
            debuggerBindingsElement->SetInnerRML(stream.str());
        }
    };

    int frameCounter = 0;
    while (!glfwWindowShouldClose(window)) {
        ++frameCounter;
        if (smokeFrames > 0 && frameCounter >= smokeFrames)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        glfwPollEvents();
        backend.poll();
        sync_ui_dimensions();

        system.update(insys::UpdateType::Dynamic);
        inputAdapter.flush();
        update_debugger_ui();

        if (devicesElement != nullptr) {
            std::ostringstream stream;
            stream << "Devices: ";
            const auto& devices = system.manager().devices();
            for (std::size_t i = 0; i < devices.size(); ++i) {
                if (i != 0)
                    stream << " | ";
                stream << devices[i]->name.c_str() << " (" << devices[i]->layout.c_str()
                       << " #" << devices[i]->deviceId << ")";
            }
            set_rml(devicesElement, stream.str());
        }

        if (gamepadElement != nullptr) {
            auto* gamepad = static_cast<insys::Gamepad*>(
                system.manager().device_by_layout("Gamepad"));
            const uint8_t* state = gamepad != nullptr ? gamepad->current_state() : nullptr;
            if (state == nullptr) {
                set_rml(gamepadElement, "Gamepad: none");
            } else {
                const insys::Vector2 left = gamepad->left_stick()->read_vector2(state);
                const insys::Vector2 right = gamepad->right_stick()->read_vector2(state);
                const insys::Vector2 dpad = gamepad->dpad()->read_vector2(state);
                const float leftTrigger = gamepad->left_trigger()->read_float(state);
                const float rightTrigger = gamepad->right_trigger()->read_float(state);

                std::ostringstream stream;
                stream.setf(std::ios::fixed);
                stream.precision(2);
                stream << "Gamepad: LS(" << left.x << ", " << left.y << ") RS("
                       << right.x << ", " << right.y << ") LT=" << leftTrigger
                       << " RT=" << rightTrigger << " dpad(" << dpad.x << ", " << dpad.y
                       << ") pressed: ";

                static const std::pair<const char*, const char*> buttons[] = {
                    {"buttonSouth", "South"}, {"buttonEast", "East"},
                    {"buttonWest", "West"}, {"buttonNorth", "North"},
                    {"leftShoulder", "L1"}, {"rightShoulder", "R1"},
                    {"leftStickButton", "L3"}, {"rightStickButton", "R3"},
                    {"start", "Start"}, {"select", "Select"},
                };

                bool anyPressed = false;
                for (const auto& [controlName, label] : buttons) {
                    insys::Control* control = gamepad->button(controlName);
                    if (control != nullptr && control->read_float(state) >= 0.5f) {
                        if (anyPressed)
                            stream << " ";
                        stream << label;
                        anyPressed = true;
                    }
                }
                if (!anyPressed)
                    stream << "-";
                set_rml(gamepadElement, stream.str());
            }
        }

        if (moveElement != nullptr || jumpElement != nullptr || fireElement != nullptr) {
            insys::ActionState* actionState = actionAsset.state();
            if (actionState != nullptr) {
                std::ostringstream stream;
                stream.setf(std::ios::fixed);
                stream.precision(2);

                if (moveElement != nullptr) {
                    const insys::Vector2 value = actionState->read_value<insys::Vector2>(*moveAction);
                    stream << "Move [" << insys::action_phase_name(actionState->phase(*moveAction))
                           << "] (" << value.x << ", " << value.y << ")";
                    if (insys::Control* control = actionState->active_control(*moveAction))
                        stream << " via " << control->path();
                    set_rml(moveElement, stream.str());
                    stream.str({});
                }

                if (jumpElement != nullptr) {
                    stream << "Jump [" << insys::action_phase_name(actionState->phase(*jumpAction))
                           << "] last=" << lastJumpTime;
                    if (insys::Control* control = actionState->active_control(*jumpAction))
                        stream << " via " << control->path();
                    set_rml(jumpElement, stream.str());
                    stream.str({});
                }

                if (fireElement != nullptr) {
                    stream << "Fire [" << insys::action_phase_name(actionState->phase(*fireAction))
                           << "] last=" << lastFireTime;
                    if (insys::Control* control = actionState->active_control(*fireAction))
                        stream << " via " << control->path();
                    set_rml(fireElement, stream.str());
                }
            }
        }

        if (uiGateElement != nullptr) {
            std::ostringstream stream;
            stream << "UI gate: pointer="
                   << (inputAdapter.is_capturing_pointer() ? "blocked" : "pass")
                   << " keyboard="
                   << (inputAdapter.is_capturing_keyboard() ? "blocked" : "pass")
                   << " text=" << (inputAdapter.is_capturing_text() ? "blocked" : "pass")
                   << " hover=" << (inputAdapter.pointer_over_ui() ? "yes" : "no");
            set_rml(uiGateElement, stream.str());
        }

        if (actionCountsElement != nullptr) {
            std::ostringstream stream;
            stream << "Actions: jump=" << jumpCount << " fire=" << fireCount
                   << " blocked jump=" << blockedJumps << " fire=" << blockedFires;
            set_rml(actionCountsElement, stream.str());
        }

        if (uiStatusElement != nullptr) {
            std::ostringstream stream;
            stream << "UI consume: pointer=" << (inputAdapter.is_consuming_pointer() ? 1 : 0)
                   << " keyboard=" << (inputAdapter.is_consuming_keyboard() ? 1 : 0)
                   << " text=" << (inputAdapter.is_consuming_text() ? 1 : 0)
                   << " policy="
                   << (inputAdapter.capture_policy() == insys::ui::CapturePolicy::Full
                           ? "Full"
                           : inputAdapter.capture_policy() ==
                                     insys::ui::CapturePolicy::PointerAndText
                                 ? "PointerAndText"
                                 : "None");
            set_rml(uiStatusElement, stream.str());
        }

        if (focusStatusElement != nullptr) {
            Rml::Element* focus = context->GetFocusElement();
            std::ostringstream stream;
            stream << "Focus: ";
            if (focus == nullptr || focus == context->GetRootElement() ||
                focus->GetId().empty()) {
                stream << "document";
            } else {
                stream << "#" << focus->GetId().c_str();
            }
            set_rml(focusStatusElement, stream.str());
        }

        if (statusLineElement != nullptr) {
            std::ostringstream stream;
            stream.setf(std::ios::fixed);
            stream.precision(2);
            stream << "framebuffer " << framebufferWidth << "x" << framebufferHeight
                   << " scale " << contentScaleX << "x" << contentScaleY
                   << " nav="
                   << (inputAdapter.navigation_source() == insys::ui::NavigationSource::Actions
                           ? "actions"
                           : "raw");
            set_rml(statusLineElement, stream.str());
        }

        if (traceStatusElement != nullptr) {
            std::ostringstream stream;
            stream << "Trace: " << (trace.enabled() ? "recording" : "stopped")
                   << " | events=" << trace.event_count()
                   << " bytes=" << trace.total_bytes()
                   << " dropped=" << trace.dropped_event_count();
            if (!traceMessage.empty())
                stream << " | " << escape_rml(traceMessage);
            else if (!trace.last_error().empty())
                stream << " | " << escape_rml(trace.last_error());
            set_rml(traceStatusElement, stream.str());
        }
        if (traceToggleElement != nullptr)
            set_rml(traceToggleElement,
                    trace.enabled() ? "Stop recording" : "Start recording");

        if (!system.pending_text().empty()) {
            std::cout << "[text] ";
            for (char32_t character : system.pending_text()) {
                if (character < 0x80)
                    std::cout << static_cast<char>(character);
                else
                    std::cout << "<U+" << std::hex << static_cast<uint32_t>(character) << std::dec << ">";
            }
            std::cout << '\n';
        }
        system.clear_pending_text();

        if (smokeFrames > 0 && frameCounter == smokeFrames - 2) {
            Rml::Element* status = document->GetElementById("status");
            auto* textbox = static_cast<Rml::ElementFormControlInput*>(document->GetElementById("textbox"));
            std::cout << "[smoke] status=" << (status ? status->GetInnerRML() : Rml::String("<missing>"))
                      << " text=" << (textbox ? textbox->GetValue() : Rml::String("<missing>"))
                      << " move=" << (moveElement ? moveElement->GetInnerRML() : Rml::String("<missing>"))
                      << " jump_binding=" << (jumpBindingElement ? jumpBindingElement->GetInnerRML() : Rml::String("<missing>"))
                      << " jump_phase="
                      << (actionAsset.state() && jumpAction
                              ? insys::action_phase_name(actionAsset.state()->phase(*jumpAction))
                              : "?")
                      << " debugger="
                      << (debuggerSummaryElement ? debuggerSummaryElement->GetInnerRML()
                                                 : Rml::String("<missing>"))
                      << " trace="
                      << (traceStatusElement ? traceStatusElement->GetInnerRML()
                                             : Rml::String("<missing>"))
                      << '\n';
            if (trace.enabled()) {
                Rml::Element* toggle = document->GetElementById("trace_toggle");
                Rml::Element* save = document->GetElementById("trace_save");
                if (toggle != nullptr && save != nullptr) {
                    const Rml::Vector2f toggleOffset = toggle->GetAbsoluteOffset();
                    const Rml::Vector2f saveOffset = save->GetAbsoluteOffset();
                    std::cout << "[pos] toggle=" << toggleOffset.x << "," << toggleOffset.y
                              << " save=" << saveOffset.x << "," << saveOffset.y << '\n';
                }
            }
        }

        renderer->BeginFrame();
        renderer->Clear();
        context->Update();
        context->Render();
        renderer->EndFrame();

        glfwSwapBuffers(window);
    }

    Rml::RemoveContext("main");
    Rml::Shutdown();

    backend.shutdown();
    renderer.reset();
    RmlGL3::Shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}


