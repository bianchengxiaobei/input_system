#include "insys/remoting_udp.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace insys {
namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

constexpr std::size_t kMaxDatagramBytes = 65507;
constexpr std::size_t kReceiveBufferBytes = 65536;

void set_error(std::string* error, std::string value) {
    if (error != nullptr)
        *error = std::move(value);
}

std::string socket_error_text() {
#ifdef _WIN32
    return "socket error " + std::to_string(static_cast<int>(WSAGetLastError()));
#else
    return std::strerror(errno);
#endif
}

bool ensure_sockets(std::string* error) {
#ifdef _WIN32
    static std::once_flag initFlag;
    static bool initialized = false;
    std::call_once(initFlag, [] {
        WSADATA data{};
        initialized = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    });
    if (!initialized) {
        set_error(error, "WSAStartup failed");
        return false;
    }
#else
    (void)error;
#endif
    return true;
}

void close_socket(SocketHandle& socket) {
    if (socket == kInvalidSocket)
        return;
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
    socket = kInvalidSocket;
}

bool would_block() {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

bool set_nonblocking(SocketHandle socket) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool wait_for_socket(SocketHandle socket, bool writable, int timeoutMs) {
    fd_set readSet;
    fd_set writeSet;
    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);
    FD_SET(socket, writable ? &writeSet : &readSet);

    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    const int result = select(static_cast<int>(socket) + 1,
                              writable ? nullptr : &readSet,
                              writable ? &writeSet : nullptr, nullptr, &timeout);
    return result > 0 &&
           FD_ISSET(socket, writable ? &writeSet : &readSet) != 0;
}

bool make_ipv4_address(std::string_view address, uint16_t port,
                       sockaddr_in& out) {
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    const std::string text(address);
    return inet_pton(AF_INET, text.c_str(), &out.sin_addr) == 1;
}

bool same_endpoint(const sockaddr_in& left, const sockaddr_in& right) {
    return left.sin_family == right.sin_family &&
           left.sin_port == right.sin_port &&
           left.sin_addr.s_addr == right.sin_addr.s_addr;
}

} // namespace

struct UdpTransport::Impl {
    SocketHandle socket = kInvalidSocket;
    bool closed = false;
    bool hasPeer = false;
    uint16_t localPort = 0;
    sockaddr_in peer{};

    ~Impl() { close_socket(socket); }
};

UdpTransport::UdpTransport(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
UdpTransport::~UdpTransport() = default;

std::unique_ptr<UdpTransport> UdpTransport::bind(std::string_view address,
                                                 uint16_t port,
                                                 std::string* error) {
    if (!ensure_sockets(error))
        return nullptr;

    sockaddr_in endpoint{};
    if (!make_ipv4_address(address, port, endpoint)) {
        set_error(error, "invalid IPv4 address");
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl->socket == kInvalidSocket) {
        set_error(error, "socket() failed: " + socket_error_text());
        return nullptr;
    }
    if (!set_nonblocking(impl->socket)) {
        set_error(error, "failed to set non-blocking mode: " + socket_error_text());
        return nullptr;
    }
    if (::bind(impl->socket, reinterpret_cast<const sockaddr*>(&endpoint),
               sizeof(endpoint)) != 0) {
        set_error(error, "bind() failed: " + socket_error_text());
        return nullptr;
    }

    sockaddr_in local{};
#ifdef _WIN32
    int length = sizeof(local);
#else
    socklen_t length = sizeof(local);
#endif
    if (getsockname(impl->socket, reinterpret_cast<sockaddr*>(&local),
                    &length) != 0) {
        set_error(error, "getsockname() failed: " + socket_error_text());
        return nullptr;
    }
    impl->localPort = ntohs(local.sin_port);
    return std::unique_ptr<UdpTransport>(new UdpTransport(std::move(impl)));
}

bool UdpTransport::set_peer(std::string_view address, uint16_t port,
                            std::string* error) {
    if (!m_impl || m_impl->closed) {
        set_error(error, "UDP transport is closed");
        return false;
    }
    if (!make_ipv4_address(address, port, m_impl->peer)) {
        set_error(error, "invalid IPv4 peer address");
        return false;
    }
    m_impl->hasPeer = true;
    return true;
}

bool UdpTransport::has_peer() const {
    return m_impl && m_impl->hasPeer;
}

uint16_t UdpTransport::local_port() const {
    return m_impl ? m_impl->localPort : 0;
}

bool UdpTransport::connected() const {
    return m_impl && !m_impl->closed && m_impl->hasPeer &&
           m_impl->socket != kInvalidSocket;
}

void UdpTransport::close() {
    if (!m_impl)
        return;
    close_socket(m_impl->socket);
    m_impl->closed = true;
}

bool UdpTransport::send(const RemoteMessage& message) {
    if (!m_impl || m_impl->closed || !m_impl->hasPeer)
        return false;

    const std::vector<uint8_t> frame = encode_remote_message(message);
    if (frame.empty() || frame.size() > kMaxDatagramBytes)
        return false;

    for (;;) {
        const int sent = sendto(
            m_impl->socket, reinterpret_cast<const char*>(frame.data()),
            static_cast<int>(frame.size()), 0,
            reinterpret_cast<const sockaddr*>(&m_impl->peer),
            sizeof(m_impl->peer));
        if (sent == static_cast<int>(frame.size()))
            return true;
        if (sent < 0 && would_block()) {
            if (!wait_for_socket(m_impl->socket, true, 1000))
                return false;
            continue;
        }
        return false;
    }
}

bool UdpTransport::receive(RemoteMessage& message) {
    if (!m_impl || m_impl->closed)
        return false;

    std::array<uint8_t, kReceiveBufferBytes> buffer{};
    for (;;) {
        sockaddr_in from{};
#ifdef _WIN32
        int length = sizeof(from);
#else
        socklen_t length = sizeof(from);
#endif
        const int received = recvfrom(
            m_impl->socket, reinterpret_cast<char*>(buffer.data()),
            static_cast<int>(buffer.size()), 0,
            reinterpret_cast<sockaddr*>(&from), &length);
        if (received < 0) {
            if (would_block())
                return false;
            m_impl->closed = true;
            return false;
        }
        if (received == 0)
            continue;
        if (m_impl->hasPeer && !same_endpoint(from, m_impl->peer))
            continue;
        if (!m_impl->hasPeer) {
            m_impl->peer = from;
            m_impl->hasPeer = true;
        }

        RemoteMessage decoded;
        if (decode_remote_message(buffer.data(), static_cast<std::size_t>(received),
                                  decoded)) {
            message = std::move(decoded);
            return true;
        }
    }
}

} // namespace insys
