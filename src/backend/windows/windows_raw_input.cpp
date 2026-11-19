#include "insys/backend/windows_raw_input.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <unordered_map>
#include <utility>
#include <vector>

namespace insys::backend {

struct WindowsRawInput::Impl {
    HWND window = nullptr;
    WNDPROC previousWndProc = nullptr;
    bool registered = false;
    std::vector<BYTE> buffer;
    MouseDeltaCallback mouseDelta;
    DeviceRemovedCallback deviceRemoved;

    static std::unordered_map<HWND, Impl*> windows;

    static std::string query_device_name(HANDLE device) {
        if (device == nullptr)
            return "Raw Mouse";

        UINT size = 0;
        GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &size);
        if (size == 0)
            return "Raw Mouse";

        std::wstring wideName(size, L'\0');
        const UINT result =
            GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, wideName.data(), &size);
        if (result == static_cast<UINT>(-1) || size == 0)
            return "Raw Mouse";

        wideName.resize(size > 0 ? size - 1 : 0);
        if (wideName.empty())
            return "Raw Mouse";

        const int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wideName.c_str(),
                                                 static_cast<int>(wideName.size()), nullptr, 0,
                                                 nullptr, nullptr);
        if (utf8Size <= 0)
            return "Raw Mouse";

        std::string name(static_cast<std::size_t>(utf8Size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wideName.c_str(), static_cast<int>(wideName.size()),
                            name.data(), utf8Size, nullptr, nullptr);
        return name;
    }

    void handle_input(HRAWINPUT rawInput) {
        UINT size = 0;
        if (GetRawInputData(rawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 ||
            size == 0) {
            return;
        }

        buffer.resize(size);
        const UINT read =
            GetRawInputData(rawInput, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER));
        if (read != size || buffer.size() < sizeof(RAWINPUT))
            return;

        const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer.data());
        if (raw->header.dwType != RIM_TYPEMOUSE)
            return;

        const RAWMOUSE& mouse = raw->data.mouse;
        if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0)
            return;
        if (mouse.lLastX == 0 && mouse.lLastY == 0)
            return;

        if (mouseDelta) {
            mouseDelta(reinterpret_cast<std::intptr_t>(raw->header.hDevice),
                       static_cast<int32_t>(mouse.lLastX),
                       static_cast<int32_t>(mouse.lLastY));
        }
    }

    void handle_device_change(WPARAM change, LPARAM device) {
        if (change != GIDC_REMOVAL)
            return;
        if (deviceRemoved)
            deviceRemoved(static_cast<std::intptr_t>(device));
    }

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        const auto it = windows.find(hwnd);
        if (it == windows.end())
            return DefWindowProcW(hwnd, message, wParam, lParam);

        Impl* self = it->second;
        if (message == WM_INPUT) {
            self->handle_input(reinterpret_cast<HRAWINPUT>(lParam));
            return self->previousWndProc != nullptr
                       ? CallWindowProcW(self->previousWndProc, hwnd, message, wParam, lParam)
                       : DefWindowProcW(hwnd, message, wParam, lParam);
        }
        if (message == WM_INPUT_DEVICE_CHANGE) {
            self->handle_device_change(wParam, lParam);
            return 0;
        }

        return self->previousWndProc != nullptr
                   ? CallWindowProcW(self->previousWndProc, hwnd, message, wParam, lParam)
                   : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool install(HWND target) {
        window = target;

        SetLastError(0);
        const LONG_PTR previous = SetWindowLongPtrW(
            window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Impl::window_proc));
        if (previous == 0 && GetLastError() != 0)
            return false;
        previousWndProc = reinterpret_cast<WNDPROC>(previous);
        windows[window] = this;

        RAWINPUTDEVICE device{};
        device.usUsagePage = 0x01;
        device.usUsage = 0x02;
        device.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
        device.hwndTarget = window;
        if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
            windows.erase(window);
            SetWindowLongPtrW(window, GWLP_WNDPROC,
                             reinterpret_cast<LONG_PTR>(previousWndProc));
            previousWndProc = nullptr;
            window = nullptr;
            return false;
        }

        registered = true;
        return true;
    }

    void uninstall() {
        if (window != nullptr) {
            RAWINPUTDEVICE device{};
            device.usUsagePage = 0x01;
            device.usUsage = 0x02;
            device.dwFlags = RIDEV_REMOVE;
            device.hwndTarget = nullptr;
            RegisterRawInputDevices(&device, 1, sizeof(device));

            windows.erase(window);
            if (previousWndProc != nullptr) {
                SetWindowLongPtrW(window, GWLP_WNDPROC,
                                 reinterpret_cast<LONG_PTR>(previousWndProc));
                previousWndProc = nullptr;
            }
            window = nullptr;
        }
        registered = false;
    }
};

std::unordered_map<HWND, WindowsRawInput::Impl*> WindowsRawInput::Impl::windows;

WindowsRawInput::WindowsRawInput() : m_impl(std::make_unique<Impl>()) {}

WindowsRawInput::~WindowsRawInput() {
    shutdown();
}

bool WindowsRawInput::initialize(void* nativeWindow) {
    if (nativeWindow == nullptr)
        return false;

    shutdown();
    return m_impl->install(static_cast<HWND>(nativeWindow));
}

void WindowsRawInput::shutdown() {
    if (m_impl)
        m_impl->uninstall();
}

bool WindowsRawInput::enabled() const {
    return m_impl != nullptr && m_impl->registered;
}

void WindowsRawInput::set_mouse_delta_callback(MouseDeltaCallback callback) {
    if (m_impl)
        m_impl->mouseDelta = std::move(callback);
}

void WindowsRawInput::set_device_removed_callback(DeviceRemovedCallback callback) {
    if (m_impl)
        m_impl->deviceRemoved = std::move(callback);
}

std::string WindowsRawInput::device_name(std::intptr_t device) const {
    return Impl::query_device_name(reinterpret_cast<HANDLE>(device));
}

} // namespace insys::backend

#endif
