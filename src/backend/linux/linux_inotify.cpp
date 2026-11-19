#include "insys/backend/linux_inotify.h"

#if defined(__linux__)

#include <sys/inotify.h>

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

namespace insys::backend::linux_evdev {
namespace {

constexpr std::uint32_t kWatchMask =
    kInCreate | kInDelete | kInMovedFrom | kInMovedTo | kInAttrib |
    IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;

} // namespace

LinuxInotifyWatcher::~LinuxInotifyWatcher() {
    shutdown();
}

bool LinuxInotifyWatcher::initialize() {
    shutdown();

    const int fd = ::inotify_init();
    if (fd < 0)
        return false;

    const int statusFlags = ::fcntl(fd, F_GETFL, 0);
    if (statusFlags < 0 || ::fcntl(fd, F_SETFL, statusFlags | O_NONBLOCK) < 0) {
        ::close(fd);
        return false;
    }
    const int descriptorFlags = ::fcntl(fd, F_GETFD, 0);
    if (descriptorFlags < 0 ||
        ::fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0) {
        ::close(fd);
        return false;
    }

    const int watch = ::inotify_add_watch(fd, "/dev/input", kWatchMask);
    if (watch < 0) {
        ::close(fd);
        return false;
    }

    m_fd = fd;
    m_watch = watch;
    m_monitor.reset();
    return true;
}

void LinuxInotifyWatcher::shutdown() {
    if (m_watch >= 0 && m_fd >= 0)
        ::inotify_rm_watch(m_fd, m_watch);
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
    m_watch = -1;
    m_monitor.reset();
}

bool LinuxInotifyWatcher::poll_scan_request() {
    if (m_fd < 0)
        return false;

    std::uint8_t buffer[4096];
    bool scan = false;
    for (;;) {
        const ssize_t bytes = ::read(m_fd, buffer, sizeof(buffer));
        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (bytes == 0)
            break;
        scan = m_monitor.feed(buffer, static_cast<std::size_t>(bytes)) || scan;
    }
    return scan;
}

} // namespace insys::backend::linux_evdev

#else

namespace insys::backend::linux_evdev {

LinuxInotifyWatcher::~LinuxInotifyWatcher() = default;

bool LinuxInotifyWatcher::initialize() {
    return false;
}

void LinuxInotifyWatcher::shutdown() {
    m_fd = -1;
    m_watch = -1;
    m_monitor.reset();
}

bool LinuxInotifyWatcher::poll_scan_request() {
    return false;
}

} // namespace insys::backend::linux_evdev

#endif
