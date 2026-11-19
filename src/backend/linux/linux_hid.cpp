#include "insys/backend/linux_hid.h"

#if defined(__linux__)

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/hid.h>
#include <linux/hidraw.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <linux/input.h>

#include "insys/backend/hid_backend.h"
#include "insys/backend/hid_report_queue.h"
#include "insys/backend/runtime.h"
#include "insys/hid/hid_gamepad.h"

namespace insys::backend {
namespace {

struct Candidate {
    std::string path;
    int fd = -1;
    hid::DeviceInfo info;
    uint16_t inputReportSize = 0;
    uint16_t featureReportSize = 0;
};

std::vector<std::string> list_hidraw_paths() {
    std::vector<std::string> paths;
    DIR* directory = ::opendir("/sys/class/hidraw");
    if (directory == nullptr)
        return paths;

    while (dirent* entry = ::readdir(directory)) {
        const std::string name = entry->d_name;
        if (name.rfind("hidraw", 0) == 0)
            paths.push_back("/dev/" + name);
    }
    ::closedir(directory);
    std::sort(paths.begin(), paths.end());
    return paths;
}

void close_fd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

bool query_device(const std::string& path, Candidate& out,
                  bool& permissionDenied) {
    permissionDenied = false;
    int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        permissionDenied = errno == EACCES || errno == EPERM;
        return false;
    }

    hidraw_devinfo rawInfo{};
    if (::ioctl(fd, HIDIOCGRAWINFO, &rawInfo) < 0) {
        close_fd(fd);
        return false;
    }

    int descriptorSize = 0;
    if (::ioctl(fd, HIDIOCGRDESCSIZE, &descriptorSize) < 0 ||
        descriptorSize <= 0) {
        close_fd(fd);
        return false;
    }

    hidraw_report_descriptor rawDescriptor{};
    if (descriptorSize > static_cast<int>(sizeof(rawDescriptor.value))) {
        close_fd(fd);
        return false;
    }
    rawDescriptor.size = static_cast<__u32>(descriptorSize);
    if (::ioctl(fd, HIDIOCGRDESC, &rawDescriptor) < 0) {
        close_fd(fd);
        return false;
    }

    std::vector<uint8_t> descriptor(rawDescriptor.value,
                                    rawDescriptor.value + rawDescriptor.size);
    hid::DeviceDescriptor parsed;
    if (!hid::parse_report_descriptor(descriptor.data(), descriptor.size(),
                                      parsed)) {
        close_fd(fd);
        return false;
    }
    if (parsed.usagePage != hid::UsagePage::GenericDesktop ||
        (parsed.usage != 0x04 && parsed.usage != 0x05 && parsed.usage != 0x08)) {
        close_fd(fd);
        return false;
    }

    const hid::ReportInfo* inputReport = parsed.primary_input_report();
    if (inputReport == nullptr || inputReport->size_in_bytes() == 0) {
        close_fd(fd);
        return false;
    }

    char productName[256]{};
    ::ioctl(fd, HIDIOCGRAWNAME(sizeof(productName)), productName);

    char uniqueName[256]{};
#ifdef HIDIOCGRAWUNIQ
    ::ioctl(fd, HIDIOCGRAWUNIQ(sizeof(uniqueName)), uniqueName);
#endif

    bool hasInputReportId = false;
    for (const hid::ReportInfo& report : parsed.reports) {
        if (report.type == hid::ReportType::Input && report.hasId) {
            hasInputReportId = true;
            break;
        }
    }

    Candidate candidate;
    candidate.path = path;
    candidate.fd = fd;
    candidate.inputReportSize =
        static_cast<uint16_t>(inputReport->size_in_bytes());
    candidate.featureReportSize =
        static_cast<uint16_t>(std::min<uint32_t>(parsed.featureReportSize,
                                                 0xFFFFu));

