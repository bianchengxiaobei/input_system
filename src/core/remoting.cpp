#include "insys/remoting.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <string_view>

#include "insys/device.h"
#include "insys/device_builder.h"
#include "insys/input_listeners.h"
#include "insys/interned_string.h"
#include "insys/low/event.h"
#include "insys/manager.h"

namespace insys {
namespace {

constexpr char kMessageMagic[8] = {'I', 'N', 'S', 'Y', 'R', 'E', 'M', '1'};
constexpr uint16_t kMessageVersion = 1;
constexpr std::size_t kMessageHeaderSize = 20;
constexpr std::size_t kMaxStringBytes = 1024u * 1024u;

bool valid_message_type(RemoteMessageType type) {
    switch (type) {
    case RemoteMessageType::Connect:
    case RemoteMessageType::Disconnect:
    case RemoteMessageType::NewDevice:
    case RemoteMessageType::NewEvents:
    case RemoteMessageType::RemoveDevice:
    case RemoteMessageType::StartSending:
    case RemoteMessageType::StopSending:
        return true;
    }
    return false;
}

struct Writer {
    std::vector<uint8_t>& out;
    bool ok = true;

    void u8(uint8_t value) { out.push_back(value); }

    void u16(uint16_t value) {
        out.push_back(static_cast<uint8_t>(value & 0xffu));
        out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
    }

    void u32(uint32_t value) {
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xffu));
    }

    void i32(int32_t value) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }

    void string(std::string_view value) {
        if (!ok)
            return;
        if (value.size() > kMaxStringBytes ||
            value.size() > std::numeric_limits<uint32_t>::max()) {
            ok = false;
            return;
        }
        u32(static_cast<uint32_t>(value.size()));
        out.insert(out.end(), value.begin(), value.end());
    }
};

struct Reader {
    const uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;

    bool u8(uint8_t& value) {
        if (offset + 1 > size)
            return false;
        value = data[offset++];
        return true;
    }

    bool u16(uint16_t& value) {
        if (offset + 2 > size)
            return false;
        value = static_cast<uint16_t>(data[offset]) |
                static_cast<uint16_t>(data[offset + 1]) << 8u;
        offset += 2;
        return true;
    }

    bool u32(uint32_t& value) {
        if (offset + 4 > size)
            return false;
        value = 0;
        for (int i = 0; i < 4; ++i)
            value |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
        offset += 4;
        return true;
    }

    bool i32(int32_t& value) {
        uint32_t bits = 0;
        if (!u32(bits))
            return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool string(std::string& value) {
        uint32_t length = 0;
        if (!u32(length) || length > kMaxStringBytes || length > size - offset)
            return false;
        value.assign(reinterpret_cast<const char*>(data + offset), length);
        offset += length;
        return true;
    }

    bool skip(std::size_t count) {
        if (count > size - offset)
            return false;
        offset += count;
        return true;
    }

    bool done() const { return offset == size; }
};

struct DevicePayload {
    int32_t deviceId = 0;
    uint32_t stateFormat = 0;
    std::string layout;
    std::string name;
    DeviceDescription description;
};

std::vector<uint8_t> make_device_payload(const Device& device) {
    std::vector<uint8_t> payload;
    Writer writer{payload};
    writer.i32(device.deviceId);
    writer.u32(device.stateBlock.format.value);
    writer.string(device.layout.str());
    writer.string(device.name.str());
    writer.string(device.description.interfaceName);
    writer.string(device.description.deviceClass);
    writer.string(device.description.manufacturer);
    writer.string(device.description.product);
    writer.string(device.description.serial);
    writer.string(device.description.version);
    writer.string(device.description.capabilities);
    return writer.ok ? std::move(payload) : std::vector<uint8_t>{};
}

bool read_device_payload(const std::vector<uint8_t>& payload, DevicePayload& out) {
    Reader reader{payload.data(), payload.size()};
    if (!reader.i32(out.deviceId) || !reader.u32(out.stateFormat) ||
        !reader.string(out.layout) || !reader.string(out.name) ||
        !reader.string(out.description.interfaceName) ||
        !reader.string(out.description.deviceClass) ||
        !reader.string(out.description.manufacturer) ||
        !reader.string(out.description.product) ||
        !reader.string(out.description.serial) ||
        !reader.string(out.description.version) ||
        !reader.string(out.description.capabilities)) {
        return false;
    }
    return reader.done() && out.deviceId > 0 && !out.layout.empty();
}

std::vector<uint8_t> make_device_id_payload(int deviceId) {
    std::vector<uint8_t> payload;
    Writer writer{payload};
    writer.i32(deviceId);
    return writer.ok ? std::move(payload) : std::vector<uint8_t>{};
}

} // namespace

