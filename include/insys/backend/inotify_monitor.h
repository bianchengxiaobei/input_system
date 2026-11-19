#pragma once

#include <cstddef>
#include <cstdint>

#include "insys/backend/inotify_reader.h"

namespace insys::backend::linux_evdev {

class InputHotplugMonitor {
public:
    bool feed(const std::uint8_t* data, std::size_t size);
    bool feed(const InotifyEvent& event);
    void reset() { m_decoder.reset(); }

private:
    InotifyEventDecoder m_decoder;
};

bool is_input_device_change(const InotifyEvent& event);

} // namespace insys::backend::linux_evdev
