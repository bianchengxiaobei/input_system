#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <objbase.h>
#include <oleauto.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <wrl.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "insys/editor/editor_server.h"

namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

constexpr wchar_t kWindowClass[] = L"InsysEditorWindow";
constexpr wchar_t kWindowTitle[] = L"insys editor";
constexpr UINT_PTR kSmokeTimer = 1;
constexpr UINT_PTR kSmokeReadyTimer = 2;
constexpr UINT_PTR kSavePollTimer = 3;
constexpr UINT kCommandOpenAsset = 40001;
constexpr UINT kCommandOpenBrowser = 40002;
constexpr UINT kCommandExit = 40003;

std::string to_utf8(std::wstring_view text) {
    if (text.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring from_utf8(std::string_view text) {
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size);
    return result;
}

std::string hresult_text(HRESULT result) {
    std::ostringstream text;
    text << "0x" << std::hex << std::setw(8) << std::setfill('0')
         << static_cast<unsigned long>(result);
    return text.str();
}

std::string redact_url(std::string_view url) {
    const std::size_t token = url.find("token=");
    if (token == std::string_view::npos)
        return std::string(url);
    return std::string(url.substr(0, token)) + "token=<redacted>";
}

std::wstring json_escape(std::wstring_view text) {
    std::wostringstream result;
    result << std::hex << std::setfill(L'0');
    for (const wchar_t character : text) {
        switch (character) {
        case L'\\':
            result << L"\\\\";
            break;
        case L'"':
            result << L"\\\"";
            break;
        case L'\n':
            result << L"\\n";
            break;
        case L'\r':
            result << L"\\r";
            break;
        case L'\t':
            result << L"\\t";
            break;
        default:
            if (character < 0x20) {
                result << L"\\u" << std::setw(4)
                       << static_cast<unsigned int>(character);
            } else {
                result << character;
            }
            break;
        }
    }
    return result.str();
}

std::string url_encode(std::string_view text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(text.size());
    for (const unsigned char character : text) {
        const bool unreserved =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_' || character == '.' ||
            character == '~';
        if (unreserved) {
            result.push_back(static_cast<char>(character));
        } else {
            result.push_back('%');
            result.push_back(kHex[(character >> 4u) & 0x0fu]);
            result.push_back(kHex[character & 0x0fu]);
        }
    }
    return result;
}

class Log {
public:
    bool open(const std::filesystem::path& path) {
        if (path.empty())
            return true;
        std::error_code error;
        const std::filesystem::path parent = path.parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, error);
        m_file.open(path, std::ios::out | std::ios::app);
        return m_file.is_open();
    }

    void line(std::string_view text) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        std::ostringstream message;
        message << "[" << std::setfill('0') << std::setw(2) << now.wHour << ":"
                << std::setw(2) << now.wMinute << ":" << std::setw(2)
                << now.wSecond << "." << std::setw(3) << now.wMilliseconds
                << "] " << text << "\n";
        const std::string output = message.str();
        OutputDebugStringA(output.c_str());
        if (m_file.is_open()) {
            m_file << output;
            m_file.flush();
        }
    }

private:
    std::ofstream m_file;
};

struct ShellConfig {
    bool devtools = false;
    bool browserOnly = false;
    bool openBrowser = false;
    UINT smokeMs = 0;
    std::uint16_t remoteDebuggingPort = 0;
    std::wstring browserPath;
    std::wstring logPath;
    std::wstring urlFile;
    std::wstring userDataDir;
};

struct CommandLine {
    insys::editor::EditorServerOptions server;
    ShellConfig shell;
    bool help = false;
};

bool parse_unsigned(const std::wstring& text, unsigned long& value) {
    if (text.empty())
        return false;
    try {
        std::size_t consumed = 0;
        value = std::stoul(text, &consumed, 10);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}

std::filesystem::path executable_directory() {
    std::vector<wchar_t> buffer(1024);
    while (true) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return std::filesystem::current_path();
        if (length < buffer.size() - 1)
            return std::filesystem::path(buffer.data(), buffer.data() + length)
                .parent_path();
        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path default_web_dist() {
    const std::filesystem::path exe = executable_directory();
    std::error_code error;
    if (std::filesystem::is_directory(exe / "web", error))
        return exe / "web";
#ifdef INSYS_EDITOR_WEB_DIST
    const std::filesystem::path configured(INSYS_EDITOR_WEB_DIST);
    if (std::filesystem::is_directory(configured))
        return configured;
#endif
    const std::filesystem::path candidates[] = {
        exe / ".." / ".." / ".." / "editor" / "web" / "dist",
        std::filesystem::current_path() / "editor" / "web" / "dist",
    };
    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::is_directory(candidate, error))
            return candidate;
    }
    return {};
}

std::filesystem::path default_user_data_dir() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE,
                                       nullptr, &raw))) {
        std::filesystem::path path(raw);
        CoTaskMemFree(raw);
        return path / "insys" / "editor" / "WebView2";
    }
    if (raw != nullptr)
        CoTaskMemFree(raw);
    return std::filesystem::temp_directory_path() / "insys-editor-WebView2";
}