std::vector<uint8_t> encode_remote_message(const RemoteMessage& message) {
    if (!valid_message_type(message.type) ||
        message.payload.size() > kRemoteMaxPayloadBytes) {
        return {};
    }

    std::vector<uint8_t> out;
    out.reserve(kMessageHeaderSize + message.payload.size());
    Writer writer{out};
    out.insert(out.end(), std::begin(kMessageMagic), std::end(kMessageMagic));
    writer.u16(kMessageVersion);
    writer.u8(static_cast<uint8_t>(message.type));
    writer.u8(0);
    writer.i32(message.participantId);
    writer.u32(static_cast<uint32_t>(message.payload.size()));
    out.insert(out.end(), message.payload.begin(), message.payload.end());
    return writer.ok ? std::move(out) : std::vector<uint8_t>{};
}

bool decode_remote_message(const uint8_t* data, std::size_t size,
                           RemoteMessage& message, std::size_t* consumed) {
    if (data == nullptr || size < kMessageHeaderSize ||
        std::memcmp(data, kMessageMagic, sizeof(kMessageMagic)) != 0) {
        return false;
    }

    Reader reader{data, size};
    if (!reader.skip(sizeof(kMessageMagic)))
        return false;

    uint16_t version = 0;
    uint8_t type = 0;
    uint8_t flags = 0;
    int32_t participantId = 0;
    uint32_t payloadSize = 0;
    if (!reader.u16(version) || !reader.u8(type) || !reader.u8(flags) ||
        !reader.i32(participantId) || !reader.u32(payloadSize)) {
        return false;
    }
    if (version != kMessageVersion || flags != 0 ||
        payloadSize > kRemoteMaxPayloadBytes ||
        !valid_message_type(static_cast<RemoteMessageType>(type))) {
        return false;
    }
    if (payloadSize > size - reader.offset)
        return false;

    message.participantId = participantId;
    message.type = static_cast<RemoteMessageType>(type);
    message.payload.assign(data + reader.offset, data + reader.offset + payloadSize);
    if (consumed != nullptr)
        *consumed = reader.offset + payloadSize;
    return true;
}

struct LoopbackTransport::Mailbox {
    std::deque<RemoteMessage> messages;
};

LoopbackTransport::LoopbackTransport() = default;
LoopbackTransport::~LoopbackTransport() = default;
LoopbackTransport::LoopbackTransport(LoopbackTransport&&) noexcept = default;
LoopbackTransport& LoopbackTransport::operator=(LoopbackTransport&&) noexcept = default;

LoopbackTransport::LoopbackTransport(std::shared_ptr<Mailbox> outbox,
                                     std::shared_ptr<Mailbox> inbox)
    : m_outbox(std::move(outbox)), m_inbox(std::move(inbox)) {}

std::pair<LoopbackTransport, LoopbackTransport> LoopbackTransport::create_pair() {
    auto first = std::make_shared<Mailbox>();
    auto second = std::make_shared<Mailbox>();
    return {LoopbackTransport(first, second), LoopbackTransport(second, first)};
}

bool LoopbackTransport::send(const RemoteMessage& message) {
    if (!m_outbox)
        return false;
    m_outbox->messages.push_back(message);
    return true;
}

bool LoopbackTransport::receive(RemoteMessage& message) {
    if (!m_inbox || m_inbox->messages.empty())
        return false;
    message = std::move(m_inbox->messages.front());
    m_inbox->messages.pop_front();
    return true;
}

std::size_t LoopbackTransport::pending() const {
    return m_inbox ? m_inbox->messages.size() : 0;
}

InputRemoting::InputRemoting(Manager& manager, IRemoteTransport& transport,
                             InputRemotingOptions options)
    : m_manager(&manager), m_transport(&transport), m_options(options) {}

InputRemoting::~InputRemoting() {
    stop_sending();
}

bool InputRemoting::start_sending() {
    if (m_sending)
        return true;
    if (m_manager == nullptr || m_transport == nullptr) {
        m_lastError = "remoting has no manager or transport";
        return false;
    }

    m_lastError.clear();
    m_sending = true;
    m_eventListenerId = m_manager->add_event_listener(
        [this](low::Event& event, Device* device) { on_event(event, device); });
    m_deviceListenerId = m_manager->add_device_change_listener(
        [this](Device* device, DeviceChange change) {
            on_device_change(device, static_cast<int>(change));
        });

    if (!send_initial_messages()) {
        stop_sending();
        return false;
    }
    return true;
}

void InputRemoting::stop_sending() {
    if (m_manager != nullptr) {
        if (m_eventListenerId != 0)
            m_manager->remove_event_listener(m_eventListenerId);
        if (m_deviceListenerId != 0)
            m_manager->remove_device_change_listener(m_deviceListenerId);
    }
    m_eventListenerId = 0;
    m_deviceListenerId = 0;
    m_sending = false;
}

