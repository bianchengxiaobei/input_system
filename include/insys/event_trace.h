#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "insys/input_listeners.h"
#include "insys/low/event.h"

namespace insys {

class Manager;

struct EventTraceOptions {
    std::size_t maxBufferBytes = 4u * 1024u * 1024u;
    bool recordDeviceChanges = true;
    bool stopWhenFull = false;
};

// Records low-level events through Manager's public event listener and can
// save/load/replay them. The trace is intentionally not part of the hot path
// when disabled; a disabled trace registers no listeners.
class EventTrace {
public:
    using Options = EventTraceOptions;

    explicit EventTrace(EventTraceOptions options = {});
    ~EventTrace();

    EventTrace(const EventTrace&) = delete;
    EventTrace& operator=(const EventTrace&) = delete;

    void attach(Manager& manager);
    void enable();
    void enable(Manager& manager);
    void disable();
    bool enabled() const { return m_recording; }

    void clear();

    bool save(const std::filesystem::path& path) const;
    bool load(const std::filesystem::path& path);

    void replay(Manager& manager);
    void replay(Manager& manager, double speed);
    bool replay_next(Manager& manager);
    void reset_replay();

    std::size_t event_count() const { return m_eventCount; }
    std::size_t total_bytes() const { return m_raw.size(); }
    std::size_t dropped_event_count() const { return m_droppedEvents; }
    const std::vector<uint8_t>& raw() const { return m_raw; }
    const std::string& last_error() const { return m_lastError; }
    const EventTraceOptions& options() const { return m_options; }

private:
    void on_event(low::Event& event, Device* device);
    void on_device_change(Device* device, DeviceChange change);
    void append_event(const low::Event* event);
    void append_device_change(int deviceId, DeviceChange change);
    bool drop_oldest_event();
    void rebuild_replay_events();
    bool validate_events(const std::vector<uint8_t>& data, std::size_t expectedCount) const;
    void set_error(std::string message) { m_lastError = std::move(message); }

    EventTraceOptions m_options;
    Manager* m_manager = nullptr;
    int m_eventListenerId = 0;
    int m_deviceListenerId = 0;
    bool m_recording = false;
    bool m_replaying = false;

    std::vector<uint8_t> m_raw;
    std::size_t m_eventCount = 0;
    std::size_t m_droppedEvents = 0;
    std::string m_lastError;

    std::vector<low::Event*> m_replayEvents;
    std::size_t m_replayIndex = 0;
};

} // namespace insys