void show_usage() {
    MessageBoxW(
        nullptr,
        L"usage: insys_editor [options]\n\n"
        L"  --port N              listen port (default: ephemeral)\n"
        L"  --static PATH         web frontend dist directory\n"
        L"  --asset-root PATH     allowed asset root (repeatable)\n"
        L"  --token TOKEN         fixed editor token (development only)\n"
        L"  --user-data-dir PATH  WebView2 user data directory\n"
        L"  --remote-debugging-port N  enable local WebView2 CDP (E2E)\n"
        L"  --devtools            enable WebView2 devtools and context menus\n"
        L"  --browser-only        use an external browser instead of WebView2\n"
        L"  --open-browser        also open the editor in an external browser\n"
        L"  --browser PATH        browser executable for external tabs\n"
        L"  --url-file PATH       write the launch URL to a file\n"
        L"  --log PATH            write launcher diagnostics to a file\n"
        L"  --smoke-ms N          close automatically after N milliseconds\n"
        L"  --help                show this help\n",
        L"insys editor", MB_OK | MB_ICONINFORMATION);
}

bool parse_command_line(CommandLine& config, std::wstring& error) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        error = L"CommandLineToArgvW failed";
        return false;
    }

    config.server.host = "127.0.0.1";
    config.server.port = 0;
    config.server.serverVersion = "0.1.0";
    config.server.session.assetRoots.push_back(std::filesystem::current_path());

    bool hasAssetRoot = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring argument = argv[i];
        const auto require_value = [&](std::wstring& value) {
            if (i + 1 >= argc) {
                error = L"missing value for " + argument;
                return false;
            }
            value = argv[++i];
            return true;
        };

        if (argument == L"--help" || argument == L"-h") {
            config.help = true;
        } else if (argument == L"--port") {
            std::wstring value;
            if (!require_value(value)) {
                LocalFree(argv);
                return false;
            }
            unsigned long port = 0;
            if (!parse_unsigned(value, port) || port > 65535) {
                error = L"invalid port";
                LocalFree(argv);
                return false;
            }
            config.server.port = static_cast<std::uint16_t>(port);
        } else if (argument == L"--static") {
            std::wstring value;
            if (!require_value(value)) {
                LocalFree(argv);
                return false;
            }
            config.server.staticRoot = std::filesystem::path(value);
        } else if (argument == L"--asset-root") {
            std::wstring value;
            if (!require_value(value)) {
                LocalFree(argv);
                return false;
            }
            if (!hasAssetRoot) {
                config.server.session.assetRoots.clear();
                hasAssetRoot = true;
            }
            config.server.session.assetRoots.push_back(std::filesystem::path(value));
        } else if (argument == L"--token") {
            std::wstring value;
            if (!require_value(value)) {
                LocalFree(argv);
                return false;
            }
            config.server.token = to_utf8(value);
        } else if (argument == L"--user-data-dir") {
            if (!require_value(config.shell.userDataDir)) {
                LocalFree(argv);
                return false;
            }
        } else if (argument == L"--remote-debugging-port") {
            std::wstring value;
            if (!require_value(value)) {
                LocalFree(argv);
                return false;
            }
            unsigned long port = 0;
            if (!parse_unsigned(value, port) || port == 0 || port > 65535) {
                error = L"invalid --remote-debugging-port";
                LocalFree(argv);
                return false;
            }
            config.shell.remoteDebuggingPort =
                static_cast<std::uint16_t>(port);
        } else if (argument == L"--devtools") {
            config.shell.devtools = true;
        } else if (argument == L"--browser-only") {
            config.shell.browserOnly = true;
        } else if (argument == L"--open-browser") {
            config.shell.openBrowser = true;
        } else if (argument == L"--browser") {
            if (!require_value(config.shell.browserPath)) {
                LocalFree(argv);
                return false;
            }
        } else if (argument == L"--url-file") {
            if (!require_value(config.shell.urlFile)) {
                LocalFree(argv);
                return false;
            }
        } else if (argument == L"--log") {
            if (!require_value(config.shell.logPath)) {
                LocalFree(argv);
                return false;
            }
        } else if (argument == L"--smoke-ms") {
            std::wstring value;
            if (!require_value(value)) {
                LocalFree(argv);
                return false;
            }
            unsigned long milliseconds = 0;
            if (!parse_unsigned(value, milliseconds) || milliseconds == 0 ||
                milliseconds > 60000) {
                error = L"invalid --smoke-ms value";
                LocalFree(argv);
                return false;
            }
            config.shell.smokeMs = static_cast<UINT>(milliseconds);
        } else {
            error = L"unknown argument: " + argument;
            LocalFree(argv);
            return false;
        }
    }
    LocalFree(argv);

    if (config.server.staticRoot.empty())
        config.server.staticRoot = default_web_dist();
    return true;
}

class EditorWindow {
public:
    EditorWindow(HINSTANCE instance, insys::editor::EditorServer& server,
                 ShellConfig config, Log& log)
        : m_instance(instance), m_server(server), m_config(std::move(config)),
          m_log(log) {}

    ~EditorWindow() { shutdown(); }

    EditorWindow(const EditorWindow&) = delete;
    EditorWindow& operator=(const EditorWindow&) = delete;

