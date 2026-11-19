#pragma once

#if defined(_WIN32)

#include <cstdint>
#include <functional>
#include <memory>

#include "insys/device_states.h"

namespace insys::backend {

class WindowsTouch {
public:
    using TouchCallback = std::function<void(const TouchscreenState&)>;

    WindowsTouch();
    ~WindowsTouch();

    WindowsTouch(const WindowsTouch&) = delete;
    WindowsTouch& operator=(const WindowsTouch&) = delete;

    bool initialize(void* nativeWindow);
    void shutdown();
    bool enabled() const;

    void set_touch_callback(TouchCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace insys::backend

#endif