bool InputRemoting::connect() {
    return send_message(RemoteMessageType::Connect);
}

bool InputRemoting::disconnect() {
    const bool sent = send_message(RemoteMessageType::Disconnect);
    remove_all_remote_devices();
    stop_sending();
    return sent;
}

std::size_t InputRemoting::update() {
    m_lastError.clear();
    std::size_t count = 0;
    RemoteMessage message;
    while (m_transport != nullptr && m_transport->receive(message)) {
        ++count;
        process_message(message);
    }
    return count;
}

void InputRemoting::set_participant_id(int id) {
    if (id > 0)
        m_participantId = id;
}

Device* InputRemoting::remote_device(int participantId, int remoteDeviceId) const {
    const auto sender = m_senders.find(participantId);
    if (sender == m_senders.end())
        return nullptr;
    const auto device = sender->second.devices.find(remoteDeviceId);
    if (device == sender->second.devices.end())
        return nullptr;
    return m_manager != nullptr ? m_manager->device_by_id(device->second) : nullptr;
}

std::size_t InputRemoting::remote_device_count() const {
    std::size_t count = 0;
    for (const auto& [participantId, sender] : m_senders) {
        (void)participantId;
        count += sender.devices.size();
    }
    return count;
}

bool InputRemoting::send_message(RemoteMessageType type,
                                 std::vector<uint8_t> payload) {
    if (m_transport == nullptr) {
        m_lastError = "remoting has no transport";
        return false;
    }
    if (payload.size() > kRemoteMaxPayloadBytes) {
        m_lastError = "remoting payload exceeds limit";
        return false;
    }
    RemoteMessage message;
    message.participantId = m_participantId;
    message.type = type;
    message.payload = std::move(payload);
    if (!m_transport->send(message)) {
        m_lastError = "remoting transport send failed";
        return false;
    }
    return true;
}

bool InputRemoting::send_device(Device& device) {
    if (device.remote)
        return true;
    auto payload = make_device_payload(device);
    if (payload.empty()) {
        m_lastError = "failed to encode remote device";
        return false;
    }
    if (!send_message(RemoteMessageType::NewDevice, std::move(payload)))
        return false;
    return send_initial_state(device);
}

bool InputRemoting::send_initial_state(Device& device) {
    if (!m_options.sendInitialState || !device.current_state() ||
        device.state_size_bytes() == 0) {
        return true;
    }
    const auto event = low::make_state_event(
        device.stateBlock.format, device.current_state(), device.state_size_bytes(),
        device.deviceId, m_manager->runtime().now());
    return send_message(RemoteMessageType::NewEvents, event);
}

bool InputRemoting::send_initial_messages() {
    bool ok = true;
    for (Device* device : m_manager->devices()) {
        if (device != nullptr && !device->remote)
            ok = send_device(*device) && ok;
    }
    return ok;
}

bool InputRemoting::send_events(const low::Event& event) {
    if (event.size < low::kHeaderSize ||
        event.size > kRemoteMaxPayloadBytes) {
        m_lastError = "invalid event for remoting";
        return false;
    }
    std::vector<uint8_t> payload(event.size);
    std::memcpy(payload.data(), &event, event.size);
    return send_message(RemoteMessageType::NewEvents, std::move(payload));
}

void InputRemoting::on_event(low::Event& event, Device* device) {
    if (!m_sending)
        return;
    if (device == nullptr && m_manager != nullptr)
        device = m_manager->device_by_id(event.deviceId);
    if (device != nullptr && device->remote)
        return;
    send_events(event);
}

void InputRemoting::on_device_change(Device* device, int change) {
    if (!m_sending || device == nullptr || device->remote)
        return;

    switch (static_cast<DeviceChange>(change)) {
    case DeviceChange::Added:
        send_device(*device);
        break;
    case DeviceChange::Removed:
        send_message(RemoteMessageType::RemoveDevice,
                     make_device_id_payload(device->deviceId));
        break;
    default:
        break;
    }
}

void InputRemoting::process_message(const RemoteMessage& message) {
    switch (message.type) {
    case RemoteMessageType::Connect:
        if (m_sending)
            send_initial_messages();
        else if (m_options.startSendingOnConnect)
            start_sending();
        break;
    case RemoteMessageType::Disconnect:
        process_disconnect(message.participantId);
        break;
    case RemoteMessageType::NewDevice:
        process_new_device(message);
        break;
    case RemoteMessageType::NewEvents:
        process_new_events(message);
        break;
    case RemoteMessageType::RemoveDevice:
        process_remove_device(message);
        break;
    case RemoteMessageType::StartSending:
        start_sending();
        break;
    case RemoteMessageType::StopSending:
        stop_sending();
        break;
    }
}

