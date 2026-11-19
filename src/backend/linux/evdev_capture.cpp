#include "insys/backend/evdev_capture.h"

#include <algorithm>
#include <array>
#include <utility>

namespace insys::backend::evdev {

bool EvdevCaptureSession::add_device(std::string path, EvdevDeviceType type,
                                     ReadFunction read) {
    if (path.empty() || !read || find_device(path) != nullptr)
        return false;
    m_devices.push_back(Device{std::move(path), EvdevDeviceState(type),
                               EvdevEventDecoder{}, std::move(read)});
    return true;
}

bool EvdevCaptureSession::remove_device(std::string_view path) {
    const auto it = std::find_if(m_devices.begin(), m_devices.end(),
                                 [path](const Device& device) {
                                     return device.path == path;
                                 });
    if (it == m_devices.end())
        return false;
    m_devices.erase(it);
    m_dirtyDevices.erase(
        std::remove(m_dirtyDevices.begin(), m_dirtyDevices.end(), path),
        m_dirtyDevices.end());
    return true;
}

void EvdevCaptureSession::clear() {
    m_devices.clear();
    m_dirtyDevices.clear();
}

std::size_t EvdevCaptureSession::poll() {
    std::size_t eventCount = 0;
    std::array<std::uint8_t, 256> buffer{};

    for (Device& device : m_devices) {
        for (;;) {
            const std::size_t bytes = device.read(buffer.data(), buffer.size());
            if (bytes == 0)
                break;
            const std::size_t consumed = std::min(bytes, buffer.size());
            device.decoder.feed(buffer.data(), consumed);

            for (const EvdevEvent& event : device.decoder.events()) {
                ++eventCount;
                if (event.type == kEvSyn && event.code == kSynReport) {
                    if (device.state.end_frame() &&
                        std::find(m_dirtyDevices.begin(), m_dirtyDevices.end(),
                                  device.path) == m_dirtyDevices.end()) {
                        m_dirtyDevices.push_back(device.path);
                    }
                } else {
                    device.state.process(event.type, event.code, event.value);
                }
            }
            device.decoder.clear_events();
        }
    }

    return eventCount;
}

std::vector<std::string> EvdevCaptureSession::take_dirty_devices() {
    std::vector<std::string> dirty = std::move(m_dirtyDevices);
    m_dirtyDevices.clear();
    return dirty;
}

const EvdevDeviceState* EvdevCaptureSession::device_state(std::string_view path) const {
    const auto it = std::find_if(m_devices.begin(), m_devices.end(),
                                 [path](const Device& device) {
                                     return device.path == path;
                                 });
    return it == m_devices.end() ? nullptr : &it->state;
}

EvdevDeviceState* EvdevCaptureSession::device_state_mutable(std::string_view path) {
    const auto it = std::find_if(m_devices.begin(), m_devices.end(),
                                 [path](const Device& device) {
                                     return device.path == path;
                                 });
    return it == m_devices.end() ? nullptr : &it->state;
}

EvdevCaptureSession::Device* EvdevCaptureSession::find_device(std::string_view path) {
    const auto it = std::find_if(m_devices.begin(), m_devices.end(),
                                 [path](const Device& device) {
                                     return device.path == path;
                                 });
    return it == m_devices.end() ? nullptr : &*it;
}

} // namespace insys::backend::evdev