    bool create(int showCommand) {
        const std::string token = url_encode(m_server.token());
        m_url = "http://127.0.0.1:" + std::to_string(m_server.port()) +
                "/?token=" + token;
        const std::string origin =
            "http://127.0.0.1:" + std::to_string(m_server.port()) + "/";
        m_origin = from_utf8(origin);

        if (!m_config.urlFile.empty()) {
            std::ofstream urlFile(std::filesystem::path(m_config.urlFile),
                                  std::ios::out | std::ios::trunc);
            if (urlFile.is_open())
                urlFile << m_url << "\n";
        }

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &EditorWindow::window_proc;
        windowClass.hInstance = m_instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground =
            reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
        windowClass.lpszClassName = kWindowClass;
        RegisterClassExW(&windowClass);

        RECT bounds{0, 0, 1280, 800};
        AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0);
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        const int x = std::max(0, (screenWidth - width) / 2);
        const int y = std::max(0, (screenHeight - height) / 2);

        m_window = CreateWindowExW(
            0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            x, y, width, height, nullptr, nullptr, m_instance, this);
        if (m_window == nullptr) {
            m_log.line("shell: CreateWindowEx failed: " +
                       std::to_string(GetLastError()));
            return false;
        }

        create_menu();
        m_status = CreateWindowExW(
            0, L"STATIC", L"Starting insys editor...", WS_CHILD | WS_VISIBLE,
            24, 24, 960, 80, m_window, nullptr, m_instance, nullptr);
        ShowWindow(m_window, showCommand);
        UpdateWindow(m_window);