void InputRemoting::process_new_device(const RemoteMessage& message) {
    DevicePayload payload;
    if (!read_device_payload(message.payload, payload)) {
        m_lastError = "invalid remote device payload";
        return;
    }

    SenderRecord& sender = find_or_create_sender(message.participantId);
    if (sender.devices.find(payload.deviceId) != sender.devices.end()) {
        m_lastError = "duplicate remote device id";
        return;
    }

    DeviceBuilder builder(m_manager->layouts());
    auto device = builder.build(payload.layout, "",
                                m_manager->runtime().allocate_device_id(),
                                payload.description);
    if (!device) {
        m_lastError = "unknown remote device layout: " + payload.layout;
        return;
    }
    if (device->stateBlock.format.value != payload.stateFormat) {
        m_lastError = "remote device state format mismatch: " + payload.layout;
        return;
    }

    device->remote = true;
    device->remoteParticipantId = message.participantId;
    if (!payload.name.empty())
        device->name = InternedString(payload.name);

    Device* raw = m_manager->add_device(std::move(device));
    if (raw == nullptr) {
        m_lastError = "failed to add remote device";
        return;
    }
    sender.devices[payload.deviceId] = raw->deviceId;
}

void InputRemoting::process_new_events(const RemoteMessage& message) {
    const uint8_t* data = message.payload.data();
    const std::size_t size = message.payload.size();
    std::size_t offset = 0;
    while (offset < size) {
        if (offset + low::kHeaderSize > size) {
            m_lastError = "truncated remote event header";
            return;
        }
        const auto* event = reinterpret_cast<const low::Event*>(data + offset);
        if (event->size < low::kHeaderSize || event->size > size - offset) {
            m_lastError = "invalid remote event size";
            return;
        }
        const std::size_t aligned = low::align_up(event->size, low::kEventAlignment);
        std::size_t nextOffset = offset + aligned;
        if (aligned > size - offset) {
            if (event->size != size - offset) {
                m_lastError = "truncated remote event payload";
                return;
            }
            nextOffset = size;
        }

        const int remoteDeviceId = event->deviceId;
        const int localDeviceId =
            remoteDeviceId == 0 ? 0
                                : find_local_device_id(message.participantId, remoteDeviceId);
        if (remoteDeviceId == 0 || localDeviceId != 0) {
            std::vector<uint8_t> copy(event->size);
            std::memcpy(copy.data(), event, event->size);
            auto* localEvent = reinterpret_cast<low::Event*>(copy.data());
            localEvent->deviceId = static_cast<uint16_t>(localDeviceId);
            localEvent->eventId = 0;
            m_manager->queue_event(localEvent);
        }
        offset = nextOffset;
    }
}

void InputRemoting::process_remove_device(const RemoteMessage& message) {
    Reader reader{message.payload.data(), message.payload.size()};
    int32_t remoteDeviceId = 0;
    if (!reader.i32(remoteDeviceId) || !reader.done()) {
        m_lastError = "invalid remote remove-device payload";
        return;
    }

    const int localDeviceId =
        find_local_device_id(message.participantId, remoteDeviceId);
    if (localDeviceId == 0)
        return;

    if (Device* device = m_manager->device_by_id(localDeviceId))
        m_manager->remove_device(device);

    const auto sender = m_senders.find(message.participantId);
    if (sender != m_senders.end())
        sender->second.devices.erase(remoteDeviceId);
}

void InputRemoting::process_disconnect(int participantId) {
    remove_remote_devices(participantId);
    stop_sending();
}

void InputRemoting::remove_remote_devices(int participantId) {
    const auto sender = m_senders.find(participantId);
    if (sender == m_senders.end())
        return;

    std::vector<int> localIds;
    localIds.reserve(sender->second.devices.size());
    for (const auto& [remoteId, localId] : sender->second.devices) {
        (void)remoteId;
        localIds.push_back(localId);
    }
    for (int localId : localIds) {
        if (Device* device = m_manager->device_by_id(localId))
            m_manager->remove_device(device);
    }
    m_senders.erase(participantId);
}

void InputRemoting::remove_all_remote_devices() {
    std::vector<int> participantIds;
    participantIds.reserve(m_senders.size());
    for (const auto& [participantId, sender] : m_senders) {
        (void)sender;
        participantIds.push_back(participantId);
    }
    for (int participantId : participantIds)
        remove_remote_devices(participantId);
}

InputRemoting::SenderRecord& InputRemoting::find_or_create_sender(int participantId) {
    auto [it, inserted] = m_senders.try_emplace(participantId);
    if (inserted)
        it->second.senderId = participantId;
    return it->second;
}

int InputRemoting::find_local_device_id(int participantId, int remoteDeviceId) const {
    const auto sender = m_senders.find(participantId);
    if (sender == m_senders.end())
        return 0;
    const auto device = sender->second.devices.find(remoteDeviceId);
    return device == sender->second.devices.end() ? 0 : device->second;
}

} // namespace insys

