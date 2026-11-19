#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "insys/remoting.h"

namespace insys {

class UdpTransport final : public IRemoteTransport {
public:
    ~UdpTransport() override;

    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    static std::unique_ptr<UdpTransport> bind(std::string_view address,
                                              uint16_t port,
                                              std::string* error = nullptr);

    bool set_peer(std::string_view address, uint16_t port,
                  std::string* error = nullptr);
    bool has_peer() const;

    uint16_t local_port() const;
    bool connected() const;
    void close();

    bool send(const RemoteMessage& message) override;
    bool receive(RemoteMessage& message) override;

private:
    struct Impl;
    explicit UdpTransport(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace insys