        if (m_config.browserOnly) {
            if (open_in_browser("requested by --browser-only")) {
                if (m_status != nullptr) {
                    SetWindowTextW(
                        m_status,
                        L"insys editor is open in an external browser.\n"
                        L"Close this window to stop the local editor server.");
                    ShowWindow(m_status, SW_SHOW);
                }
                SetWindowTextW(m_window, L"insys editor - external browser");
            } else if (m_status != nullptr) {
                SetWindowTextW(
                    m_status,
                    L"Could not open an external browser.\nUse File > Open in "
                    L"Browser to retry, or close this window.");
            }
        } else {
            if (m_config.openBrowser)
                open_in_browser("requested by --open-browser", false);
            start_webview();
        }
        if (m_config.smokeMs != 0)
            SetTimer(m_window, kSmokeTimer, m_config.smokeMs, nullptr);
        return true;
    }

    int run() {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (m_accelerators != nullptr &&
                TranslateAcceleratorW(m_window, m_accelerators, &message) != 0) {
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

    void shutdown() {
        if (m_shutdown)
            return;
        m_shutdown = true;
        if (m_window != nullptr && IsWindow(m_window)) {
            KillTimer(m_window, kSmokeTimer);
            KillTimer(m_window, kSmokeReadyTimer);
            KillTimer(m_window, kSavePollTimer);
        }
        if (m_webview && m_navigationStartingToken.value != 0)
            m_webview->remove_NavigationStarting(m_navigationStartingToken);
        if (m_webview && m_navigationCompletedToken.value != 0)
            m_webview->remove_NavigationCompleted(m_navigationCompletedToken);
        if (m_webview && m_newWindowRequestedToken.value != 0)
            m_webview->remove_NewWindowRequested(m_newWindowRequestedToken);
        if (m_webview && m_documentTitleChangedToken.value != 0)
            m_webview->remove_DocumentTitleChanged(m_documentTitleChangedToken);
        if (m_webview && m_windowCloseRequestedToken.value != 0)
            m_webview->remove_WindowCloseRequested(m_windowCloseRequestedToken);
        if (m_controller && m_acceleratorKeyPressedToken.value != 0)
            m_controller->remove_AcceleratorKeyPressed(
                m_acceleratorKeyPressedToken);
        if (m_accelerators != nullptr) {
            DestroyAcceleratorTable(m_accelerators);
            m_accelerators = nullptr;
        }
        if (m_menu != nullptr) {
            DestroyMenu(m_menu);
            m_menu = nullptr;
        }
        if (m_controller)
            m_controller->Close();
        m_webview.Reset();
        m_controller.Reset();
        m_environment.Reset();
        m_navigationStartingHandler.Reset();
        m_navigationCompletedHandler.Reset();
        m_newWindowRequestedHandler.Reset();
        m_documentTitleChangedHandler.Reset();
        m_windowCloseRequestedHandler.Reset();
        m_acceleratorKeyPressedHandler.Reset();
        m_executeScriptHandler.Reset();
        m_closeQueryHandler.Reset();
        m_saveClickHandler.Reset();
        m_savePollHandler.Reset();
        m_environmentCompletedHandler.Reset();
        m_controllerCompletedHandler.Reset();
    }

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wParam,
                                        LPARAM lParam) {
        EditorWindow* self = reinterpret_cast<EditorWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<EditorWindow*>(create->lpCreateParams);
            self->m_window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self));
        }
        if (self == nullptr)
            return DefWindowProcW(window, message, wParam, lParam);
        return self->handle_message(message, wParam, lParam);
    }

    LRESULT handle_message(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case kCommandOpenAsset:
                open_asset_dialog();
                return 0;
            case kCommandOpenBrowser:
                open_in_browser("requested from File menu");
                return 0;
            case kCommandExit:
                request_close();
                return 0;
            default:
                break;
            }
            break;
        case WM_SIZE:
            if (m_controller)
                set_bounds(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_DPICHANGED:
            if (m_controller)
                set_bounds();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 900;
            info->ptMinTrackSize.y = 600;
            return 0;
        }
        case WM_TIMER:
            if (wParam == kSmokeTimer || wParam == kSmokeReadyTimer) {
                KillTimer(m_window, static_cast<UINT_PTR>(wParam));
                m_log.line("shell: smoke timer elapsed");
                DestroyWindow(m_window);
                return 0;
            }
            if (wParam == kSavePollTimer) {
                poll_save_before_close();
                return 0;
            }
            break;
        case WM_CLOSE:
            request_close();
            return 0;
        case WM_DESTROY:
            m_server.stop();
            KillTimer(m_window, kSmokeTimer);
            KillTimer(m_window, kSmokeReadyTimer);
            KillTimer(m_window, kSavePollTimer);
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(m_window, message, wParam, lParam);
    }

    void set_bounds() {
        if (!m_controller)
            return;
        RECT client{};
        GetClientRect(m_window, &client);
        set_bounds(client.right - client.left, client.bottom - client.top);
    }

    void set_bounds(int width, int height) {
        if (!m_controller)
            return;
        RECT bounds{0, 0, std::max(0, width), std::max(0, height)};
        m_controller->put_Bounds(bounds);
        if (m_status != nullptr)
            MoveWindow(m_status, 24, 24, std::max(0, width - 48), 80, TRUE);
    }

    void create_menu() {
        m_menu = CreateMenu();
        HMENU fileMenu = CreatePopupMenu();
        if (m_menu == nullptr || fileMenu == nullptr) {
            m_log.line("shell: could not create menu");
            return;
        }
        AppendMenuW(fileMenu, MF_STRING, kCommandOpenAsset,
                    L"&Open Asset...\tCtrl+O");
        AppendMenuW(fileMenu, MF_STRING, kCommandOpenBrowser,
                    L"Open in &Browser\tCtrl+Shift+B");
        AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu, MF_STRING, kCommandExit, L"E&xit");
        AppendMenuW(m_menu, MF_POPUP,
                    reinterpret_cast<UINT_PTR>(fileMenu), L"&File");
        SetMenu(m_window, m_menu);

        ACCEL accelerators[2]{};
        accelerators[0].fVirt =
            static_cast<BYTE>(FVIRTKEY | FCONTROL);
        accelerators[0].key = 'O';
        accelerators[0].cmd = kCommandOpenAsset;
        accelerators[1].fVirt =
            static_cast<BYTE>(FVIRTKEY | FCONTROL | FSHIFT);
        accelerators[1].key = 'B';
        accelerators[1].cmd = kCommandOpenBrowser;
        m_accelerators = CreateAcceleratorTableW(accelerators, 2);
    }

    void open_asset_dialog() {
        if (!m_webview) {
            MessageBoxW(
                m_window,
                L"Open Asset is available in the WebView2 window.\n"
                L"In external-browser mode, enter the path in the editor.",
                L"insys editor", MB_OK | MB_ICONINFORMATION);
            return;
        }

        ComPtr<IFileOpenDialog> dialog;
        HRESULT result = CoCreateInstance(
            CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog));
        if (FAILED(result) || !dialog) {
            MessageBoxW(m_window, L"Could not create the file dialog.",
                        L"insys editor", MB_OK | MB_ICONERROR);
            return;
        }

        COMDLG_FILTERSPEC filters[] = {
            {L"Input action assets", L"*.json;*.insysb"},
            {L"JSON assets", L"*.json"},
            {L"Binary assets", L"*.insysb"},
            {L"All files", L"*.*"},
        };
        dialog->SetFileTypes(ARRAYSIZE(filters), filters);
        dialog->SetTitle(L"Open input actions asset");
        m_log.line("shell: showing open asset dialog");

        FILEOPENDIALOGOPTIONS options{};
        if (SUCCEEDED(dialog->GetOptions(&options))) {
            dialog->SetOptions(options | FOS_FILEMUSTEXIST |
                               FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
        }

        result = dialog->Show(m_window);
        if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
            return;
        if (FAILED(result)) {
            MessageBoxW(m_window, L"Could not open the file dialog.",
                        L"insys editor", MB_OK | MB_ICONERROR);
            return;
        }

        ComPtr<IShellItem> item;
        result = dialog->GetResult(&item);
        if (FAILED(result) || !item)
            return;

        PWSTR rawPath = nullptr;
        result = item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
        std::wstring path = rawPath != nullptr ? rawPath : L"";
        if (rawPath != nullptr)
            CoTaskMemFree(rawPath);
        if (FAILED(result) || path.empty())
            return;

        const std::wstring message = L"\"" + json_escape(path) + L"\"";
        result = m_webview->PostWebMessageAsJson(message.c_str());
        if (FAILED(result)) {
            const std::string error =
                "Could not send the selected path to the editor: " +
                hresult_text(result);
            MessageBoxW(m_window, from_utf8(error).c_str(), L"insys editor",
                        MB_OK | MB_ICONERROR);
            return;
        }
        m_log.line("shell: selected asset " + to_utf8(path));
        if (m_controller)
            m_controller->MoveFocus(
                COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }

    bool open_in_browser(std::string_view reason, bool reportFailure = true) {
        m_log.line("shell: opening external browser: " + std::string(reason));
        const std::wstring url = from_utf8(m_url);
        const HINSTANCE launched =
            m_config.browserPath.empty()
                ? ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr,
                                SW_SHOWNORMAL)
                : ShellExecuteW(nullptr, L"open",
                                m_config.browserPath.c_str(), url.c_str(),
                                nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(launched) <= 32) {
            m_log.line("shell: external browser launch failed: " +
                       std::to_string(
                           reinterpret_cast<INT_PTR>(launched)));
            if (reportFailure) {
                const std::string message =
                    "Could not open the editor in an external browser.\n\n" +
                    redact_url(m_url);
                MessageBoxW(m_window, from_utf8(message).c_str(),
                            L"insys editor",
                            MB_OK | MB_ICONERROR);
            }
            return false;
        }
        m_browserOpened = true;
        return true;
    }

    void close_now() {
        m_allowClose = true;
        DestroyWindow(m_window);
    }

    void request_close() {
        if (m_allowClose || m_config.smokeMs != 0 || !m_webview) {
            close_now();
            return;
        }
        if (m_savingBeforeClose || m_closeCheckInFlight)
            return;
        m_closeCheckInFlight = true;
        m_closeQueryHandler = Callback<
            ICoreWebView2ExecuteScriptCompletedHandler>(
            [this](HRESULT result, LPCWSTR json) -> HRESULT {
                m_closeCheckInFlight = false;
                if (FAILED(result) || json == nullptr) {
                    m_log.line("shell: close probe failed " +
                               hresult_text(result));
                    close_now();
                    return S_OK;
                }
                const int state =
                    static_cast<int>(wcstol(json, nullptr, 10));
                const bool assetDirty = (state & 1) != 0;
                const bool rawDirty = (state & 2) != 0;
                const bool autoSave = (state & 4) != 0;
                if (!assetDirty && !rawDirty) {
                    close_now();
                    return S_OK;
                }
                if (autoSave && assetDirty && !rawDirty) {
                    save_before_close();
                    return S_OK;
                }
                if (rawDirty) {
                    const int choice = MessageBoxW(
                        m_window,
                        L"The JSON editor has unapplied changes.\n\n"
                        L"Close without saving these changes?",
                        L"insys editor", MB_YESNO | MB_ICONWARNING);
                    if (choice == IDYES)
                        close_now();
                    return S_OK;
                }
                const int choice = MessageBoxW(
                    m_window,
                    L"Do you want to save the changes you made in this "
                    L"asset?\n\n"
                    L"Yes: save and close\n"
                    L"No: close without saving\n"
                    L"Cancel: keep editing\n\n"
                    L"Your changes will be lost if you do not save them.",
                    L"insys editor", MB_YESNOCANCEL | MB_ICONWARNING);
                if (choice == IDNO) {
                    close_now();
                } else if (choice == IDYES) {
                    save_before_close();
                }
                return S_OK;
            });
        const HRESULT result = m_webview->ExecuteScript(
            L"(() => {"
            L"const app = document.querySelector('.app');"
            L"if (!app) return 0;"
            L"return (app.dataset.dirty === 'true' ? 1 : 0) +"
            L"(app.dataset.rawDirty === 'true' ? 2 : 0) +"
            L"(app.dataset.autoSave === 'true' ? 4 : 0);"
            L"})()",
            m_closeQueryHandler.Get());
        if (FAILED(result)) {
            m_closeCheckInFlight = false;
            m_log.line("shell: close probe failed " + hresult_text(result));
            close_now();
        }
    }

    void save_before_close() {
        m_log.line("shell: saving before close");
        m_savingBeforeClose = true;
        m_saveClickHandler = Callback<
            ICoreWebView2ExecuteScriptCompletedHandler>(
            [this](HRESULT result, LPCWSTR json) -> HRESULT {
                if (FAILED(result) || json == nullptr ||
                    wcscmp(json, L"true") != 0) {
                    m_savingBeforeClose = false;
                    m_log.line("shell: save before close could not start");
                    const int choice = MessageBoxW(
                        m_window,
                        L"The editor could not start a save. Close without "
                        L"saving your changes?",
                        L"insys editor", MB_YESNO | MB_ICONWARNING);
                    if (choice == IDYES)
                        close_now();
                    return S_OK;
                }
                m_saveStartedAt = std::chrono::steady_clock::now();
                m_savePollInFlight = false;
                SetTimer(m_window, kSavePollTimer, 100, nullptr);
                return S_OK;
            });
        const HRESULT result = m_webview->ExecuteScript(
            L"(() => {"
            L"const button = Array.from(document.querySelectorAll('button'))"
            L".find((candidate) => candidate.textContent.trim() === 'Save');"
            L"if (!button || button.disabled) return false;"
            L"button.click();"
            L"return true;"
            L"})()",
            m_saveClickHandler.Get());
        if (FAILED(result)) {
            m_savingBeforeClose = false;
            m_log.line("shell: save before close could not start " +
                       hresult_text(result));
            MessageBoxW(
                m_window,
                L"The editor could not start a save. The window will stay "
                L"open.",
                L"insys editor", MB_OK | MB_ICONERROR);
        }
    }

    void poll_save_before_close() {
        if (!m_savingBeforeClose) {
            KillTimer(m_window, kSavePollTimer);
            return;
        }
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_saveStartedAt);
        if (elapsed.count() > 5000) {
            fail_save_before_close("timed out");
            return;
        }
        if (m_savePollInFlight)
            return;
        m_savePollInFlight = true;
        m_savePollHandler = Callback<
            ICoreWebView2ExecuteScriptCompletedHandler>(
            [this](HRESULT result, LPCWSTR json) -> HRESULT {
                m_savePollInFlight = false;
                if (!m_savingBeforeClose)
                    return S_OK;
                if (FAILED(result) || json == nullptr) {
                    fail_save_before_close("probe failed");
                    return S_OK;
                }
                if (wcscmp(json, L"true") != 0) {
                    KillTimer(m_window, kSavePollTimer);
                    m_savingBeforeClose = false;
                    m_log.line("shell: saved before close");
                    close_now();
                }
                return S_OK;
            });
        const HRESULT result = m_webview->ExecuteScript(
            L"(() => {"
            L"const app = document.querySelector('.app');"
            L"return app ? (app.dataset.dirty === 'true' ||"
            L"app.dataset.rawDirty === 'true') : false;"
            L"})()",
            m_savePollHandler.Get());
        if (FAILED(result)) {
            m_savePollInFlight = false;
            fail_save_before_close("probe failed");
        }
    }

    void fail_save_before_close(std::string_view reason) {
        KillTimer(m_window, kSavePollTimer);
        m_savingBeforeClose = false;
        m_savePollInFlight = false;
        m_log.line("shell: save before close failed: " +
                   std::string(reason));
        MessageBoxW(
            m_window,
            L"The changes could not be saved, so the editor will stay open.",
            L"insys editor", MB_OK | MB_ICONERROR);
    }

    void start_webview() {
        std::filesystem::path userData =
            m_config.userDataDir.empty()
                ? default_user_data_dir()
                : std::filesystem::path(m_config.userDataDir);
        std::error_code error;
        std::filesystem::create_directories(userData, error);
        if (error) {
            fallback_to_browser("cannot create WebView2 user data directory");
            return;
        }
        ComPtr<ICoreWebView2EnvironmentOptions> environmentOptions;
        if (m_config.remoteDebuggingPort != 0) {
            ComPtr<CoreWebView2EnvironmentOptions> concreteOptions =
                Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
            if (!concreteOptions) {
                fallback_to_browser(
                    "could not allocate WebView2 environment options");
                return;
            }
            const std::wstring arguments =
                L"--remote-debugging-port=" +
                std::to_wstring(m_config.remoteDebuggingPort);
            concreteOptions->put_AdditionalBrowserArguments(arguments.c_str());
            const HRESULT asResult = concreteOptions.As(&environmentOptions);
            if (FAILED(asResult)) {
                fallback_to_browser(
                    "could not expose WebView2 environment options " +
                    hresult_text(asResult));
                return;
            }
            m_log.line("shell: WebView2 remote debugging enabled on port " +
                       std::to_string(m_config.remoteDebuggingPort));
        }
        m_log.line("shell: creating WebView2 environment");
        m_environmentCompletedHandler = Callback<
            ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* environment)
                -> HRESULT {
                if (FAILED(result) || environment == nullptr) {
                    fallback_to_browser("CreateCoreWebView2EnvironmentWithOptions failed " +
                                        hresult_text(result));
                    return S_OK;
                }
                m_environment = environment;
                m_log.line("shell: WebView2 environment ready");
                m_controllerCompletedHandler = Callback<
                    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this](HRESULT controllerResult,
                           ICoreWebView2Controller* controller) -> HRESULT {
                        if (FAILED(controllerResult) || controller == nullptr) {
                            fallback_to_browser(
                                "CreateCoreWebView2Controller failed " +
                                hresult_text(controllerResult));
                            return S_OK;
                        }
                        m_controller = controller;
                        controller->get_CoreWebView2(&m_webview);
                        if (!m_webview) {
                            fallback_to_browser(
                                "WebView2 controller has no CoreWebView2");
                            return S_OK;
                        }
                        configure_webview();
                        m_acceleratorKeyPressedHandler = Callback<
                            ICoreWebView2AcceleratorKeyPressedEventHandler>(
                            [this](ICoreWebView2Controller*,
                                   ICoreWebView2AcceleratorKeyPressedEventArgs*
                                       args) -> HRESULT {
                                COREWEBVIEW2_KEY_EVENT_KIND kind =
                                    COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN;
                                args->get_KeyEventKind(&kind);
                                if (kind !=
                                        COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
                                    kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
                                    return S_OK;
                                }
                                UINT key = 0;
                                args->get_VirtualKey(&key);
                                const bool control =
                                    (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                                const bool shift =
                                    (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                                if (control && !shift && key == 'O') {
                                    args->put_Handled(TRUE);
                                    open_asset_dialog();
                                } else if (control && shift && key == 'B') {
                                    args->put_Handled(TRUE);
                                    open_in_browser(
                                        "requested by keyboard shortcut");
                                }
                                return S_OK;
                            });
                        m_controller->add_AcceleratorKeyPressed(
                            m_acceleratorKeyPressedHandler.Get(),
                            &m_acceleratorKeyPressedToken);
                        set_bounds();
                        controller->put_IsVisible(TRUE);
                        m_log.line("shell: WebView2 controller ready");
                        m_webview->Navigate(from_utf8(m_url).c_str());
                        return S_OK;
                    });
                m_environment->CreateCoreWebView2Controller(
                    m_window, m_controllerCompletedHandler.Get());
                return S_OK;
            });
        const std::wstring userDataText = userData.wstring();
        const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, userDataText.c_str(), environmentOptions.Get(),
            m_environmentCompletedHandler.Get());
        if (FAILED(result)) {
            fallback_to_browser("CreateCoreWebView2EnvironmentWithOptions failed " +
                                hresult_text(result));
        }
    }

    void configure_webview() {
        ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(m_webview->get_Settings(&settings)) && settings) {
            settings->put_AreDevToolsEnabled(m_config.devtools ? TRUE : FALSE);
            settings->put_AreDefaultContextMenusEnabled(
                m_config.devtools ? TRUE : FALSE);
            settings->put_AreDefaultScriptDialogsEnabled(TRUE);
            settings->put_IsStatusBarEnabled(FALSE);
        }

        m_navigationStartingHandler = Callback<
            ICoreWebView2NavigationStartingEventHandler>(
            [this](ICoreWebView2*,
                   ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR raw = nullptr;
                args->get_Uri(&raw);
                const std::wstring uri = raw != nullptr ? raw : L"";
                if (raw != nullptr)
                    CoTaskMemFree(raw);
                if (uri.rfind(m_origin, 0) != 0) {
                    m_log.line("shell: blocked navigation to " + redact_url(to_utf8(uri)));
                    args->put_Cancel(TRUE);
                } else {
                    m_log.line("shell: navigation started " + redact_url(to_utf8(uri)));
                }
                return S_OK;
            });
        m_webview->add_NavigationStarting(
            m_navigationStartingHandler.Get(), &m_navigationStartingToken);

        m_navigationCompletedHandler = Callback<
            ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2*,
                   ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                BOOL success = FALSE;
                args->get_IsSuccess(&success);
                m_log.line(success ? "shell: navigation completed"
                                   : "shell: navigation failed");
                if (success) {
                    if (m_status != nullptr)
                        ShowWindow(m_status, SW_HIDE);
                    m_controller->MoveFocus(
                        COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                    m_executeScriptHandler = Callback<
                        ICoreWebView2ExecuteScriptCompletedHandler>(
                        [this](HRESULT scriptResult, LPCWSTR json) -> HRESULT {
                            m_log.line(
                                SUCCEEDED(scriptResult) && json != nullptr
                                    ? "shell: page state " + to_utf8(json)
                                    : "shell: page probe failed " +
                                          hresult_text(scriptResult));
                            return S_OK;
                        });
                    m_webview->ExecuteScript(
                        L"JSON.stringify({ready:document.readyState,"
                        L"root:document.querySelector('#root')?"
                        L"document.querySelector('#root').childElementCount:-1})",
                        m_executeScriptHandler.Get());
                    if (m_config.smokeMs != 0)
                        SetTimer(m_window, kSmokeReadyTimer, 750, nullptr);
                }
                return S_OK;
            });
        m_webview->add_NavigationCompleted(
            m_navigationCompletedHandler.Get(), &m_navigationCompletedToken);

        m_newWindowRequestedHandler = Callback<
            ICoreWebView2NewWindowRequestedEventHandler>(
            [this](ICoreWebView2*,
                   ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                LPWSTR raw = nullptr;
                args->get_Uri(&raw);
                if (raw != nullptr) {
                    m_log.line("shell: blocked popup " + redact_url(to_utf8(raw)));
                    CoTaskMemFree(raw);
                }
                args->put_Handled(TRUE);
                return S_OK;
            });
        m_webview->add_NewWindowRequested(
            m_newWindowRequestedHandler.Get(), &m_newWindowRequestedToken);

        m_documentTitleChangedHandler = Callback<
            ICoreWebView2DocumentTitleChangedEventHandler>(
            [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                LPWSTR raw = nullptr;
                sender->get_DocumentTitle(&raw);
                if (raw != nullptr) {
                    const std::wstring title = std::wstring(kWindowTitle) +
                                               L" - " + raw;
                    SetWindowTextW(m_window, title.c_str());
                    CoTaskMemFree(raw);
                }
                return S_OK;
            });
        m_webview->add_DocumentTitleChanged(
            m_documentTitleChangedHandler.Get(),
            &m_documentTitleChangedToken);

        m_windowCloseRequestedHandler = Callback<
            ICoreWebView2WindowCloseRequestedEventHandler>(
            [this](ICoreWebView2*, IUnknown*) -> HRESULT {
                PostMessageW(m_window, WM_CLOSE, 0, 0);
                return S_OK;
            });
        m_webview->add_WindowCloseRequested(
            m_windowCloseRequestedHandler.Get(),
            &m_windowCloseRequestedToken);
    }

    void fallback_to_browser(const std::string& reason) {
        if (m_fallbackStarted)
            return;
        m_fallbackStarted = true;
        m_log.line("shell: browser fallback: " + reason);
        if (!m_browserOpened &&
            !open_in_browser("WebView2 fallback: " + reason, false)) {
            const std::string message =
                "WebView2 could not start and the external browser could not "
                "be opened.\n\n" +
                reason + "\n\n" + redact_url(m_url);
            MessageBoxW(m_window, from_utf8(message).c_str(), L"insys editor",
                        MB_OK | MB_ICONERROR);
            return;
        }
        if (m_status != nullptr) {
            SetWindowTextW(
                m_status,
                L"WebView2 is unavailable; insys editor is open in an external "
                L"browser.\nClose this window to stop the local editor "
                L"server.");
            ShowWindow(m_status, SW_SHOW);
        }
        SetWindowTextW(m_window, L"insys editor - external browser");
    }

    HINSTANCE m_instance = nullptr;
    insys::editor::EditorServer& m_server;
    ShellConfig m_config;
    Log& m_log;
    HWND m_window = nullptr;
    HWND m_status = nullptr;
    HMENU m_menu = nullptr;
    HACCEL m_accelerators = nullptr;
    std::string m_url;
    std::wstring m_origin;
    bool m_shutdown = false;
    bool m_fallbackStarted = false;
    bool m_browserOpened = false;
    bool m_allowClose = false;
    bool m_closeCheckInFlight = false;
    bool m_savingBeforeClose = false;
    bool m_savePollInFlight = false;
    std::chrono::steady_clock::time_point m_saveStartedAt;

    ComPtr<ICoreWebView2Environment> m_environment;
    ComPtr<ICoreWebView2Controller> m_controller;
    ComPtr<ICoreWebView2> m_webview;
    ComPtr<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>
        m_environmentCompletedHandler;
    ComPtr<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>
        m_controllerCompletedHandler;
    ComPtr<ICoreWebView2NavigationStartingEventHandler>
        m_navigationStartingHandler;
    ComPtr<ICoreWebView2NavigationCompletedEventHandler>
        m_navigationCompletedHandler;
    ComPtr<ICoreWebView2NewWindowRequestedEventHandler>
        m_newWindowRequestedHandler;
    ComPtr<ICoreWebView2DocumentTitleChangedEventHandler>
        m_documentTitleChangedHandler;
    ComPtr<ICoreWebView2WindowCloseRequestedEventHandler>
        m_windowCloseRequestedHandler;
    ComPtr<ICoreWebView2AcceleratorKeyPressedEventHandler>
        m_acceleratorKeyPressedHandler;
    ComPtr<ICoreWebView2ExecuteScriptCompletedHandler> m_executeScriptHandler;
    ComPtr<ICoreWebView2ExecuteScriptCompletedHandler> m_closeQueryHandler;
    ComPtr<ICoreWebView2ExecuteScriptCompletedHandler> m_saveClickHandler;
    ComPtr<ICoreWebView2ExecuteScriptCompletedHandler> m_savePollHandler;
    EventRegistrationToken m_navigationStartingToken{};
    EventRegistrationToken m_navigationCompletedToken{};
    EventRegistrationToken m_newWindowRequestedToken{};
    EventRegistrationToken m_documentTitleChangedToken{};
    EventRegistrationToken m_windowCloseRequestedToken{};
    EventRegistrationToken m_acceleratorKeyPressedToken{};
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    CommandLine config;
    std::wstring parseError;
    if (!parse_command_line(config, parseError)) {
        MessageBoxW(nullptr, parseError.c_str(), L"insys editor",
                    MB_OK | MB_ICONERROR);
        return 2;
    }
    if (config.help) {
        show_usage();
        return 0;
    }

    Log log;
    if (!config.shell.logPath.empty() && !log.open(config.shell.logPath)) {
        MessageBoxW(nullptr, L"Could not open --log file.", L"insys editor",
                    MB_OK | MB_ICONERROR);
        return 2;
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) {
        log.line("shell: CoInitializeEx failed " + hresult_text(comResult));
        MessageBoxW(nullptr, L"Could not initialize COM for WebView2.",
                    L"insys editor", MB_OK | MB_ICONERROR);
        return 1;
    }

    insys::editor::EditorServer server(std::move(config.server));
    std::string serverError;
    if (!server.start(&serverError)) {
        log.line("shell: editor server failed: " + serverError);
        MessageBoxW(nullptr, from_utf8(serverError).c_str(), L"insys editor",
                    MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    std::thread serverThread([&server]() { server.run(); });
    log.line("shell: editor server listening on 127.0.0.1:" +
             std::to_string(server.port()));

    int result = 0;
    {
        EditorWindow window(instance, server, config.shell, log);
        if (!window.create(showCommand)) {
            result = 1;
        } else {
            result = window.run();
        }
        window.shutdown();
    }

    server.stop();
    if (serverThread.joinable())
        serverThread.join();
    CoUninitialize();
    log.line("shell: exit");
    return result;
}
