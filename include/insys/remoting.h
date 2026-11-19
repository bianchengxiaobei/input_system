#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace insys {

class Device;
class Manager;

namespace low {
struct Event;
}

enum class RemoteMessageType : uint8_t {
    Connect = 0,
    Disconnect,
    NewDevice,
    NewEvents,
    RemoveDevice,
    StartSending,
    StopSending,
};

struct RemoteMessage {
    int participantId = 0;
    RemoteMessageType type = RemoteMessageType::Connect;
    std::vector<uint8_t> payload;
};

inline constexpr uint32_t kRemoteMaxPayloadBytes = 4u * 1024u * 1024u;

std::vector<uint8_t> encode_remote_message(const RemoteMessage& message);
bool decode_remote_message(const uint8_t* data, std::size_t size,
                           RemoteMessage& message,
                           std::size_t* consumed = nullptr);

class IRemoteTransport {
public:
    virtual ~IRemoteTransport() = default;
    virtual bool send(const RemoteMessage& message) = 0;
    virtual bool receive(RemoteMessage& message) = 0;
};

class LoopbackTransport final : public IRemoteTransport {
public:
    LoopbackTransport();
    ~LoopbackTransport() override;

    LoopbackTransport(const LoopbackTransport&) = delete;
    LoopbackTransport& operator=(const LoopbackTransport&) = delete;
    LoopbackTransport(LoopbackTransport&&) noexcept;
    LoopbackTransport& operator=(LoopbackTransport&&) noexcept;

    static std::pair<LoopbackTransport, LoopbackTransport> create_pair();

    bool send(const RemoteMessage& message) override;
    bool receive(RemoteMessage& message) override;

    std::size_t pending() const;

private:
    struct Mailbox;

    LoopbackTransport(std::shared_ptr<Mailbox> outbox,
                      std::shared_ptr<Mailbox> inbox);

    std::shared_ptr<Mailbox> m_outbox;
    std::shared_ptr<Mailbox> m_inbox;
};

struct InputRemotingOptions {
    bool startSendingOnConnect = false;
    bool sendInitialState = true;
};

class InputRemoting {
public:
    InputRemoting(Manager& manager, IRemoteTransport& transport,
                  InputRemotingOptions options = {});
    ~InputRemoting();

    InputRemoting(const InputRemoting&) = delete;
    InputRemoting& operator=(const InputRemoting&) = delete;

    bool start_sending();
    void stop_sending();
    bool sending() const { return m_sending; }

    bool connect();
    bool disconnect();

    std::size_t update();

    int participant_id() const { return m_participantId; }
    void set_participant_id(int id);

    Device* remote_device(int participantId, int remoteDeviceId) const;
    std::size_t remote_device_count() const;

    const std::string& last_error() const { return m_lastError; }

private:
    struct SenderRecord {
        int senderId = 0;
        std::unordered_map<int, int> devices;
    };

    bool send_message(RemoteMessageType type, std::vector<uint8_t> payload = {});
    bool send_device(Device& device);
    bool send_initial_state(Device& device);
    bool send_initial_messages();
    bool send_events(const low::Event& event);

    void on_event(low::Event& event, Device* device);
    void on_device_change(Device* device, int change);

    void process_message(const RemoteMessage& message);
    void process_new_device(const RemoteMessage& message);
    void process_new_events(const RemoteMessage& message);
    void process_remove_device(const RemoteMessage& message);
    void process_disconnect(int participantId);
    void remove_remote_devices(int participantId);
    void remove_all_remote_devices();

    SenderRecord& find_or_create_sender(int participantId);
    int find_local_device_id(int participantId, int remoteDeviceId) const;

    Manager* m_manager = nullptr;
    IRemoteTransport* m_transport = nullptr;
    InputRemotingOptions m_options;
    bool m_sending = false;
    int m_participantId = 1;
    int m_eventListenerId = 0;
    int m_deviceListenerId = 0;
    std::unordered_map<int, SenderRecord> m_senders;
    std::string m_lastError;
};

} // namespace insys
