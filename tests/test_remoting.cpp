#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include "action_test_helpers.h"
#include "insys/device.h"
#include "insys/low/event.h"
#include "insys/low/key.h"
#include "insys/low/state_format.h"
#include "insys/manager.h"
#include "insys/remoting.h"

using namespace insys;

namespace {

std::vector<std::uint8_t> keyboard_state_with(Key key) {
    std::vector<std::uint8_t> state((kKeyCount + 7) / 8);
    const auto bit = static_cast<std::uint32_t>(key);
    state[bit / 8] |= static_cast<std::uint8_t>(1u << (bit % 8));
    return state;
}

} // namespace

TEST_CASE("Remote messages round trip and reject malformed frames") {
    RemoteMessage original;
    original.participantId = 42;
    original.type = RemoteMessageType::NewEvents;
    original.payload = {1, 2, 3, 4, 5};

    const auto encoded = encode_remote_message(original);
    REQUIRE_FALSE(encoded.empty());

    RemoteMessage decoded;
    std::size_t consumed = 0;
    REQUIRE(decode_remote_message(encoded.data(), encoded.size(), decoded, &consumed));
    REQUIRE(consumed == encoded.size());
    REQUIRE(decoded.participantId == 42);
    REQUIRE(decoded.type == RemoteMessageType::NewEvents);
    REQUIRE(decoded.payload == original.payload);

    std::vector<std::uint8_t> trailing = encoded;
    trailing.push_back(0xff);
    consumed = 0;
    REQUIRE(decode_remote_message(trailing.data(), trailing.size(), decoded, &consumed));
    REQUIRE(consumed == encoded.size());

    auto corrupted = encoded;
    corrupted[0] = 'X';
    REQUIRE_FALSE(decode_remote_message(corrupted.data(), corrupted.size(), decoded));

    REQUIRE_FALSE(decode_remote_message(encoded.data(), encoded.size() - 1, decoded));

    auto invalidType = encoded;
    invalidType[10] = 99;
    REQUIRE_FALSE(decode_remote_message(invalidType.data(), invalidType.size(), decoded));
}

TEST_CASE("Input remoting mirrors devices, state events and removal") {
    insys_test::TestRuntime senderRuntime;
    insys_test::TestRuntime receiverRuntime;
    Manager senderManager(senderRuntime);
    Manager receiverManager(receiverRuntime);
    Keyboard* keyboard = insys_test::add_keyboard(senderManager);

    auto transports = LoopbackTransport::create_pair();
    InputRemoting sender(senderManager, transports.first);
    InputRemoting receiver(receiverManager, transports.second);
    sender.set_participant_id(11);
    receiver.set_participant_id(22);

    REQUIRE(sender.start_sending());
    REQUIRE(receiver.update() == 2);
    REQUIRE(receiver.remote_device_count() == 1);
    receiverManager.update(UpdateType::Dynamic);

    Device* remote = receiver.remote_device(11, keyboard->deviceId);
    REQUIRE(remote != nullptr);
    REQUIRE(remote->remote);
    REQUIRE(remote->remoteParticipantId == 11);
    REQUIRE(remote->layout.str() == "Keyboard");
    REQUIRE(remote->name.str() == "Keyboard");

    const auto state = keyboard_state_with(Key::A);
    const auto event = low::make_state_event(
        kFormatKeyboard, state.data(), static_cast<std::uint32_t>(state.size()),
        keyboard->deviceId, 2.0);
    senderManager.queue_event(reinterpret_cast<const low::Event*>(event.data()));
    senderManager.update(UpdateType::Dynamic);

    REQUIRE(receiver.update() == 1);
    REQUIRE(receiver.last_error().empty());
    std::vector<low::Event> receivedEvents;
    receiverManager.add_event_listener(
        [&](low::Event& event, Device*) { receivedEvents.push_back(event); });
    receiverManager.update(UpdateType::Dynamic);
    REQUIRE_FALSE(receivedEvents.empty());
    REQUIRE(receivedEvents.back().deviceId == remote->deviceId);
    auto* remoteKeyboard = static_cast<Keyboard*>(remote);
    REQUIRE(static_cast<Keyboard*>(keyboard)->key_control(Key::A)->read_bool(keyboard->current_state()));
    REQUIRE(remote->state_size_bytes() > 0);
    REQUIRE(remote->current_state()[0] == state[0]);
    REQUIRE(remoteKeyboard->key_control(Key::A)->read_bool(remote->current_state()));

    senderManager.remove_device(keyboard);
    REQUIRE(receiver.update() == 1);
    REQUIRE(receiver.remote_device_count() == 0);
    REQUIRE(receiverManager.devices().empty());
}

