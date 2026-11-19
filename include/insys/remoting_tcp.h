#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "insys/remoting.h"

namespace insys {

class TcpTransport final : public IRemoteTransport {
public:
    ~TcpTransport() override;

    TcpTransport(const TcpTransport&) = delete;
    TcpTransport& operator=(const TcpTransport&) = delete;

    static std::unique_ptr<TcpTransport> connect(std::string_view address,
                                                 uint16_t port,
                                                 std::string* error = nullptr);

    bool send(const RemoteMessage& message) override;
    bool receive(RemoteMessage& message) override;

    bool send_raw(const void* data, std::size_t size);
    bool receive_raw(void* data, std::size_t capacity, std::size_t& received);

    bool connected() const;
    void close();

private:
    struct Impl;
    explicit TcpTransport(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;

    friend class TcpServer;
};

class TcpServer {
public:
    TcpServer();
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    bool listen(std::string_view address, uint16_t port,
                std::string* error = nullptr);
    std::unique_ptr<TcpTransport> accept(int timeoutMs = 5000,
                                         std::string* error = nullptr);

    uint16_t port() const;
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace insys
