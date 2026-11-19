#include "insys/backend/hid_report_queue.h"

#include <algorithm>
#include <utility>

#include "insys/backend/hid_backend.h"

namespace insys::backend {

HidReportQueue::HidReportQueue(std::size_t capacity) : m_capacity(capacity) {}

void HidReportQueue::drop_oldest_locked() {
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (!it->removal) {
            m_entries.erase(it);
            ++m_dropped;
            return;
        }
    }
    if (!m_entries.empty()) {
        m_entries.erase(m_entries.begin());
        ++m_dropped;
    }
}

bool HidReportQueue::push_report(std::string_view id, const uint8_t* data,
                                 std::size_t size) {
    if (id.empty() || data == nullptr || size == 0)
        return false;

    Entry entry;
    entry.id.assign(id.data(), id.size());
    entry.data.assign(data, data + size);

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_capacity != 0 && m_entries.size() >= m_capacity)
        drop_oldest_locked();
    m_entries.push_back(std::move(entry));
    return true;
}

bool HidReportQueue::push_removal(std::string_view id) {
    if (id.empty())
        return false;

    Entry entry;
    entry.id.assign(id.data(), id.size());
    entry.removal = true;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_capacity != 0 && m_entries.size() >= m_capacity)
        drop_oldest_locked();
    m_entries.push_back(std::move(entry));
    return true;
}

std::size_t HidReportQueue::drain(HidBackend& backend) {
    std::vector<Entry> entries;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        entries.swap(m_entries);
    }

    std::size_t applied = 0;
    for (const Entry& entry : entries) {
        if (entry.removal) {
            if (backend.remove_device(entry.id))
                ++applied;
            continue;
        }
        if (backend.feed_report(entry.id, entry.data.data(), entry.data.size()))
            ++applied;
    }
    return applied;
}

void HidReportQueue::discard(std::string_view id) {
    if (id.empty())
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
                       [&](const Entry& entry) { return entry.id == id; }),
        m_entries.end());
}

void HidReportQueue::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
    m_dropped = 0;
}

bool HidReportQueue::empty() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.empty();
}

std::size_t HidReportQueue::pending() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
}

std::size_t HidReportQueue::capacity() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_capacity;
}

void HidReportQueue::set_capacity(std::size_t capacity) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_capacity = capacity;
    if (m_capacity != 0) {
        while (m_entries.size() > m_capacity)
            drop_oldest_locked();
    }
}

std::size_t HidReportQueue::take_dropped_count() {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::size_t result = m_dropped;
    m_dropped = 0;
    return result;
}

} // namespace insys::backend
