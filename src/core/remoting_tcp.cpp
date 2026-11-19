#include "insys/remoting_tcp.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

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

constexpr int kSocketBufferSize = 8192;

#ifdef _WIN32
constexpr int kSendFlags = 0;
#else
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
#endif

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
    const int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

bool connect_in_progress() {
#ifdef _WIN32
    const int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return errno == EINPROGRESS;
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
    if (socket == kInvalidSocket)
        return false;

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
    if (result <= 0)
        return false;
    return FD_ISSET(socket, writable ? &writeSet : &readSet) != 0;
}

bool make_ipv4_address(std::string_view address, uint16_t port,
                       sockaddr_in& out) {
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    const std::string text(address);
    return inet_pton(AF_INET, text.c_str(), &out.sin_addr) == 1;
}

void mark_closed(SocketHandle& socket) {
    close_socket(socket);
}

} // namespace

struct TcpTransport::Impl {
    SocketHandle socket = kInvalidSocket;
    bool closed = false;
    std::vector<uint8_t> inbound;
    std::deque<RemoteMessage> received;

    ~Impl() { close_socket(socket); }
};

struct TcpServer::Impl {
    SocketHandle socket = kInvalidSocket;
    uint16_t port = 0;

    ~Impl() { close_socket(socket); }
};

TcpTransport::TcpTransport(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
TcpTransport::~TcpTransport() = default;

std::unique_ptr<TcpTransport> TcpTransport::connect(std::string_view address,
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
    impl->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl->socket == kInvalidSocket) {
        set_error(error, "socket() failed: " + socket_error_text());
        return nullptr;
    }
    if (!set_nonblocking(impl->socket)) {
        set_error(error, "failed to set non-blocking mode: " + socket_error_text());
        return nullptr;
    }

    const int result = ::connect(impl->socket,
                                 reinterpret_cast<const sockaddr*>(&endpoint),
                                 sizeof(endpoint));
    if (result != 0) {
        if (!connect_in_progress()) {
            set_error(error, "connect() failed: " + socket_error_text());
            return nullptr;
        }
        if (!wait_for_socket(impl->socket, true, 3000)) {
            set_error(error, "connect() timed out");
            return nullptr;
        }

        int socketError = 0;
#ifdef _WIN32
        int length = sizeof(socketError);
#else
        socklen_t length = sizeof(socketError);
#endif
        if (getsockopt(impl->socket, SOL_SOCKET, SO_ERROR,
                       reinterpret_cast<char*>(&socketError), &length) != 0 ||
            socketError != 0) {
            set_error(error, "connect() failed: " + socket_error_text());
            return nullptr;
        }
    }

    return std::unique_ptr<TcpTransport>(new TcpTransport(std::move(impl)));
}

bool TcpTransport::send(const RemoteMessage& message) {
    const std::vector<uint8_t> frame = encode_remote_message(message);
    if (frame.empty())
        return false;
    return send_raw(frame.data(), frame.size());
}

bool TcpTransport::send_raw(const void* data, std::size_t size) {
    if (!m_impl || m_impl->closed || (data == nullptr && size != 0))
        return false;
    if (size == 0)
        return true;

    const auto* bytes = static_cast<const uint8_t*>(data);
    std::size_t offset = 0;
    while (offset < size) {
        if (!wait_for_socket(m_impl->socket, true, 1000)) {
            mark_closed(m_impl->socket);
            m_impl->closed = true;
            return false;
        }

        const std::size_t remaining = size - offset;
        const int chunk = static_cast<int>(
            std::min<std::size_t>(remaining, 1024u * 1024u));
        const int sent = ::send(m_impl->socket,
                                reinterpret_cast<const char*>(bytes + offset),
                                chunk, kSendFlags);
        if (sent > 0) {
            offset += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && would_block())
            continue;

        mark_closed(m_impl->socket);
        m_impl->closed = true;
        return false;
    }
    return true;
}

bool TcpTransport::receive_raw(void* data, std::size_t capacity,
                               std::size_t& received) {
    received = 0;
    if (!m_impl || m_impl->closed || data == nullptr || capacity == 0)
        return false;

    const int result = ::recv(
        m_impl->socket, static_cast<char*>(data),
        static_cast<int>(std::min<std::size_t>(capacity, 1024u * 1024u)), 0);
    if (result > 0) {
        received = static_cast<std::size_t>(result);
        return true;
    }
    if (result == 0) {
        m_impl->closed = true;
        return false;
    }
    if (would_block())
        return false;
    m_impl->closed = true;
    return false;
}

