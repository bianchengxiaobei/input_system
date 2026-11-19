#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "insys/remoting.h"

namespace insys {

class TcpServer;

class WebSocketTransport final : public IRemoteTransport {
public:
    struct Impl;

    ~WebSocketTransport() override;

    WebSocketTransport(const WebSocketTransport&) = delete;
    WebSocketTransport& operator=(const WebSocketTransport&) = delete;

    static std::unique_ptr<WebSocketTransport> connect(
        std::string_view host, uint16_t port, std::string_view path = "/",
        int timeoutMs = 3000, std::string* error = nullptr);

    static std::unique_ptr<WebSocketTransport> accept(
        TcpServer& server, std::string_view path = "/", int timeoutMs = 5000,
        std::string* error = nullptr);

    bool send(const RemoteMessage& message) override;
    bool receive(RemoteMessage& message) override;

    bool connected() const;
    void close();

private:
    explicit WebSocketTransport(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace insys

