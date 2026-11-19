#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "insys/backend/evdev_reader.h"

namespace insys::backend::evdev {

class EvdevCaptureSession {
public:
    using ReadFunction =
        std::function<std::size_t(std::uint8_t* buffer, std::size_t capacity)>;

    bool add_device(std::string path, EvdevDeviceType type, ReadFunction read);
    bool remove_device(std::string_view path);
    void clear();

    std::size_t poll();
    std::vector<std::string> take_dirty_devices();

    const EvdevDeviceState* device_state(std::string_view path) const;
    EvdevDeviceState* device_state_mutable(std::string_view path);
    std::size_t device_count() const { return m_devices.size(); }

private:
    struct Device {
        std::string path;
        EvdevDeviceState state;
        EvdevEventDecoder decoder;
        ReadFunction read;
    };

    Device* find_device(std::string_view path);

    std::vector<Device> m_devices;
    std::vector<std::string> m_dirtyDevices;
};

} // namespace insys::backend::evdev
