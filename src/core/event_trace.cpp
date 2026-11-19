#include "insys/event_trace.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

#include "insys/device.h"
#include "insys/manager.h"

namespace insys {
namespace {

constexpr char kTraceMagic[8] = {'I', 'N', 'S', 'Y', 'T', 'R', 'C', 'E'};
constexpr uint32_t kTraceVersion = 1;
constexpr FourCC kTraceDeviceChangeType('D', 'C', 'H', 'G');

struct TraceDeviceChange {
    low::Event header;
    int32_t deviceId;
    int32_t change;
};

static_assert(sizeof(TraceDeviceChange) >= low::kHeaderSize);

std::size_t aligned_event_size(const low::Event* event) {
    return low::align_up(event->size, low::kEventAlignment);
}

bool next_event_offset(std::size_t& offset, const uint8_t* data,
                       std::size_t totalSize) {
    if (offset + low::kHeaderSize > totalSize)
        return false;
    const auto* event = reinterpret_cast<const low::Event*>(data + offset);
    if (event->size < low::kHeaderSize)
        return false;
    const std::size_t aligned = low::align_up(event->size, low::kEventAlignment);
    if (aligned > totalSize - offset)
        return false;
    offset += aligned;
    return true;
}

} // namespace

EventTrace::EventTrace(EventTraceOptions options) : m_options(options) {
    if (m_options.maxBufferBytes < low::kHeaderSize)
        m_options.maxBufferBytes = low::kHeaderSize;
}

EventTrace::~EventTrace() {
    disable();
}

void EventTrace::attach(Manager& manager) {
    if (m_manager == &manager)
        return;
    if (m_recording)
        disable();
    m_manager = &manager;
}

void EventTrace::enable(Manager& manager) {
    attach(manager);
    enable();
}

void EventTrace::enable() {
    if (m_recording)
        return;
    if (m_manager == nullptr) {
        set_error("EventTrace has no Manager; call attach() first");
        return;
    }

    try {
        m_raw.reserve(m_options.maxBufferBytes);
    } catch (...) {
        // Reserve is only an optimization. Recording still works with a
        // smaller buffer and will grow up to the configured limit.
    }

    m_eventListenerId = m_manager->add_event_listener(
        [this](low::Event& event, Device*) { on_event(event, nullptr); });
    if (m_options.recordDeviceChanges) {
        m_deviceListenerId = m_manager->add_device_change_listener(
            [this](Device* device, DeviceChange change) { on_device_change(device, change); });
    }
    m_recording = true;
    m_lastError.clear();
}

void EventTrace::disable() {
    if (m_manager != nullptr) {
        if (m_eventListenerId != 0)
            m_manager->remove_event_listener(m_eventListenerId);
        if (m_deviceListenerId != 0)
            m_manager->remove_device_change_listener(m_deviceListenerId);
    }
    m_eventListenerId = 0;
    m_deviceListenerId = 0;
    m_recording = false;
}

void EventTrace::clear() {
    m_raw.clear();
    m_eventCount = 0;
    m_droppedEvents = 0;
    m_replayEvents.clear();
    m_replayIndex = 0;
    m_lastError.clear();
}

void EventTrace::on_event(low::Event& event, Device*) {
    if (!m_recording || m_replaying)
        return;
    append_event(&event);
}

void EventTrace::on_device_change(Device* device, DeviceChange change) {
    if (!m_recording || m_replaying || !m_options.recordDeviceChanges)
        return;
    append_device_change(device != nullptr ? device->deviceId : 0, change);
}

void EventTrace::append_event(const low::Event* event) {
    if (event == nullptr || event->size < low::kHeaderSize)
        return;

    const std::size_t aligned = aligned_event_size(event);
    if (aligned > m_options.maxBufferBytes)
        return;

    while (m_raw.size() + aligned > m_options.maxBufferBytes) {
        if (m_options.stopWhenFull) {
            m_recording = false;
            set_error("EventTrace buffer is full");
            return;
        }
        if (!drop_oldest_event()) {
            m_recording = false;
            set_error("EventTrace buffer is full");
            return;
        }
    }

    const std::size_t offset = m_raw.size();
    m_raw.resize(offset + aligned, 0);
    std::memcpy(m_raw.data() + offset, event, event->size);
    ++m_eventCount;
    m_replayEvents.clear();
    m_replayIndex = 0;
}

void EventTrace::append_device_change(int deviceId, DeviceChange change) {
    TraceDeviceChange record{};
    record.header = low::make_event(kTraceDeviceChangeType, sizeof(TraceDeviceChange),
                                     deviceId, m_manager != nullptr ? m_manager->runtime().now() : 0.0);
    record.deviceId = deviceId;
    record.change = static_cast<int32_t>(change);
    append_event(&record.header);
}

bool EventTrace::drop_oldest_event() {
    if (m_raw.empty())
        return false;

    const auto* event = reinterpret_cast<const low::Event*>(m_raw.data());
    if (event->size < low::kHeaderSize)
        return false;

    const std::size_t aligned = low::align_up(event->size, low::kEventAlignment);
    if (aligned > m_raw.size())
        return false;

    m_raw.erase(m_raw.begin(), m_raw.begin() + static_cast<std::ptrdiff_t>(aligned));
    if (m_eventCount > 0)
        --m_eventCount;
    ++m_droppedEvents;
    m_replayEvents.clear();
    m_replayIndex = 0;
    return true;
}

bool EventTrace::save(const std::filesystem::path& path) const {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        const_cast<EventTrace*>(this)->set_error("Cannot open trace file for writing: " +
                                                 path.string());
        return false;
    }

