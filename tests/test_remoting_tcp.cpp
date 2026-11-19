#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "action_test_helpers.h"
#include "insys/device.h"
#include "insys/low/event.h"
#include "insys/low/key.h"
#include "insys/low/state_format.h"
#include "insys/manager.h"
#include "insys/remoting.h"
#include "insys/remoting_tcp.h"

using namespace insys;

namespace {

bool wait_for_message(TcpTransport& transport, RemoteMessage& message,
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

bool wait_for_updates(InputRemoting& remoting, std::size_t minimum,
                      int timeoutMs = 2000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::size_t total = 0;
    do {
        total += remoting.update();
        if (total >= minimum)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

} // namespace

TEST_CASE("TCP transport exchanges framed remote messages") {
    TcpServer server;
    std::string serverError;
    REQUIRE(server.listen("127.0.0.1", 0, &serverError));
    REQUIRE(server.port() != 0);

    std::unique_ptr<TcpTransport> client;
    std::string clientError;
    std::thread connector([&] {
        client = TcpTransport::connect("127.0.0.1", server.port(), &clientError);
    });
    auto serverSide = server.accept(3000, &serverError);
    connector.join();

    INFO("client error: " << clientError);
    INFO("server error: " << serverError);
    REQUIRE(client != nullptr);
    REQUIRE(serverSide != nullptr);

    RemoteMessage outbound;
    outbound.participantId = 17;
    outbound.type = RemoteMessageType::NewEvents;
    outbound.payload = {1, 2, 3, 4};
    REQUIRE(client->send(outbound));

    RemoteMessage inbound;
    REQUIRE(wait_for_message(*serverSide, inbound));
    REQUIRE(inbound.participantId == 17);
    REQUIRE(inbound.type == RemoteMessageType::NewEvents);
    REQUIRE(inbound.payload == outbound.payload);

    RemoteMessage reply;
    reply.participantId = 18;
    reply.type = RemoteMessageType::RemoveDevice;
    reply.payload = {9, 8, 7};
    REQUIRE(serverSide->send(reply));
    REQUIRE(wait_for_message(*client, inbound));
    REQUIRE(inbound.participantId == 18);
    REQUIRE(inbound.type == RemoteMessageType::RemoveDevice);
    REQUIRE(inbound.payload == reply.payload);
}

TEST_CASE("TCP transport carries mirrored device state") {
    insys_test::TestRuntime senderRuntime;
    insys_test::TestRuntime receiverRuntime;
    Manager senderManager(senderRuntime);
    Manager receiverManager(receiverRuntime);
    Keyboard* keyboard = insys_test::add_keyboard(senderManager);

    TcpServer server;
    std::string serverError;
    REQUIRE(server.listen("127.0.0.1", 0, &serverError));

    std::unique_ptr<TcpTransport> client;
    std::string clientError;
    std::thread connector([&] {
        client = TcpTransport::connect("127.0.0.1", server.port(), &clientError);
    });
    auto serverSide = server.accept(3000, &serverError);
    connector.join();

    INFO("client error: " << clientError);
    INFO("server error: " << serverError);
    REQUIRE(client != nullptr);
    REQUIRE(serverSide != nullptr);

    InputRemoting sender(senderManager, *client);
    InputRemoting receiver(receiverManager, *serverSide);
    sender.set_participant_id(31);
    receiver.set_participant_id(32);

    REQUIRE(sender.start_sending());
    REQUIRE(wait_for_updates(receiver, 2));
    receiverManager.update(UpdateType::Dynamic);
    REQUIRE(receiver.remote_device_count() == 1);

    Device* remote = receiver.remote_device(31, keyboard->deviceId);
    REQUIRE(remote != nullptr);
    REQUIRE(remote->remote);

    std::vector<std::uint8_t> state((kKeyCount + 7) / 8);
    const auto bit = static_cast<std::uint32_t>(Key::A);
    state[bit / 8] |= static_cast<std::uint8_t>(1u << (bit % 8));
    const auto event = low::make_state_event(
        kFormatKeyboard, state.data(), static_cast<std::uint32_t>(state.size()),
        keyboard->deviceId, 2.0);
    senderManager.queue_event(reinterpret_cast<const low::Event*>(event.data()));
    senderManager.update(UpdateType::Dynamic);

    REQUIRE(wait_for_updates(receiver, 1));
    receiverManager.update(UpdateType::Dynamic);
    auto* remoteKeyboard = static_cast<Keyboard*>(remote);
    REQUIRE(remoteKeyboard->key_control(Key::A)->read_bool(remote->current_state()));
}