    hid::DeviceInfo& info = candidate.info;
    info.id = path;
    info.vendorId =
        static_cast<uint32_t>(static_cast<uint16_t>(rawInfo.vendor));
    info.productId =
        static_cast<uint32_t>(static_cast<uint16_t>(rawInfo.product));
    info.usagePage = static_cast<uint16_t>(parsed.usagePage);
    info.usage = static_cast<uint16_t>(parsed.usage);
    info.inputReportSize = candidate.inputReportSize;
    info.outputReportSize =
        static_cast<uint16_t>(std::min<uint32_t>(parsed.outputReportSize,
                                                 0xFFFFu));
    info.featureReportSize = candidate.featureReportSize;
    info.reportDataIncludesId = hasInputReportId;
    info.transport = rawInfo.bustype == BUS_BLUETOOTH
                         ? hid::HidTransport::Bluetooth
                         : rawInfo.bustype == BUS_USB
                               ? hid::HidTransport::Usb
                               : rawInfo.bustype == 0
                                     ? hid::HidTransport::Unknown
                                     : hid::HidTransport::Other;
    info.bluetooth = info.transport == hid::HidTransport::Bluetooth;
    info.bluetoothKnown =
        info.transport != hid::HidTransport::Unknown;
    info.reportDescriptor = std::move(descriptor);
    info.description.interfaceName = "HID";
    info.description.product = productName[0] != 0
                                   ? std::string(productName)
                                   : std::string("HID Device");
    info.description.serial =
        uniqueName[0] != 0 ? std::string(uniqueName) : path;
    info.description.version = "Linux";

    out = std::move(candidate);
    return true;
}

} // namespace

struct LinuxHid::Impl {
    struct Device {
        std::string id;
        int fd = -1;
        std::thread reader;
        std::atomic<bool> stop{false};
        std::atomic<bool> finished{false};
        uint16_t inputReportSize = 0;
        uint16_t featureReportSize = 0;
    };

    HidBackend* backend = nullptr;
    Runtime* runtime = nullptr;
    HidReportQueue queue;
    std::unordered_map<std::string, std::unique_ptr<Device>> devices;
    std::unordered_set<std::string> reportedPermissions;
    double nextRefresh = 0.0;
    bool running = false;

    void read_loop(Device& device) {
        std::vector<uint8_t> buffer(device.inputReportSize, 0);
        while (!device.stop.load()) {
            pollfd descriptor{};
            descriptor.fd = device.fd;
            descriptor.events = POLLIN;
            const int result = ::poll(&descriptor, 1, 100);
            if (result < 0) {
                if (errno == EINTR)
                    continue;
                if (backend != nullptr) {
                    backend->report_error(
                        HidErrorCode::ReadFailed, device.id,
                        "hidraw poll failed (errno " +
                            std::to_string(errno) + ")");
                }
                device.finished.store(true);
                break;
            }
            if (result == 0)
                continue;
            if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                if (backend != nullptr) {
                    backend->report_error(
                        HidErrorCode::ReadFailed, device.id,
                        "hidraw device hung up");
                }
                device.finished.store(true);
                break;
            }
            if ((descriptor.revents & POLLIN) == 0)
                continue;

            const ssize_t bytes = ::read(device.fd, buffer.data(), buffer.size());
            if (bytes > 0) {
                queue.push_report(device.id, buffer.data(),
                                  static_cast<std::size_t>(bytes));
            } else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                       errno != EINTR) {
                if (backend != nullptr) {
                    backend->report_error(
                        HidErrorCode::ReadFailed, device.id,
                        "hidraw read failed (errno " +
                            std::to_string(errno) + ")");
                }
                device.finished.store(true);
                break;
            }
        }
        device.finished.store(true);
    }

    void drain_queue() {
        if (backend == nullptr)
            return;
        queue.drain(*backend);
        const std::size_t dropped = queue.take_dropped_count();
        if (dropped != 0) {
            backend->report_error(
                HidErrorCode::ReportQueueOverflow, {},
                "dropped " + std::to_string(dropped) + " HID reports");
        }
    }

    void report_permission_denied(const std::string& path) {
        if (backend == nullptr || !reportedPermissions.insert(path).second)
            return;
        backend->report_error(
            HidErrorCode::PermissionDenied, path,
            "hidraw permission denied; add a udev rule granting the user "
            "access to this device");
    }

    void stop_device(Device& device) {
        device.stop.store(true);
        if (device.reader.joinable())
            device.reader.join();
        close_fd(device.fd);
    }

    bool start_device(const std::string& path) {
        if (backend == nullptr)
            return false;

        Candidate candidate;
        bool permissionDenied = false;
        if (!query_device(path, candidate, permissionDenied)) {
            if (permissionDenied)
                report_permission_denied(path);
            return false;
        }

        if (!backend->add_device(candidate.info)) {
            close_fd(candidate.fd);
            return false;
        }
        reportedPermissions.erase(path);

        auto device = std::make_unique<Device>();
        device->id = candidate.path;
        device->fd = candidate.fd;
        device->inputReportSize = candidate.inputReportSize;
        device->featureReportSize = candidate.featureReportSize;
        Device* raw = device.get();
        devices.emplace(candidate.path, std::move(device));
        raw->reader = std::thread([this, raw]() { read_loop(*raw); });
        return true;
    }

    void remove_device(std::unordered_map<std::string, std::unique_ptr<Device>>::iterator it) {
        if (it == devices.end())
            return;
        stop_device(*it->second);
        queue.discard(it->first);
        if (backend != nullptr)
            backend->remove_device(it->first);
        devices.erase(it);
    }

    void refresh() {
        auto paths = list_hidraw_paths();
        std::unordered_set<std::string> present(paths.begin(), paths.end());

        for (const std::string& path : paths) {
            if (devices.find(path) != devices.end())
                continue;
            start_device(path);
        }

        for (auto it = devices.begin(); it != devices.end();) {
            if (present.find(it->first) == present.end())
                remove_device(it++);
            else
                ++it;
        }
    }

    void remove_finished() {
        for (auto it = devices.begin(); it != devices.end();) {
            Device& device = *it->second;
            if (device.finished.load()) {
                if (device.reader.joinable())
                    device.reader.join();
                queue.discard(it->first);
                if (backend != nullptr)
                    backend->remove_device(it->first);
                close_fd(device.fd);
                it = devices.erase(it);
            } else {
                ++it;
            }
        }
    }
};

