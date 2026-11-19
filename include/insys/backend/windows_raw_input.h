#pragma once

#if defined(_WIN32)

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace insys::backend {

class WindowsRawInput {
public:
    using MouseDeltaCallback =
        std::function<void(std::intptr_t device, int32_t deltaX, int32_t deltaY)>;
    using DeviceRemovedCallback = std::function<void(std::intptr_t device)>;

    WindowsRawInput();
    ~WindowsRawInput();

    WindowsRawInput(const WindowsRawInput&) = delete;
    WindowsRawInput& operator=(const WindowsRawInput&) = delete;

    bool initialize(void* nativeWindow);
    void shutdown();
    bool enabled() const;

    void set_mouse_delta_callback(MouseDeltaCallback callback);
    void set_device_removed_callback(DeviceRemovedCallback callback);

    std::string device_name(std::intptr_t device) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace insys::backend

#endif
