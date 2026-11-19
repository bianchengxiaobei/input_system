#include "insys/backend/linux_evdev.h"

#if defined(__linux__)

#include <linux/input.h>
#include <sys/ioctl.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <string>
#include <unistd.h>

namespace insys::backend::linux_evdev {
namespace {

struct FileDescriptor {
    int value = -1;

    ~FileDescriptor() {
        if (value >= 0)
            ::close(value);
    }
};

bool query_capabilities(int fd, evdev::EvdevCapabilityBits& bits) {
    bits.types.assign((EV_CNT + 7) / 8, 0);
    bits.keys.assign((KEY_CNT + 7) / 8, 0);
    bits.relative.assign((REL_CNT + 7) / 8, 0);
    bits.absolute.assign((ABS_CNT + 7) / 8, 0);

    if (::ioctl(fd, EVIOCGBIT(0, bits.types.size()), bits.types.data()) < 0)
        return false;
    if (evdev::evdev_bit_is_set(bits.types, EV_KEY) &&
        ::ioctl(fd, EVIOCGBIT(EV_KEY, bits.keys.size()), bits.keys.data()) < 0) {
        return false;
    }
    if (evdev::evdev_bit_is_set(bits.types, EV_REL) &&
        ::ioctl(fd, EVIOCGBIT(EV_REL, bits.relative.size()), bits.relative.data()) < 0) {
        return false;
    }
    if (evdev::evdev_bit_is_set(bits.types, EV_ABS) &&
        ::ioctl(fd, EVIOCGBIT(EV_ABS, bits.absolute.size()), bits.absolute.data()) < 0) {
        return false;
    }
    return true;
}

} // namespace

std::vector<evdev::EvdevDeviceInfo> scan_devices() {
    std::vector<evdev::EvdevDeviceInfo> devices;
    DIR* directory = ::opendir("/dev/input");
    if (directory == nullptr)
        return devices;

    while (dirent* entry = ::readdir(directory)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0)
            continue;

        const std::string path = std::string("/dev/input/") + entry->d_name;
        const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;

        evdev::EvdevCapabilityBits bits;
        if (query_capabilities(fd, bits)) {
            const evdev::EvdevCapabilities capabilities =
                evdev::decode_evdev_capabilities(bits);
            const evdev::EvdevDeviceType type =
                evdev::classify_evdev_device(capabilities);
            if (type != evdev::EvdevDeviceType::Unknown)
                devices.push_back({path, type});
        }
        ::close(fd);
    }

    ::closedir(directory);
    return devices;
}

EvdevBackend::SourceFactory make_source_factory() {
    return [](const evdev::EvdevDeviceInfo& info) {
        if (info.path.empty())
            return evdev::EvdevCaptureSession::ReadFunction{};

        const int fd = ::open(info.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            return evdev::EvdevCaptureSession::ReadFunction{};

        auto descriptor = std::make_shared<FileDescriptor>();
        descriptor->value = fd;

        return evdev::EvdevCaptureSession::ReadFunction(
            [descriptor](std::uint8_t* buffer, std::size_t capacity) {
                if (buffer == nullptr || capacity < evdev::EvdevEventDecoder::kDefaultRecordSize)
                    return std::size_t{0};

                std::array<input_event, 64> events{};
                ssize_t bytes = 0;
                do {
                    bytes = ::read(descriptor->value, events.data(), sizeof(events));
                } while (bytes < 0 && errno == EINTR);
                if (bytes <= 0)
                    return std::size_t{0};

                std::size_t count = static_cast<std::size_t>(bytes) / sizeof(input_event);
                const std::size_t maxCount =
                    capacity / evdev::EvdevEventDecoder::kDefaultRecordSize;
                if (count > maxCount)
                    count = maxCount;

                for (std::size_t i = 0; i < count; ++i) {
                    evdev::encode_evdev_event(
                        buffer + i * evdev::EvdevEventDecoder::kDefaultRecordSize,
                        {events[i].type, events[i].code, events[i].value});
                }
                return count * evdev::EvdevEventDecoder::kDefaultRecordSize;
            });
    };
}

} // namespace insys::backend::linux_evdev

#else

namespace insys::backend::linux_evdev {

std::vector<evdev::EvdevDeviceInfo> scan_devices() {
    return {};
}

EvdevBackend::SourceFactory make_source_factory() {
    return {};
}

} // namespace insys::backend::linux_evdev

#endif