bool TcpTransport::receive(RemoteMessage& message) {
    if (!m_impl)
        return false;

    if (m_impl->received.empty() && !m_impl->closed) {
        char buffer[kSocketBufferSize];
        for (;;) {
            const int received = ::recv(m_impl->socket, buffer, sizeof(buffer), 0);
            if (received > 0) {
                m_impl->inbound.insert(m_impl->inbound.end(), buffer,
                                       buffer + received);
                continue;
            }
            if (received == 0) {
                m_impl->closed = true;
                break;
            }
            if (would_block())
                break;
            m_impl->closed = true;
            break;
        }

        while (!m_impl->inbound.empty()) {
            RemoteMessage decoded;
            std::size_t consumed = 0;
            if (!decode_remote_message(m_impl->inbound.data(),
                                       m_impl->inbound.size(), decoded, &consumed)) {
                break;
            }
            m_impl->received.push_back(std::move(decoded));
            m_impl->inbound.erase(m_impl->inbound.begin(),
                                  m_impl->inbound.begin() +
                                      static_cast<std::ptrdiff_t>(consumed));
        }
    }

    if (m_impl->received.empty())
        return false;
    message = std::move(m_impl->received.front());
    m_impl->received.pop_front();
    return true;
}

bool TcpTransport::connected() const {
    return m_impl && !m_impl->closed && m_impl->socket != kInvalidSocket;
}

void TcpTransport::close() {
    if (!m_impl)
        return;
    mark_closed(m_impl->socket);
    m_impl->closed = true;
}

TcpServer::TcpServer() = default;
TcpServer::~TcpServer() = default;

bool TcpServer::listen(std::string_view address, uint16_t port,
                       std::string* error) {
    close();
    if (!ensure_sockets(error))
        return false;

    sockaddr_in endpoint{};
    if (!make_ipv4_address(address, port, endpoint)) {
        set_error(error, "invalid IPv4 address");
        return false;
    }

    auto impl = std::make_unique<Impl>();
    impl->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl->socket == kInvalidSocket) {
        set_error(error, "socket() failed: " + socket_error_text());
        return false;
    }

    const int reuse = 1;
    setsockopt(impl->socket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    if (bind(impl->socket, reinterpret_cast<const sockaddr*>(&endpoint),
             sizeof(endpoint)) != 0) {
        set_error(error, "bind() failed: " + socket_error_text());
        return false;
    }
    if (::listen(impl->socket, SOMAXCONN) != 0) {
        set_error(error, "listen() failed: " + socket_error_text());
        return false;
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
        return false;
    }

    impl->port = ntohs(local.sin_port);
    if (!set_nonblocking(impl->socket)) {
        set_error(error, "failed to set non-blocking mode: " + socket_error_text());
        return false;
    }
    m_impl = std::move(impl);
    return true;
}

std::unique_ptr<TcpTransport> TcpServer::accept(int timeoutMs, std::string* error) {
    if (!m_impl || m_impl->socket == kInvalidSocket) {
        set_error(error, "TCP server is not listening");
        return nullptr;
    }
    if (!wait_for_socket(m_impl->socket, false, timeoutMs)) {
        set_error(error, "accept() timed out");
        return nullptr;
    }

    sockaddr_in remote{};
#ifdef _WIN32
    int length = sizeof(remote);
#else
    socklen_t length = sizeof(remote);
#endif
    SocketHandle client = ::accept(m_impl->socket,
                                   reinterpret_cast<sockaddr*>(&remote), &length);
    if (client == kInvalidSocket) {
        set_error(error, "accept() failed: " + socket_error_text());
        return nullptr;
    }
    if (!set_nonblocking(client)) {
        close_socket(client);
        set_error(error, "failed to set non-blocking mode: " + socket_error_text());
        return nullptr;
    }

    auto impl = std::make_unique<TcpTransport::Impl>();
    impl->socket = client;
    return std::unique_ptr<TcpTransport>(new TcpTransport(std::move(impl)));
}

uint16_t TcpServer::port() const {
    return m_impl ? m_impl->port : 0;
}

void TcpServer::close() {
    if (!m_impl)
        return;
    mark_closed(m_impl->socket);
    m_impl->port = 0;
}

} // namespace insys
