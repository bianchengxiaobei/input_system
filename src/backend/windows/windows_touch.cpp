#include "insys/backend/windows_touch.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>
#include <vector>

namespace insys::backend {

struct WindowsTouch::Impl {
    struct Track {
        DWORD id = 0;
        bool active = false;
        Vector2 last;
        Vector2 start;
    };

    HWND window = nullptr;
    WNDPROC previousWndProc = nullptr;
    bool registered = false;
    TouchscreenState state{};
    std::array<Track, kMaxTouches> tracks{};
    TouchCallback callback;

    static std::unordered_map<HWND, Impl*> windows;

    static Vector2 to_client(HWND hwnd, const TOUCHINPUT& touch) {
        const LONG wholeX = touch.x / 100;
        const LONG wholeY = touch.y / 100;
        POINT point{wholeX, wholeY};
        if (hwnd != nullptr)
            ScreenToClient(hwnd, &point);

        const float fractionX = static_cast<float>(touch.x % 100) / 100.0f;
        const float fractionY = static_cast<float>(touch.y % 100) / 100.0f;
        return {static_cast<float>(point.x) + fractionX,
                static_cast<float>(point.y) + fractionY};
    }

    int find_track(DWORD id) const {
        for (int i = 0; i < kMaxTouches; ++i) {
            if (tracks[static_cast<std::size_t>(i)].active &&
                tracks[static_cast<std::size_t>(i)].id == id) {
                return i;
            }
        }
        return -1;
    }

    int allocate_track(DWORD id) {
        for (int i = 0; i < kMaxTouches; ++i) {
            Track& track = tracks[static_cast<std::size_t>(i)];
            if (!track.active) {
                track = {};
                track.id = id;
                track.active = true;
                return i;
            }
        }
        return -1;
    }

    void handle_touch(HTOUCHINPUT handle, UINT count) {
        if (handle == nullptr || count == 0)
            return;

        std::vector<TOUCHINPUT> inputs(count);
        if (!GetTouchInputInfo(handle, count, inputs.data(), sizeof(TOUCHINPUT))) {
            CloseTouchInputHandle(handle);
            return;
        }

        for (int i = 0; i < kMaxTouches; ++i) {
            if (tracks[static_cast<std::size_t>(i)].active)
                state.touches[static_cast<std::size_t>(i)].delta = {};
        }

        bool changed = false;
        for (const TOUCHINPUT& input : inputs) {
            const bool down = (input.dwFlags & TOUCHEVENTF_DOWN) != 0;
            const bool up = (input.dwFlags & TOUCHEVENTF_UP) != 0;
            const bool move = (input.dwFlags & TOUCHEVENTF_MOVE) != 0;

            int slot = find_track(input.dwID);
            if (down && slot < 0)
                slot = allocate_track(input.dwID);
            if (slot < 0)
                continue;

            Track& track = tracks[static_cast<std::size_t>(slot)];
            TouchPointState& point = state.touches[static_cast<std::size_t>(slot)];
            const Vector2 position = to_client(window, input);

            if (down) {
                point = {};
                point.position = position;
                point.startPosition = position;
                point.delta = {};
                point.phase = static_cast<float>(TouchPhase::Began);
                point.press = 1.0f;
                point.tapCount = 1.0f;
                track.last = position;
                track.start = position;
            } else {
                point.position = position;
                point.startPosition = track.start;
                point.delta = position - track.last;
                if (up) {
                    point.phase = static_cast<float>(TouchPhase::Ended);
                    point.press = 0.0f;
                    point.tapCount = 0.0f;
                } else if (move) {
                    point.phase = static_cast<float>(TouchPhase::Moved);
                    point.press = 1.0f;
                    point.tapCount = 1.0f;
                } else {
                    point.phase = static_cast<float>(TouchPhase::Stationary);
                    point.press = 1.0f;
                    point.tapCount = 1.0f;
                }
                track.last = position;
            }

            changed = true;
            if (up)
                track.active = false;
        }

        CloseTouchInputHandle(handle);
        if (changed && callback)
            callback(state);
    }

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wParam,
                                        LPARAM lParam) {
        const auto it = windows.find(hwnd);
        if (it == windows.end())
            return DefWindowProcW(hwnd, message, wParam, lParam);

        Impl* self = it->second;
        if (message == WM_TOUCH) {
            self->handle_touch(reinterpret_cast<HTOUCHINPUT>(lParam),
                               static_cast<UINT>(LOWORD(wParam)));
            return 0;
        }

        return self->previousWndProc != nullptr
                   ? CallWindowProcW(self->previousWndProc, hwnd, message, wParam,
                                     lParam)
                   : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool install(HWND target) {
        window = target;
        if (!RegisterTouchWindow(window, TWF_WANTPALM)) {
            window = nullptr;
            return false;
        }

        SetLastError(0);
        const LONG_PTR previous = SetWindowLongPtrW(
            window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Impl::window_proc));
        if (previous == 0 && GetLastError() != 0) {
            UnregisterTouchWindow(window);
            window = nullptr;
            return false;
        }
        previousWndProc = reinterpret_cast<WNDPROC>(previous);
        windows[window] = this;
        registered = true;
        return true;
    }

    void uninstall() {
        if (window != nullptr) {
            UnregisterTouchWindow(window);
            windows.erase(window);
            if (previousWndProc != nullptr) {
                SetWindowLongPtrW(window, GWLP_WNDPROC,
                                 reinterpret_cast<LONG_PTR>(previousWndProc));
                previousWndProc = nullptr;
            }
            window = nullptr;
        }
        state = {};
        tracks = {};
        registered = false;
    }
};

std::unordered_map<HWND, WindowsTouch::Impl*> WindowsTouch::Impl::windows;

WindowsTouch::WindowsTouch() : m_impl(std::make_unique<Impl>()) {}
WindowsTouch::~WindowsTouch() {
    shutdown();
}

bool WindowsTouch::initialize(void* nativeWindow) {
    if (nativeWindow == nullptr)
        return false;
    shutdown();
    return m_impl->install(static_cast<HWND>(nativeWindow));
}

void WindowsTouch::shutdown() {
    if (m_impl)
        m_impl->uninstall();
}

bool WindowsTouch::enabled() const {
    return m_impl != nullptr && m_impl->registered;
}

void WindowsTouch::set_touch_callback(TouchCallback callback) {
    if (m_impl)
        m_impl->callback = std::move(callback);
}

} // namespace insys::backend

#endif

