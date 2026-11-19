#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "insys/remoting.h"
#include "insys/remoting_udp.h"

using namespace insys;

namespace {

bool wait_for_udp_message(UdpTransport& transport, RemoteMessage& message,
                          int timeoutMs = 2000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (transport.receive(message))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

} // namespace

TEST_CASE("UDP transport exchanges best-effort datagrams") {
    std::string firstError;
    std::string secondError;
    auto first = UdpTransport::bind("127.0.0.1", 0, &firstError);
    auto second = UdpTransport::bind("127.0.0.1", 0, &secondError);

    INFO("first error: " << firstError);
    INFO("second error: " << secondError);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(first->local_port() != 0);
    REQUIRE(second->local_port() != 0);

    REQUIRE(first->set_peer("127.0.0.1", second->local_port()));
    REQUIRE(second->set_peer("127.0.0.1", first->local_port()));
    REQUIRE(first->connected());
    REQUIRE(second->connected());

    RemoteMessage outbound;
    outbound.participantId = 61;
    outbound.type = RemoteMessageType::NewEvents;
    outbound.payload = {1, 2, 3, 4};
    REQUIRE(first->send(outbound));

    RemoteMessage inbound;
    REQUIRE(wait_for_udp_message(*second, inbound));
    REQUIRE(inbound.participantId == 61);
    REQUIRE(inbound.type == RemoteMessageType::NewEvents);
    REQUIRE(inbound.payload == outbound.payload);

    RemoteMessage reply;
    reply.participantId = 62;
    reply.type = RemoteMessageType::RemoveDevice;
    reply.payload = {9, 8};
    REQUIRE(second->send(reply));
    REQUIRE(wait_for_udp_message(*first, inbound));
    REQUIRE(inbound.participantId == 62);
    REQUIRE(inbound.type == RemoteMessageType::RemoveDevice);
    REQUIRE(inbound.payload == reply.payload);
}

TEST_CASE("UDP transport adopts the first peer and rejects oversized sends") {
    auto listener = UdpTransport::bind("127.0.0.1", 0);
    auto sender = UdpTransport::bind("127.0.0.1", 0);
    REQUIRE(listener != nullptr);
    REQUIRE(sender != nullptr);
    REQUIRE_FALSE(listener->has_peer());

    REQUIRE(sender->set_peer("127.0.0.1", listener->local_port()));
    RemoteMessage message;
    message.participantId = 71;
    message.type = RemoteMessageType::Connect;
    REQUIRE(sender->send(message));

    RemoteMessage received;
    REQUIRE(wait_for_udp_message(*listener, received));
    REQUIRE(listener->has_peer());
    REQUIRE(received.type == RemoteMessageType::Connect);

    RemoteMessage oversized;
    oversized.type = RemoteMessageType::NewEvents;
    oversized.payload.assign(65508, 0);
    REQUIRE_FALSE(sender->send(oversized));
}