    const uint32_t version = kTraceVersion;
    const uint64_t eventCount = static_cast<uint64_t>(m_eventCount);
    const uint64_t totalBytes = static_cast<uint64_t>(m_raw.size());
    output.write(kTraceMagic, sizeof(kTraceMagic));
    output.write(reinterpret_cast<const char*>(&version), sizeof(version));
    output.write(reinterpret_cast<const char*>(&eventCount), sizeof(eventCount));
    output.write(reinterpret_cast<const char*>(&totalBytes), sizeof(totalBytes));
    if (!m_raw.empty())
        output.write(reinterpret_cast<const char*>(m_raw.data()),
                     static_cast<std::streamsize>(m_raw.size()));
    if (!output) {
        const_cast<EventTrace*>(this)->set_error("Failed writing trace file: " + path.string());
        return false;
    }
    return true;
}

bool EventTrace::validate_events(const std::vector<uint8_t>& data,
                                 std::size_t expectedCount) const {
    std::size_t offset = 0;
    std::size_t count = 0;
    while (offset < data.size()) {
        if (!next_event_offset(offset, data.data(), data.size()))
            return false;
        ++count;
    }
    return offset == data.size() && count == expectedCount;
}

bool EventTrace::load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        set_error("Cannot open trace file for reading: " + path.string());
        return false;
    }

    char magic[8]{};
    uint32_t version = 0;
    uint64_t eventCount = 0;
    uint64_t totalBytes = 0;
    input.read(magic, sizeof(magic));
    input.read(reinterpret_cast<char*>(&version), sizeof(version));
    input.read(reinterpret_cast<char*>(&eventCount), sizeof(eventCount));
    input.read(reinterpret_cast<char*>(&totalBytes), sizeof(totalBytes));
    if (!input || std::memcmp(magic, kTraceMagic, sizeof(kTraceMagic)) != 0) {
        set_error("Invalid trace file magic");
        return false;
    }
    if (version != kTraceVersion) {
        set_error("Unsupported trace file version");
        return false;
    }
    if (totalBytes > std::numeric_limits<std::size_t>::max() ||
        totalBytes > m_options.maxBufferBytes) {
        set_error("Trace file is larger than the configured buffer");
        return false;
    }

    std::vector<uint8_t> data(static_cast<std::size_t>(totalBytes));
    if (!data.empty())
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!input || !validate_events(data, static_cast<std::size_t>(eventCount))) {
        set_error("Malformed trace event stream");
        return false;
    }

    m_raw = std::move(data);
    m_eventCount = static_cast<std::size_t>(eventCount);
    m_droppedEvents = 0;
    m_replayEvents.clear();
    m_replayIndex = 0;
    m_lastError.clear();
    return true;
}

void EventTrace::rebuild_replay_events() {
    m_replayEvents.clear();
    m_replayIndex = 0;

    std::size_t offset = 0;
    while (offset < m_raw.size()) {
        auto* event = reinterpret_cast<low::Event*>(m_raw.data() + offset);
        if (event->size < low::kHeaderSize)
            break;
        if (event->type != kTraceDeviceChangeType.value)
            m_replayEvents.push_back(event);
        const std::size_t aligned = low::align_up(event->size, low::kEventAlignment);
        if (aligned > m_raw.size() - offset)
            break;
        offset += aligned;
    }

    std::stable_sort(m_replayEvents.begin(), m_replayEvents.end(),
                     [](const low::Event* left, const low::Event* right) {
                         return left->time < right->time;
                     });
}

void EventTrace::reset_replay() {
    m_replayEvents.clear();
    m_replayIndex = 0;
}

bool EventTrace::replay_next(Manager& manager) {
    if (m_replayEvents.empty() && m_replayIndex == 0)
        rebuild_replay_events();
    if (m_replayIndex >= m_replayEvents.size())
        return false;

    const low::Event* source = m_replayEvents[m_replayIndex++];
    if (source == nullptr || source->size < low::kHeaderSize)
        return false;

    std::vector<uint8_t> copy(low::align_up(source->size, low::kEventAlignment), 0);
    std::memcpy(copy.data(), source, source->size);
    auto* event = reinterpret_cast<low::Event*>(copy.data());
    event->eventId = 0;
    low::set_event_handled(event, false);

    m_replaying = true;
    manager.queue_event(event);
    manager.update(UpdateType::Dynamic);
    m_replaying = false;
    return true;
}

void EventTrace::replay(Manager& manager) {
    replay(manager, 1.0);
}

void EventTrace::replay(Manager& manager, double speed) {
    if (speed <= 0.0) {
        set_error("Replay speed must be positive");
        return;
    }

    reset_replay();
    rebuild_replay_events();
    while (replay_next(manager)) {
    }
}

} // namespace insys