TEST_CASE("Input remoting starts sending on connect") {
    insys_test::TestRuntime senderRuntime;
    insys_test::TestRuntime receiverRuntime;
    Manager senderManager(senderRuntime);
    Manager receiverManager(receiverRuntime);
    insys_test::add_keyboard(receiverManager);

    auto transports = LoopbackTransport::create_pair();
    InputRemoting sender(senderManager, transports.first);
    InputRemotingOptions options;
    options.startSendingOnConnect = true;
    InputRemoting receiver(receiverManager, transports.second, options);
    sender.set_participant_id(3);
    receiver.set_participant_id(4);

    REQUIRE(sender.connect());
    REQUIRE(receiver.update() == 1);
    REQUIRE(receiver.sending());
    REQUIRE(sender.update() == 2);
    REQUIRE(sender.remote_device_count() == 1);

    REQUIRE(sender.disconnect());
    REQUIRE(sender.remote_device_count() == 0);
    REQUIRE(receiver.update() == 1);
    REQUIRE_FALSE(receiver.sending());
}

TEST_CASE("Input remoting forwards reset events once") {
    insys_test::TestRuntime senderRuntime;
    insys_test::TestRuntime receiverRuntime;
    Manager senderManager(senderRuntime);
    Manager receiverManager(receiverRuntime);
    Keyboard* keyboard = insys_test::add_keyboard(senderManager);

    auto transports = LoopbackTransport::create_pair();
    InputRemoting sender(senderManager, transports.first);
    InputRemoting receiver(receiverManager, transports.second);
    sender.set_participant_id(51);
    receiver.set_participant_id(52);

    REQUIRE(sender.start_sending());
    REQUIRE(receiver.update() == 2);
    receiverManager.update(UpdateType::Dynamic);

    std::vector<DeviceChange> changes;
    receiverManager.add_device_change_listener(
        [&](Device*, DeviceChange change) { changes.push_back(change); });

    low::DeviceResetEvent reset{};
    reset.header = low::make_event(low::kDeviceResetEvent,
                                   static_cast<uint16_t>(sizeof(reset)),
                                   keyboard->deviceId, 3.0);
    reset.hardReset = true;
    senderManager.queue_event(reinterpret_cast<const low::Event*>(&reset));
    senderManager.update(UpdateType::Dynamic);

    REQUIRE(receiver.update() == 1);
    receiverManager.update(UpdateType::Dynamic);
    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0] == DeviceChange::HardReset);
}

TEST_CASE("Input remoting reports malformed device messages") {
    insys_test::TestRuntime senderRuntime;
    insys_test::TestRuntime receiverRuntime;
    Manager senderManager(senderRuntime);
    Manager receiverManager(receiverRuntime);

    auto transports = LoopbackTransport::create_pair();
    InputRemoting sender(senderManager, transports.first);
    InputRemoting receiver(receiverManager, transports.second);

    RemoteMessage malformed;
    malformed.type = RemoteMessageType::NewDevice;
    malformed.payload = {0, 1, 2};
    REQUIRE(transports.first.send(malformed));
    REQUIRE(receiver.update() == 1);
    REQUIRE_FALSE(receiver.last_error().empty());
    REQUIRE(receiver.remote_device_count() == 0);
    REQUIRE(receiverManager.devices().empty());
}