LinuxHid::LinuxHid() : m_impl(std::make_unique<Impl>()) {}

LinuxHid::~LinuxHid() {
    shutdown();
}

bool LinuxHid::initialize(HidBackend& backend, Runtime& runtime) {
    if (m_impl->running)
        shutdown();

    m_impl->backend = &backend;
    m_impl->runtime = &runtime;
    m_impl->running = true;
    m_impl->refresh();
    m_impl->nextRefresh = runtime.now() + 0.5;
    return true;
}

void LinuxHid::shutdown() {
    if (!m_impl->running)
        return;

    for (auto& entry : m_impl->devices) {
        m_impl->stop_device(*entry.second);
        if (m_impl->backend != nullptr)
            m_impl->backend->remove_device(entry.first);
    }
    m_impl->devices.clear();
    m_impl->queue.clear();
    m_impl->backend = nullptr;
    m_impl->runtime = nullptr;
    m_impl->running = false;
}

void LinuxHid::poll() {
    if (!m_impl->running)
        return;

    m_impl->drain_queue();
    m_impl->remove_finished();

    if (m_impl->runtime == nullptr)
        return;
    const double now = m_impl->runtime->now();
    if (now < m_impl->nextRefresh)
        return;
    m_impl->nextRefresh = now + 0.5;
    m_impl->refresh();
}

std::size_t LinuxHid::device_count() const {
    return m_impl->devices.size();
}

bool LinuxHid::write_report(std::string_view id, const uint8_t* data,
                            std::size_t size) {
    if (!m_impl->running || data == nullptr || size == 0)
        return false;

    const auto it = m_impl->devices.find(std::string(id));
    if (it == m_impl->devices.end())
        return false;

    std::size_t offset = 0;
    int stalls = 0;
    while (offset < size) {
        const ssize_t written =
            ::write(it->second->fd, data + offset, size - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            stalls = 0;
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd descriptor{};
            descriptor.fd = it->second->fd;
            descriptor.events = POLLOUT;
            if (::poll(&descriptor, 1, 10) < 0 && errno != EINTR)
                return false;
            if (++stalls > 3)
                return false;
            continue;
        }
        return false;
    }
    return true;
}

bool LinuxHid::write_feature_report(std::string_view id, const uint8_t* data,
                                    std::size_t size) {
    if (!m_impl->running || data == nullptr || size == 0)
        return false;

    const auto it = m_impl->devices.find(std::string(id));
    if (it == m_impl->devices.end())
        return false;

    std::vector<uint8_t> report(data, data + size);
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (::ioctl(it->second->fd, HIDIOCSFEATURE(size), report.data()) >= 0)
            return true;
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

int LinuxHid::read_feature_report(std::string_view id, uint8_t reportId,
                                  uint8_t* data, std::size_t size) {
    if (!m_impl->running || data == nullptr || size == 0)
        return -1;

    const auto it = m_impl->devices.find(std::string(id));
    if (it == m_impl->devices.end())
        return -1;

    const std::size_t required = it->second->featureReportSize;
    if (required == 0 || size < required)
        return -1;

    data[0] = reportId;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const int result =
            ::ioctl(it->second->fd, HIDIOCGFEATURE(required), data);
        if (result >= 0)
            return result > 0 ? result : static_cast<int>(required);
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            return -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return -1;
}

} // namespace insys::backend

#endif


