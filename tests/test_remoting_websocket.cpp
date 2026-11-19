#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "insys/remoting.h"
#include "insys/remoting_tcp.h"
#include "insys/remoting_websocket.h"

using namespace insys;

namespace {

bool wait_for_websocket_message(WebSocketTransport& transport,
                                RemoteMessage& message, int timeoutMs = 2000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (transport.receive(message))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

bool read_raw_http(TcpTransport& transport, std::string& out,
                   int timeoutMs = 2000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        std::uint8_t buffer[512];
        std::size_t received = 0;
        if (transport.receive_raw(buffer, sizeof(buffer), received)) {
            out.append(reinterpret_cast<const char*>(buffer), received);
            if (out.find("\r\n\r\n") != std::string::npos)
                return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

} // namespace

TEST_CASE("WebSocket handshake matches the RFC accept key") {
    TcpServer server;
    std::string serverError;
    REQUIRE(server.listen("127.0.0.1", 0, &serverError));

    auto raw = TcpTransport::connect("127.0.0.1", server.port(), &serverError);
    REQUIRE(raw != nullptr);

    std::unique_ptr<WebSocketTransport> accepted;
    std::thread acceptor([&] {
        accepted = WebSocketTransport::accept(server, "/", 3000, &serverError);
    });

    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";
    const bool sent = raw->send_raw(request.data(), request.size());

    std::string response;
    const bool responseRead = read_raw_http(*raw, response);
    acceptor.join();

    INFO("server error: " << serverError);
    REQUIRE(sent);
    REQUIRE(responseRead);
    REQUIRE(accepted != nullptr);
    REQUIRE(response.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
}


TEST_CASE("WebSocket transport exchanges binary remote messages") {
    TcpServer server;
    std::string serverError;
    REQUIRE(server.listen("127.0.0.1", 0, &serverError));

    std::unique_ptr<WebSocketTransport> client;
    std::string clientError;
    std::thread connector([&] {
        client = WebSocketTransport::connect("127.0.0.1", server.port(), "/",
                                           3000, &clientError);
    });
    auto serverSide = WebSocketTransport::accept(server, "/", 3000, &serverError);
    connector.join();

    INFO("client error: " << clientError);
    INFO("server error: " << serverError);
    REQUIRE(client != nullptr);
    REQUIRE(serverSide != nullptr);
    REQUIRE(client->connected());
    REQUIRE(serverSide->connected());

    RemoteMessage outbound;
    outbound.participantId = 81;
    outbound.type = RemoteMessageType::NewEvents;
    outbound.payload.assign(300, 0x5a);
    REQUIRE(client->send(outbound));

    RemoteMessage inbound;
    REQUIRE(wait_for_websocket_message(*serverSide, inbound));
    REQUIRE(inbound.participantId == 81);
    REQUIRE(inbound.type == RemoteMessageType::NewEvents);
    REQUIRE(inbound.payload == outbound.payload);

    RemoteMessage reply;
    reply.participantId = 82;
    reply.type = RemoteMessageType::RemoveDevice;
    reply.payload = {9, 8, 7};
    REQUIRE(serverSide->send(reply));
    REQUIRE(wait_for_websocket_message(*client, inbound));
    REQUIRE(inbound.participantId == 82);
    REQUIRE(inbound.type == RemoteMessageType::RemoveDevice);
    REQUIRE(inbound.payload == reply.payload);
}
